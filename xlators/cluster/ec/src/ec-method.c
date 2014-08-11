/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <inttypes.h>

#include "ec-method.h"

#define EC_METHOD_WORD_SIZE 16

static uint32_t GfPow[EC_METHOD_SIZE << 1];
static uint32_t GfLog[EC_METHOD_SIZE << 1];

void ec_method_initialize(void)
{
    uint32_t i;

    GfPow[0] = 1;
    GfLog[0] = EC_METHOD_SIZE;
    for (i = 1; i < EC_METHOD_SIZE; i++)
    {
        GfPow[i] = GfPow[i - 1] << 1;
        if (GfPow[i] >= EC_METHOD_SIZE)
        {
            GfPow[i] ^= EC_GF_MOD;
        }
        GfPow[i + EC_METHOD_SIZE - 1] = GfPow[i];
        GfLog[GfPow[i] + EC_METHOD_SIZE - 1] = GfLog[GfPow[i]] = i;
    }
}

static uint32_t ec_method_mul(uint32_t a, uint32_t b)
{
    if (a && b)
    {
        return GfPow[GfLog[a] + GfLog[b]];
    }
    return 0;
}

static uint32_t ec_method_div(uint32_t a, uint32_t b)
{
    if (b)
    {
        if (a)
        {
            return GfPow[EC_METHOD_SIZE - 1 + GfLog[a] - GfLog[b]];
        }
        return 0;
    }
    return EC_METHOD_SIZE;
}

size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row,
                        uint8_t * in, uint8_t * out)
{
    uint32_t i, j;

    size /= EC_METHOD_CHUNK_SIZE * columns;
    row++;
    for (j = 0; j < size; j++)
    {
        ec_gf_load(in);
        in += EC_METHOD_CHUNK_SIZE;
        for (i = 1; i < columns; i++)
        {
            ec_gf_mul_table[row]();
            ec_gf_xor(in);
            in += EC_METHOD_CHUNK_SIZE;
        }
        ec_gf_store(out);
        out += EC_METHOD_CHUNK_SIZE;
    }

    return size * EC_METHOD_CHUNK_SIZE;
}

size_t ec_method_decode(size_t size, uint32_t columns, uint32_t * rows,
                        uint8_t ** in, uint8_t * out)
{
    uint32_t i, j, k;
    uint32_t f, off;
    uint8_t inv[EC_METHOD_MAX_FRAGMENTS][EC_METHOD_MAX_FRAGMENTS + 1];
    uint8_t mtx[EC_METHOD_MAX_FRAGMENTS][EC_METHOD_MAX_FRAGMENTS];
    uint8_t * p[EC_METHOD_MAX_FRAGMENTS];

    size /= EC_METHOD_CHUNK_SIZE;

    memset(inv, 0, sizeof(inv));
    memset(mtx, 0, sizeof(mtx));
    for (i = 0; i < columns; i++)
    {
        inv[i][i] = 1;
        inv[i][columns] = 1;
    }
    k = 0;
    for (i = 0; i < columns; i++)
    {
        mtx[k][columns - 1] = 1;
        for (j = columns - 1; j > 0; j--)
        {
            mtx[k][j - 1] = ec_method_mul(mtx[k][j], rows[i] + 1);
        }
        p[k] = in[i];
        k++;
    }

    for (i = 0; i < columns; i++)
    {
        f = mtx[i][i];
        for (j = 0; j < columns; j++)
        {
            mtx[i][j] = ec_method_div(mtx[i][j], f);
            inv[i][j] = ec_method_div(inv[i][j], f);
        }
        for (j = 0; j < columns; j++)
        {
            if (i != j)
            {
                f = mtx[j][i];
                for (k = 0; k < columns; k++)
                {
                    mtx[j][k] ^= ec_method_mul(mtx[i][k], f);
                    inv[j][k] ^= ec_method_mul(inv[i][k], f);
                }
            }
        }
    }
    off = 0;
    for (f = 0; f < size; f++)
    {
        for (i = 0; i < columns; i++)
        {
            ec_gf_load(p[0] + off);
            j = 0;
            while (j < columns)
            {
                k = j + 1;
                while (inv[i][k] == 0)
                {
                    k++;
                }
                ec_gf_mul_table[ec_method_div(inv[i][j], inv[i][k])]();
                if (k < columns)
                {
                    ec_gf_xor(p[k] + off);
                }
                j = k;
            }
            ec_gf_store(out);
            out += EC_METHOD_CHUNK_SIZE;
            in[i] += EC_METHOD_CHUNK_SIZE;
        }
        off += EC_METHOD_CHUNK_SIZE;
    }

    return size * EC_METHOD_CHUNK_SIZE * columns;
}
