#include <sys/param.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/acl.h>

int do_setfacl(const char *path, const char *options, const char *textacl)
{
	int r;
	int type;
	acl_t acl;
	int dob;
	int dok;
	int dom;
	struct stat st;
	char textmode[30];

	r = 0;
	dob = strchr(options,'b') != (char*)NULL;
	dok = strchr(options,'k') != (char*)NULL;
	dom = strchr(options,'m') != (char*)NULL;
	if ((dom && !textacl)
	    || (!dom && (textacl || (!dok && !dob) ||
                                    strchr(options,'d')))) {
		errno = EBADRQC; /* "bad request" */
		r = -1;
	} else {
		if (dob || dok) {
			r = acl_delete_def_file(path);
		}
		if (dob && !r) {
			if (!stat(path,&st)) {
				sprintf(textmode,
                                        "u::%c%c%c,g::%c%c%c,o::%c%c%c",
					(st.st_mode & 0400 ? 'r' : '-'),
					(st.st_mode & 0200 ? 'w' : '-'),
					(st.st_mode & 0100 ? 'x' : '-'),
					(st.st_mode & 0040 ? 'r' : '-'),
					(st.st_mode & 0020 ? 'w' : '-'),
					(st.st_mode & 0010 ? 'x' : '-'),
					(st.st_mode & 004 ? 'r' : '-'),
					(st.st_mode & 002 ? 'w' : '-'),
					(st.st_mode & 001 ? 'x' : '-'));
				acl = acl_from_text(textmode);
				if (acl) {
					r = acl_set_file(path,
                                                         ACL_TYPE_ACCESS,acl);
					acl_free(acl);
				} else
					r = -1;
			} else
				r = -1;
		}
		if (!r && dom) {
			if (strchr(options,'d'))
				type = ACL_TYPE_DEFAULT;
			else
				type = ACL_TYPE_ACCESS;
			acl = acl_from_text(textacl);
			if (acl) {
				r = acl_set_file(path,type,acl);
				acl_free(acl);
			} else
				r = -1;
		}
	}
	if (r)
		r = -errno;
	return (r);
}


int main(int argc, char *argv[]){
	int rc = 0;
	if (argc != 4) {
		fprintf(stderr,
                        "usage: ./setfacl_test <path> <options> <textacl>\n");
		return 0;
	}
	if ((rc = do_setfacl(argv[1], argv[2], argv[3])) != 0){
		fprintf(stderr, "do_setfacl failed: %s\n", strerror(errno));
		return rc;
	}
	return 0;
}
