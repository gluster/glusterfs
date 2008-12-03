
#include <sys/stat.h>
#include <errno.h>
#include "fuse-options.h"

#define LOG_ERROR(args...)      gf_log ("fuse-options", GF_LOG_ERROR, ##args)
#define LOG_DEBUG(args...)    gf_log ("fuse-options", GF_LOG_DEBUG, ##args)

static int 
_fuse_options_mount_point_validate (const char *value_string, char **s)
{
	struct stat stbuf;
	char *str = NULL;
	
	if (value_string == NULL) {
		return -1;
	}
	
	if (stat (value_string, &stbuf) != 0) {
		if (errno == ENOENT) {
			LOG_ERROR ("%s %s does not exist",
				   GF_FUSE_MOUNT_POINT_OPTION_STRING,
				   value_string);
		} else if (errno == ENOTCONN) {
			LOG_ERROR ("%s %s seems to have a stale mount, try 'umount %s' and re-run glusterfs",
				   GF_FUSE_MOUNT_POINT_OPTION_STRING, value_string, value_string);
		} else {
			LOG_ERROR ("%s %s : stat returned %s",
				   GF_FUSE_MOUNT_POINT_OPTION_STRING,
				   value_string, strerror (errno));
		}
		return -1;
	}
	
	if (S_ISDIR (stbuf.st_mode) == 0) {
		LOG_ERROR ("%s %s is not a directory",
			   GF_FUSE_MOUNT_POINT_OPTION_STRING,
			   value_string);
		return -1;
	}
	
	if ((str = strdup (value_string)) == NULL) {
		return -1;
	}
	
	*s = str;
	
	return 0;
}

static int 
_fuse_options_attribute_timeout_validate (const char *value_string, uint16_t *n)
{
	uint16_t value = 0;
	
	if (value_string == NULL) {
		return -1;
	}
	
	if (gf_string2uint16_base10 (value_string, &value) != 0) {
		LOG_ERROR ("invalid number format [%s] of option [%s]",
			   value_string,
			   GF_FUSE_ATTRIBUTE_TIMEOUT_OPTION_STRING);
		return -1;
	}
	
	if ((value <= GF_FUSE_ATTRIBUTE_TIMEOUT_VALUE_MIN) ||
	    (value >= GF_FUSE_ATTRIBUTE_TIMEOUT_VALUE_MAX)) {
		LOG_ERROR ("out of range [%d] of option [%s].  Allowed range is %d to %d.", 
			   value, 
			   GF_FUSE_ATTRIBUTE_TIMEOUT_OPTION_STRING, 
			   GF_FUSE_ATTRIBUTE_TIMEOUT_VALUE_MIN, 
			   GF_FUSE_ATTRIBUTE_TIMEOUT_VALUE_MAX);
		return -1;
	}
	
	*n = value;
	
	return 0;
}

static int 
_fuse_options_dir_entry_timeout_validate (const char *value_string, uint16_t *n)
{
	uint16_t value = 0;
	
	if (value_string == NULL) {
		return -1;
	}
	
	if (gf_string2uint16_base10 (value_string, &value) != 0) {
		LOG_ERROR ("invalid number format [%s] of option [%s]",
			   value_string,
			   GF_FUSE_ENTRY_TIMEOUT_OPTION_STRING);
		return -1;
	}
	
	if ((value <= GF_FUSE_ENTRY_TIMEOUT_VALUE_MIN) ||
	    (value >= GF_FUSE_ENTRY_TIMEOUT_VALUE_MAX)) {
		LOG_ERROR ("out of range [%d] of option [%s].  Allowed range is %d to %d.", 
			   value, 
			   GF_FUSE_ENTRY_TIMEOUT_OPTION_STRING, 
			   GF_FUSE_ENTRY_TIMEOUT_VALUE_MIN, 
			   GF_FUSE_ENTRY_TIMEOUT_VALUE_MAX);
		return -1;
	}
	
	*n = value;
	
	return 0;
}

static int 
_fuse_options_direct_io_mode_validate (const char *value_string, gf_boolean_t *b)
{
	gf_boolean_t value;
	if (value_string == NULL) {
		return -1;
	}
	
	if (gf_string2boolean (value_string, &value) != 0) {
		LOG_ERROR ("invalid value [%s] of option [%s]",
			   value_string,
			   GF_FUSE_DIRECT_IO_MODE_OPTION_STRING);
		return -1;
	}
	
	*b = value;
	return 0;
}

int 
fuse_options_validate (const dict_t *options, fuse_options_t *fuse_options)
{
	const char *value_string = NULL;
	
	if (options == NULL || fuse_options == NULL) {
		return -1;
	}
	
	value_string = data_to_str (dict_get ((dict_t *) options, GF_FUSE_MOUNT_POINT_OPTION_STRING));
	if (value_string == NULL) {
                LOG_ERROR ("mandatory option mount-point is not specified");
		return -1;
	}
	if (_fuse_options_mount_point_validate (value_string, &fuse_options->mount_point) != 0) {
		return -1;
	}
	LOG_DEBUG ("using %s = %s", GF_FUSE_MOUNT_POINT_OPTION_STRING, fuse_options->mount_point);
	
	value_string = data_to_str (dict_get ((dict_t *) options, GF_FUSE_ATTRIBUTE_TIMEOUT_OPTION_STRING));
	if (value_string != NULL) {
		if (_fuse_options_attribute_timeout_validate (value_string, &fuse_options->attr_timeout) != 0)
			return -1;
		
		LOG_DEBUG ("using %s = %d", GF_FUSE_ATTRIBUTE_TIMEOUT_OPTION_STRING, fuse_options->attr_timeout);
	}
	else {
		fuse_options->attr_timeout = GF_FUSE_ATTRIBUTE_TIMEOUT_VALUE_DEFAULT;
		LOG_DEBUG ("using %s = %d [default]", GF_FUSE_ATTRIBUTE_TIMEOUT_OPTION_STRING, fuse_options->attr_timeout);
	}
	
	value_string = data_to_str (dict_get ((dict_t *) options, GF_FUSE_ENTRY_TIMEOUT_OPTION_STRING));
	if (value_string != NULL) {
		if (_fuse_options_dir_entry_timeout_validate (value_string, &fuse_options->entry_timeout) != 0)
			return -1;
		
		LOG_DEBUG ("using %s = %d", GF_FUSE_ENTRY_TIMEOUT_OPTION_STRING, fuse_options->entry_timeout);
	}
	else {
		fuse_options->entry_timeout = GF_FUSE_ENTRY_TIMEOUT_VALUE_DEFAULT;
		LOG_DEBUG ("using %s = %d [default]", GF_FUSE_ENTRY_TIMEOUT_OPTION_STRING, fuse_options->entry_timeout);
	}
	
	value_string = data_to_str (dict_get ((dict_t *) options, GF_FUSE_DIRECT_IO_MODE_OPTION_STRING));
	if (value_string != NULL) {
		if (_fuse_options_direct_io_mode_validate (value_string, &fuse_options->direct_io_mode) != 0)
			return -1;
		
		LOG_DEBUG ("using %s = %d", GF_FUSE_DIRECT_IO_MODE_OPTION_STRING, fuse_options->direct_io_mode);
	}
	else {
		fuse_options->direct_io_mode = GF_FUSE_DIRECT_IO_MODE_VALUE_DEFAULT;
		LOG_DEBUG ("using %s = %d [default]", GF_FUSE_DIRECT_IO_MODE_OPTION_STRING, fuse_options->direct_io_mode);
	}
	
	return 0;
}
