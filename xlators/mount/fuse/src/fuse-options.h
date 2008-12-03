
#ifndef _FUSE_OPTIONS_H
#define _FUSE_OPTIONS_H

#include <stdint.h>

#include "common-utils.h"
#include "dict.h"

#define GF_FUSE_MOUNT_POINT_OPTION_STRING          "mount-point"

#define GF_FUSE_ATTRIBUTE_TIMEOUT_OPTION_STRING    "attr-timeout"
#define GF_FUSE_ATTRIBUTE_TIMEOUT_VALUE_DEFAULT    1
#define GF_FUSE_ATTRIBUTE_TIMEOUT_VALUE_MIN        0
#define GF_FUSE_ATTRIBUTE_TIMEOUT_VALUE_MAX        UINT16_MAX

#define GF_FUSE_ENTRY_TIMEOUT_OPTION_STRING    "entry-timeout"
#define GF_FUSE_ENTRY_TIMEOUT_VALUE_DEFAULT    1
#define GF_FUSE_ENTRY_TIMEOUT_VALUE_MIN        0
#define GF_FUSE_ENTRY_TIMEOUT_VALUE_MAX        UINT16_MAX

#define GF_FUSE_DIRECT_IO_MODE_OPTION_STRING       "direct-io-mode"
#define GF_FUSE_DIRECT_IO_MODE_VALUE_DEFAULT       1

struct _fuse_options
{
	char          *mount_point;
	uint16_t      attr_timeout;
	uint16_t      entry_timeout;
	gf_boolean_t  direct_io_mode;
};

typedef struct _fuse_options fuse_options_t;

int fuse_options_validate (const dict_t *options, fuse_options_t *fuse_options);

#endif
