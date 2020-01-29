#define GFID_STR_LEN 37

#include "metadisp.h"

/*
 * backend.c
 *
 * functions responsible for converting user-facing paths to backend-style
 * "/$GFID" paths.
 */

int32_t
build_backend_loc(uuid_t gfid, loc_t *src_loc, loc_t *dst_loc)
{
    static uuid_t root = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    char gfid_buf[GFID_STR_LEN + 1] = {
        0,
    };
    char *path = NULL;

    GF_VALIDATE_OR_GOTO("metadisp", src_loc, out);
    GF_VALIDATE_OR_GOTO("metadisp", dst_loc, out);

    loc_copy(dst_loc, src_loc);
    memcpy(dst_loc->pargfid, root, sizeof(root));
    GF_FREE((char *)dst_loc->path);  // we are overwriting path so nuke
                                     // whatever loc_copy gave us

    uuid_utoa_r(gfid, gfid_buf);

    path = GF_CALLOC(GFID_STR_LEN + 1, sizeof(char),
                     gf_common_mt_char);  // freed via loc_wipe

    path[0] = '/';
    strncpy(path + 1, gfid_buf, GFID_STR_LEN);
    path[GFID_STR_LEN] = 0;
    dst_loc->path = path;
    if (src_loc->name)
        dst_loc->name = strrchr(dst_loc->path, '/');
    if (dst_loc->name)
        dst_loc->name++;
    return 0;
out:
    return -1;
}
