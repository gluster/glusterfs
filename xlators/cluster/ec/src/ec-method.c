/*
  Copyright (c) 2012-2015 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <string.h>
#include <inttypes.h>

#include "ec-types.h"
#include "ec-mem-types.h"
#include "ec-galois.h"
#include "ec-code.h"
#include "ec-method.h"
#include "ec-helpers.h"

static void
ec_method_matrix_normal(ec_gf_t *gf, uint32_t *matrix, uint32_t columns,
                        uint32_t *values, uint32_t count)
{
    uint32_t i, j, v, tmp;

    columns--;
    for (i = 0; i < count; i++) {
        v = *values++;
        *matrix++ = tmp = ec_gf_exp(gf, v, columns);
        for (j = 0; j < columns; j++) {
            *matrix++ = tmp = ec_gf_div(gf, tmp, v);
        }
    }
}

static void
ec_method_matrix_inverse(ec_gf_t *gf, uint32_t *matrix, uint32_t *values,
                         uint32_t count)
{
    uint32_t a[count];
    uint32_t i, j, p, last, tmp;

    last = count - 1;
    for (i = 0; i < last; i++) {
        a[i] = 1;
    }
    a[i] = values[0];
    for (i = last; i > 0; i--) {
        for (j = i - 1; j < last; j++) {
            a[j] = a[j + 1] ^ ec_gf_mul(gf, values[i], a[j]);
        }
        a[j] = ec_gf_mul(gf, values[i], a[j]);
    }
    for (i = 0; i < count; i++) {
        p = a[0];
        matrix += count;
        *matrix = tmp = p ^ values[i];
        for (j = 1; j < last; j++) {
            matrix += count;
            *matrix = tmp = a[j] ^ ec_gf_mul(gf, values[i], tmp);
            p = tmp ^ ec_gf_mul(gf, values[i], p);
        }
        for (j = 0; j < last; j++) {
            *matrix = ec_gf_div(gf, *matrix, p);
            matrix -= count;
        }
        *matrix = ec_gf_div(gf, 1, p);
        matrix++;
    }
}

static void
ec_method_matrix_init(ec_matrix_list_t *list, ec_matrix_t *matrix,
                      uintptr_t mask, uint32_t *rows, gf_boolean_t inverse)
{
    uint32_t i;

    matrix->refs = 1;
    matrix->mask = mask;
    matrix->code = list->code;
    matrix->columns = list->columns;
    INIT_LIST_HEAD(&matrix->lru);

    if (inverse) {
        matrix->rows = list->columns;
        ec_method_matrix_inverse(matrix->code->gf, matrix->values, rows,
                                 matrix->rows);
        for (i = 0; i < matrix->rows; i++) {
            matrix->row_data[i].values = matrix->values + i * matrix->columns;
            matrix->row_data[i].func.interleaved =
                ec_code_build_interleaved(matrix->code,
                                          EC_METHOD_WORD_SIZE,
                                          matrix->row_data[i].values,
                                          matrix->columns);
        }
    } else {
        matrix->rows = list->rows;
        ec_method_matrix_normal(matrix->code->gf, matrix->values,
                                matrix->columns, rows, matrix->rows);
        for (i = 0; i < matrix->rows; i++) {
            matrix->row_data[i].values = matrix->values + i * matrix->columns;
            matrix->row_data[i].func.linear =
                ec_code_build_linear(matrix->code, EC_METHOD_WORD_SIZE,
                                     matrix->row_data[i].values,
                                     matrix->columns);
        }
    }
}

static void
ec_method_matrix_release(ec_matrix_t *matrix)
{
    uint32_t i;

    for (i = 0; i < matrix->rows; i++) {
        if (matrix->row_data[i].func.linear != NULL) {
            ec_code_release(matrix->code, &matrix->row_data[i].func);
            matrix->row_data[i].func.linear = NULL;
        }
    }
}

static void
ec_method_matrix_destroy(ec_matrix_list_t *list, ec_matrix_t *matrix)
{
    list_del_init(&matrix->lru);

    ec_method_matrix_release(matrix);

    mem_put(matrix);

    list->count--;
}

static void
ec_method_matrix_unref(ec_matrix_list_t *list, ec_matrix_t *matrix)
{
    if (--matrix->refs == 0) {
        list_add_tail(&matrix->lru, &list->lru);
        if (list->count > list->max) {
            matrix = list_first_entry(&list->lru, ec_matrix_t, lru);
            ec_method_matrix_destroy(list, matrix);
        }
    }
}

static ec_matrix_t *
ec_method_matrix_lookup(ec_matrix_list_t *list, uintptr_t mask, uint32_t *pos)
{
    ec_matrix_t *matrix;
    uint32_t i, j, k;

    i = 0;
    j = list->count;
    while (i < j) {
        k = (i + j) >> 1;
        matrix = list->objects[k];
        if (matrix->mask == mask) {
            *pos = k;
            return matrix;
        }
        if (matrix->mask < mask) {
            i = k + 1;
        } else {
            j = k;
        }
    }
    *pos = i;

    return NULL;
}

static void
ec_method_matrix_remove(ec_matrix_list_t *list, uintptr_t mask)
{
    uint32_t pos;

    if (ec_method_matrix_lookup(list, mask, &pos) != NULL) {
        list->count--;
        if (pos < list->count) {
            memmove(list->objects + pos, list->objects + pos + 1,
                    sizeof(ec_matrix_t *) * (list->count - pos));
        }
    }
}

static void
ec_method_matrix_insert(ec_matrix_list_t *list, ec_matrix_t *matrix)
{
    uint32_t pos;

    GF_ASSERT(ec_method_matrix_lookup(list, matrix->mask, &pos) == NULL);

    if (pos < list->count) {
        memmove(list->objects + pos + 1, list->objects + pos,
                sizeof(ec_matrix_t *) * (list->count - pos));
    }
    list->objects[pos] = matrix;
    list->count++;
}

static ec_matrix_t *
ec_method_matrix_get(ec_matrix_list_t *list, uintptr_t mask, uint32_t *rows)
{
    ec_matrix_t *matrix;
    uint32_t pos;

    LOCK(&list->lock);

    matrix = ec_method_matrix_lookup(list, mask, &pos);
    if (matrix != NULL) {
        list_del_init(&matrix->lru);
        matrix->refs++;

        goto out;
    }

    if ((list->count >= list->max) && !list_empty(&list->lru)) {
        matrix = list_first_entry(&list->lru, ec_matrix_t, lru);
        list_del_init(&matrix->lru);

        ec_method_matrix_remove(list, matrix->mask);

        ec_method_matrix_release(matrix);
    } else {
        matrix = mem_get0(list->pool);
        if (matrix == NULL) {
            matrix = EC_ERR(ENOMEM);
            goto out;
        }
        matrix->values = (uint32_t *)((uintptr_t)matrix + sizeof(ec_matrix_t) +
                                      sizeof(ec_matrix_row_t) * list->columns);
    }

    ec_method_matrix_init(list, matrix, mask, rows, _gf_true);

    if (list->count < list->max) {
        ec_method_matrix_insert(list, matrix);
    } else {
        matrix->mask = 0;
    }

out:
    UNLOCK(&list->lock);

    return matrix;
}

static void
ec_method_matrix_put(ec_matrix_list_t *list, ec_matrix_t *matrix)
{
    LOCK(&list->lock);

    ec_method_matrix_unref(list, matrix);

    UNLOCK(&list->lock);
}

static int32_t
ec_method_setup(xlator_t *xl, ec_matrix_list_t *list, const char *gen)
{
    ec_matrix_t *matrix;
    uint32_t values[list->rows];
    uint32_t i;
    int32_t err;

    matrix = GF_MALLOC(sizeof(ec_matrix_t) +
                       sizeof(ec_matrix_row_t) * list->rows +
                       sizeof(uint32_t) * list->columns * list->rows,
                       ec_mt_ec_matrix_t);
    if (matrix == NULL) {
        err = -ENOMEM;
        goto failed;
    }
    memset(matrix, 0, sizeof(ec_matrix_t));
    matrix->values = (uint32_t *)((uintptr_t)matrix + sizeof(ec_matrix_t) +
                                  sizeof(ec_matrix_row_t) * list->rows);

    list->code = ec_code_create(list->gf, ec_code_detect(xl, gen));
    if (EC_IS_ERR(list->code)) {
        err = EC_GET_ERR(list->code);
        list->code = NULL;
        goto failed_matrix;
    }

    for (i = 0; i < list->rows; i++) {
        values[i] = i + 1;
    }
    ec_method_matrix_init(list, matrix, 0, values, _gf_false);

    list->encode = matrix;

    return 0;

failed_matrix:
    GF_FREE(matrix);
failed:
    return err;
}

int32_t
ec_method_init(xlator_t *xl, ec_matrix_list_t *list, uint32_t columns,
               uint32_t rows, uint32_t max, const char *gen)
{
    list->columns = columns;
    list->rows = rows;
    list->max = max;
    list->stripe = EC_METHOD_CHUNK_SIZE * list->columns;
    INIT_LIST_HEAD(&list->lru);
    int32_t err;

    list->pool = mem_pool_new_fn(sizeof(ec_matrix_t) +
                                 sizeof(ec_matrix_row_t) * columns +
                                 sizeof(uint32_t) * columns * columns,
                                 128, "ec_matrix_t");
    if (list->pool == NULL) {
        err = -ENOMEM;
        goto failed;
    }

    list->objects = GF_MALLOC(sizeof(ec_matrix_t *) * max, ec_mt_ec_matrix_t);
    if (list->objects == NULL) {
        err = -ENOMEM;
        goto failed_pool;
    }

    list->gf = ec_gf_prepare(EC_GF_BITS, EC_GF_MOD);
    if (EC_IS_ERR(list->gf)) {
        err = EC_GET_ERR(list->gf);
        goto failed_objects;
    }

    err = ec_method_setup(xl, list, gen);
    if (err != 0) {
        goto failed_gf;
    }

    LOCK_INIT(&list->lock);

    return 0;

failed_gf:
    ec_gf_destroy(list->gf);
failed_objects:
    GF_FREE(list->objects);
failed_pool:
    mem_pool_destroy(list->pool);
failed:
    list->pool = NULL;
    list->objects = NULL;
    list->gf = NULL;

    return err;
}

void
ec_method_fini(ec_matrix_list_t *list)
{
    ec_matrix_t *matrix;

    if (list->encode == NULL) {
        return;
    }

    while (!list_empty(&list->lru)) {
        matrix = list_first_entry(&list->lru, ec_matrix_t, lru);
        ec_method_matrix_destroy(list, matrix);
    }

    GF_ASSERT(list->count == 0);

    if (list->pool)/*Init was successful*/
            LOCK_DESTROY(&list->lock);

    ec_method_matrix_release(list->encode);
    GF_FREE(list->encode);

    ec_code_destroy(list->code);
    ec_gf_destroy(list->gf);
    GF_FREE(list->objects);
    mem_pool_destroy(list->pool);
}

int32_t
ec_method_update(xlator_t *xl, ec_matrix_list_t *list, const char *gen)
{
    /* TODO: Allow changing code generator */

    return 0;
}

void
ec_method_encode(ec_matrix_list_t *list, size_t size, void *in, void **out)
{
    ec_matrix_t *matrix;
    size_t pos;
    uint32_t i;

    matrix = list->encode;
    for (pos = 0; pos < size; pos += list->stripe) {
        for (i = 0; i < matrix->rows; i++) {
            matrix->row_data[i].func.linear(out[i], in, pos,
                                            matrix->row_data[i].values,
                                            list->columns);
            out[i] += EC_METHOD_CHUNK_SIZE;
        }
    }
}

int32_t
ec_method_decode(ec_matrix_list_t *list, size_t size, uintptr_t mask,
                 uint32_t *rows, void **in, void *out)
{
    ec_matrix_t *matrix;
    size_t pos;
    uint32_t i;

    matrix = ec_method_matrix_get(list, mask, rows);
    if (EC_IS_ERR(matrix)) {
        return EC_GET_ERR(matrix);
    }
    for (pos = 0; pos < size; pos += EC_METHOD_CHUNK_SIZE) {
        for (i = 0; i < matrix->rows; i++) {
            matrix->row_data[i].func.interleaved(out, in, pos,
                                                 matrix->row_data[i].values,
                                                 list->columns);
            out += EC_METHOD_CHUNK_SIZE;
        }
    }

    ec_method_matrix_put(list, matrix);

    return 0;
}
