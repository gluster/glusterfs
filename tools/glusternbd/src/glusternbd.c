/*
   Copyright (c) 2019 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/types.h>
#include <linux/nbd.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>

#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <glusterfs/api/glfs.h>
#include <libnl3/netlink/genl/genl.h>
#include <libnl3/netlink/genl/mngt.h>
#include <libnl3/netlink/genl/ctrl.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include "nbd-netlink.h"

#define ALLOWED_BSOFLAGS (O_DIRECT | O_RDWR | O_LARGEFILE)
#define NBD_CMD_MASK_COMMAND 0x0000ffff
#define NBD_GFAPI_LOG_FILE "/var/log/glusterfs/glusternbd.log"
#define NBD_GFAPI_LOG_LEVEL 7
#define NBD_NL_VERSION 1

const char *version_info = ""                                       \
"glusternbd (0)\n\n"                                                \
"Repository rev: https://github.com/gluster/gluster.git\n"          \
"Copyright (c) 2019 Red Hat, Inc. <https://redhat.com/>\n"          \
"gluster-nbd comes with ABSOLUTELY NO WARRANTY.\n"                  \
"It is licensed to you under your choice of the GNU Lesser\n"       \
"General Public License, version 3 or any later version (LGPLv3\n"  \
"or later), or the GNU General Public License, version 2 (GPLv2),\n"\
"in all cases as published by the Free Software Foundation.";

static void
usage(void)
{
	printf("Usage:\n" );
	printf("\tnbd <command> [<args>]\n\n");
	printf("Commands:\n");
	printf("\thelp\n");
	printf("\t\tdisplay help for nbd commands\n\n");
	printf("\tcreate <volname@host:/path> [prealloc <full|no>] <size SIZE>\n");
	printf("\t\tcreate path file on the volname volume, prealloc is no as default,\n");
	printf("\t\tand the SIZE is valid with B, K(iB), M(iB), G(iB), T(iB), P(iB), E(iB), Z(iB), Y(iB)\n\n");
	printf("\tdelete <volname@host:/path>\n");
	printf("\t\tdelete path file on the volname volume\n\n");
	printf("\tmap <volname@host:/path> [nbd-device] [threads NUM] [timeout TIME] [daemon on|off]\n");
	printf("\t\tmap path file to the nbd device, as default the threads 4, timeout 0 and daemon on\n\n");
	printf("\tumap <nbd-device>\n");
	printf("\t\tumap the nbd device\n\n");
	printf("\tlist <mapd|umap|all>\n");
	printf("\t\tlist the mapped|umapped|all nbd devices, all as default\n\n");
	printf("\tversion\n");
	printf("\t\tshow version info and exit.\n");
}

typedef enum nbd_commands {
	NBD_HELP,
	NBD_CREATE,
	NBD_DELETE,
	NBD_MAP,
	NBD_UNMAP,
	NBD_LIST,
	NBD_VERSION,

	NBD_OPT_MAX
} nbd_command;

static const char *const nbd_command_loopup[] = {
	[NBD_HELP]           = "help",
	[NBD_DELETE]         = "delete",
	[NBD_CREATE]         = "create",
	[NBD_MAP]            = "map",
	[NBD_UNMAP]          = "umap",
	[NBD_LIST]           = "list",
	[NBD_VERSION]        = "version",

	[NBD_OPT_MAX]        = NULL,
};

static int nbd_device_index;
static struct nl_sock *sock;
static int driver_id;
static pthread_spinlock_t nbd_write_lock;

static int
nbd_command_lookup(const char *command)
{
	int i;

	if (!command)
		return -1;

	for (i = 0; i < NBD_OPT_MAX; i++) {
		if (!strcmp(nbd_command_loopup[i], command))
			return i;
	}

	return -1;
}

# define  NBD_DEFAULT_SECTOR_SIZE  512

# define round_down(a, b) ({           \
        __typeof__ (a) _a = (a);       \
        __typeof__ (b) _b = (b);       \
        (_a - (_a % _b)); })

static ssize_t
nbd_parse_size(const char *value)
{
	char *postfix;
	ssize_t sizef;

	if (!value)
		return -1;

	sizef = strtod(value, &postfix);
	if (sizef <= 0) {
		fprintf(stderr, "The size cannot be negative number or zero!\n");
		return -1;
	}

	switch (tolower(*postfix)) {
		case 'y':
			sizef *= 1024;
		case 'z':
			sizef *= 1024;
		case 'e':
			sizef *= 1024;
		case 'p':
			sizef *= 1024;
		case 't':
			sizef *= 1024;
		case 'g':
			sizef *= 1024;
		case 'm':
			sizef *= 1024;
		case 'k':
			sizef *= 1024;
		case 'b':
		case '\0':
			if (sizef < NBD_DEFAULT_SECTOR_SIZE) {
				fprintf(stderr, "minimum acceptable block size is %d bytes\n",
					NBD_DEFAULT_SECTOR_SIZE);
				return -1;
			}

			if (sizef % NBD_DEFAULT_SECTOR_SIZE) {
				fprintf(stdout, "The size %ld will align to sector size %d bytes\n",
				        sizef, NBD_DEFAULT_SECTOR_SIZE);
				sizef = round_down(sizef, NBD_DEFAULT_SECTOR_SIZE);
			}
			break;
		default:
			return -1;
	}

	return sizef;
}

static struct glfs *
nbd_volume_init(char *volume, char *host)
{
	struct glfs *glfs;
	int ret;

	glfs = glfs_new(volume);
	if (!glfs) {
		fprintf(stderr, "Not able to Initialize volume %s, %s\n",
			volume, strerror(errno));
		return NULL;
	}

	ret = glfs_set_volfile_server(glfs, "tcp", host, 24007);
	if (ret) {
		fprintf(stderr, "Not able to add Volfile server for volume %s, %s\n",
			volume, strerror(errno));
		goto out;
	}

	ret = glfs_set_logging(glfs, NBD_GFAPI_LOG_FILE, NBD_GFAPI_LOG_LEVEL);
	if (ret) {
		fprintf(stderr, "Not able to add logging for volume %s, %s\n",
			volume, strerror(errno));
		goto out;
	}

	ret = glfs_init(glfs);
	if (ret) {
		if (errno == ENOENT) {
			fprintf(stderr, "Volume %s does not exist\n", volume);
		} else if (errno == EIO) {
			fprintf(stderr, "Check if volume %s is operational\n",
				volume);
		} else {
			fprintf(stderr, "Not able to initialize volume %s, %s\n",
				volume, strerror(errno));
		}
		goto out;
	}

	return glfs;

out:
	glfs_fini(glfs);

	return NULL;
}

static bool
nbd_check_available_space(struct glfs *glfs, char *volume, size_t size)
{
	struct statvfs buf = {'\0', };

	if (!glfs_statvfs(glfs, "/", &buf)) {
		if ((buf.f_bfree * buf.f_bsize) >= size)
			return true;

		fprintf(stderr, "Low space on volume %s\n", volume);
		return false;
	}

	fprintf(stderr, "couldn't get file-system statistics on volume %s\n", volume);

	return false;
}

static int
nbd_parse_volfile(const char *cfg, char **volume, char **host, char **file)
{
	char *sep;
	char *tmp;
	char *ptr;
	int ret = 0;

	if (!cfg || !volume || !host || !file)
		return -1;

	tmp = strdup(cfg);
	if (!tmp) {
		fprintf(stderr, "No memory!");
		return -1;
	}

	sep = strchr(tmp, '@');
	if (!sep) {
		fprintf(stderr, "argument '<volname@host:/path>' (%s) is incorrect\n",
			cfg);
		ret = -1;
		goto out;
	}

	*sep = '\0';

	*volume = strdup(tmp);
	if (!*volume) {
		fprintf(stderr, "No memory for volume!\n");
		ret = -1;
		goto out;
	}

	sep++;
	ptr = sep;
	sep = strchr(ptr, ':');
	if (!sep) {
		fprintf(stderr, "argument '<volname@host:/path>' (%s) is incorrect\n",
			cfg);
		ret = -1;
		goto out;
	}

	*sep = '\0';

	*host = strdup(ptr);
	if (!*host) {
		fprintf(stderr, "No memory for host!\n");
		ret = -1;
		goto out;
	}

	sep++;
	if (*sep != '/') {
		fprintf(stderr, "argument '<volname@host:/path>' (%s) is incorrect\n",
			cfg);
		ret = -1;
		goto out;
	}
	sep++;

	*file = strdup(sep);
	if (!*file) {
		fprintf(stderr, "No memory for file!\n");
		ret = -1;
		goto out;
	}

out:
	free(tmp);
	return ret;
}

static int
nbd_create_file(int count, char **options)
{
	char *volume = NULL;
	char *host = NULL;
	char *file = NULL;
	int ret = 0;
	int ind;
	bool prealloc = false;
	ssize_t size = -1;
	struct glfs *glfs = NULL;
	struct glfs_fd *fd = NULL;

	if (count != 5 && count != 7) {
		fprintf(stderr, "Invalid arguments for create command!\n");
		return -1;
	}

	ret = nbd_parse_volfile(options[2], &volume, &host, &file);
	if (ret)
		return -1;

	ind = 3;
	while (ind < count) {
		if (!strcmp("prealloc", options[ind])) {
			if (!strcmp(options[ind + 1], "full")) {
				prealloc = true;
			} else if (!strcmp(options[ind + 1], "no")) {
				prealloc = false;
			} else {
				fprintf(stderr, "Invalid value for prealloc!\n");
				ret = -1;
				goto out;
			}

			ind += 2;
		} else if (!strcmp("size", options[ind])) {
			size = nbd_parse_size(options[ind + 1]);
			if (size < 0) {
				fprintf(stderr, "Invalid size!\n");
				ret = -1;
				goto out;
			}
			ind += 2;
		} else {
			ret = -1;
			goto out;
		}
	}

	if (size < 0) {
		fprintf(stderr, "Please specify the file size!\n");
		ret = -1;
		goto out;

	}

	glfs = nbd_volume_init(volume, host);
	if (!glfs) {
		ret = -1;
		goto out;
	}

	if (!glfs_access(glfs, file, F_OK)) {
		fprintf(stderr, "file with name %s already exist in the volume %s\n",
			file, volume);
		ret = -1;
		goto out;
	}

	if (!nbd_check_available_space(glfs, volume, size)) {
		ret = -1;
		goto out;
	}

	fd = glfs_creat(glfs, file, O_WRONLY | O_CREAT | O_EXCL | O_SYNC, S_IRUSR | S_IWUSR);
	if (!fd) {
		fprintf(stderr, "glfs_creat() on volume %s for file %s failed, %s\n",
			volume, file, strerror(errno));
		ret = -1;
		goto out;
	}

	ret = glfs_ftruncate(fd, size, NULL, NULL);
	if (ret) {
		fprintf(stderr, "glfs_ftruncate() on volume %s for file %s failed, %s\n",
			volume, file, strerror(errno));
		ret = -1;
		goto out;
	}

	if (prealloc) {
		ret = glfs_zerofill(fd, 0, size);
		if (ret) {
			fprintf(stderr, "glfs_zerofill() on volume %s for file %s failed, %s\n",
				volume, file, strerror(errno));
			ret = -1;
			goto out;
		}
	}

out:
	if (fd)
		glfs_close(fd);

	free(volume);
	free(host);
	free(file);
	return ret;
}

static int
nbd_delete_file(int count, char **options)
{
	char *volume = NULL;
	char *host = NULL;
	char *file = NULL;
	int ret = 0;
	struct glfs *glfs = NULL;

	if (count != 3) {
		fprintf(stderr, "Invalid arguments for delete command!\n");
		return -1;
	}

	ret = nbd_parse_volfile(options[2], &volume, &host, &file);
	if (ret)
		return -1;

	printf("volume: %s, host: %s, file: %s\n", volume, host, file);

	glfs = nbd_volume_init(volume, host);
	if (!glfs) {
		ret = -1;
		goto out;
	}

	if (glfs_access(glfs, file, F_OK)) {
		fprintf(stderr, "file with name %s is not exist on volume %s\n",
			file, volume);
		ret = -1;
		goto out;
	}

	ret = glfs_unlink(glfs, file);
	if (ret) {
		fprintf(stderr, "failed to delete file %s on volume %s\n",
			file, volume);
	}

out:
	free(volume);
	free(host);
	free(file);
	return ret;
}

static struct nl_sock *
nbd_socket_init(void)
{
	struct nl_sock *sock;

	sock = nl_socket_alloc();
	if (!sock) {
		fprintf(stderr, "Couldn't alloc socket, %s!\n", strerror(errno));
		return NULL;
	}

	nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, genl_handle_msg, NULL);

	if (genl_connect(sock)) {
		fprintf(stderr, "Couldn't connect to the nbd netlink socket, %s!\n",
			strerror(errno));
		goto err;
	}

	driver_id = genl_ctrl_resolve(sock, "nbd");
	if (driver_id < 0) {
		fprintf(stderr, "Couldn't resolve the nbd netlink family, %s!\n",
			strerror(errno));
		goto err;
	}

	return sock;
err:
	nl_socket_free(sock);
	return NULL;
}

/* Netlink interface. */
static struct nla_policy nbd_attr_policy[NBD_ATTR_MAX + 1] = {
	[NBD_ATTR_INDEX]		=	{ .type = NLA_U32 },
	[NBD_ATTR_SIZE_BYTES]		=	{ .type = NLA_U64 },
	[NBD_ATTR_BLOCK_SIZE_BYTES]	=	{ .type = NLA_U64 },
	[NBD_ATTR_TIMEOUT]		=	{ .type = NLA_U64 },
	[NBD_ATTR_SERVER_FLAGS]		=	{ .type = NLA_U64 },
	[NBD_ATTR_CLIENT_FLAGS]		=	{ .type = NLA_U64 },
	[NBD_ATTR_SOCKETS]		=	{ .type = NLA_NESTED},
	[NBD_ATTR_DEAD_CONN_TIMEOUT]	=	{ .type = NLA_U64 },
	[NBD_ATTR_DEVICE_LIST]		=	{ .type = NLA_NESTED},
};

typedef enum {
	NBD_LIST_MAPPED,
	NBD_LIST_UNMAPPED,
	NBD_LIST_ALL,
} list_type;

static int nbd_list_type = NBD_LIST_ALL;

static struct nla_policy nbd_device_policy[NBD_DEVICE_ATTR_MAX + 1] = {
	[NBD_DEVICE_INDEX]              =       { .type = NLA_U32 },
	[NBD_DEVICE_CONNECTED]          =       { .type = NLA_U8 },
};

static int
nbd_genl_map_done(struct genl_info *info)
{
	int index;

	if (!info) {
		fprintf(stderr, "Invalid info argument!\n");
		return -1;
	}

	index = (int)nla_get_u32(info->attrs[NBD_ATTR_INDEX]);
	nbd_device_index = index;

	fprintf(stderr, "mapped to /dev/nbd%d!\n", index);
	return 0;
}

static int
nbd_genl_list_devices(struct genl_info *info)
{
	struct nlattr *attr;
	int rem;
	int index;
	int status;
	int ret;

	if (!info) {
		fprintf(stderr, "Invalid info argument!\n");
		return -1;
	}

	if (!info->attrs[NBD_ATTR_DEVICE_LIST]) {
		fprintf(stderr, "NBD_ATTR_DEVICE_LIST not set in cmd!\n");
		return -1;
	}

	nla_for_each_nested(attr, info->attrs[NBD_ATTR_DEVICE_LIST], rem) {
		struct nlattr *devices[NBD_DEVICE_ATTR_MAX + 1];

		if (nla_type(attr) != NBD_DEVICE_ITEM) {
			fprintf(stderr, "NBD_DEVICE_ITEM not set!\n");
			return -1;
		}

		ret = nla_parse_nested(devices, NBD_DEVICE_ATTR_MAX, attr,
				       nbd_device_policy);
		if (ret) {
			fprintf(stderr, "nbd: error processing device list\n");
			return -1;
		}

		index = (int)nla_get_u32(devices[NBD_DEVICE_INDEX]);
		status = (int)nla_get_u8(devices[NBD_DEVICE_CONNECTED]);

		switch (nbd_list_type) {
		case NBD_LIST_MAPPED:
			if (status)
				fprintf(stderr, "/dev/nbd%d \t%s\n", index, "Mapped");
			break;
		case NBD_LIST_UNMAPPED:
			if (!status)
				fprintf(stderr, "/dev/nbd%d \t%s\n", index, "Unmapped");
			break;
		case NBD_LIST_ALL:
			fprintf(stderr, "/dev/nbd%d \t%s\n", index,
				status ? "Mapped" : "Unmapped");
			break;
		default:
			fprintf(stderr, "Invalid list type: %d!\n", nbd_list_type);
			return -1;
		}
	}

	return 0;
}

static int
handle_netlink(struct nl_cache_ops *unused, struct genl_cmd *cmd,
	       struct genl_info *info, void *arg)
{
	if (info->genlhdr->version != NBD_NL_VERSION) {
		fprintf(stderr, "cmd %d. Got header version %d. Supported %d.\n",
			cmd->c_id, info->genlhdr->version, NBD_NL_VERSION);
		return -EINVAL;
	}

	switch (cmd->c_id) {
	case NBD_CMD_CONNECT:
		return nbd_genl_map_done(info);
		break;
	case NBD_CMD_STATUS:
		return nbd_genl_list_devices(info);
		break;
	default:
		fprintf(stderr, "Unknown netlink command %d!\n", cmd->c_id);
		return -EOPNOTSUPP;
	}

	return -EINVAL;
}

static struct genl_cmd nbd_cmds[] = {
	{
		.c_id		= NBD_CMD_CONNECT,
		.c_name		= "MAP DEVICE",
		.c_msg_parser	= handle_netlink,
		.c_maxattr	= NBD_ATTR_MAX,
		.c_attr_policy	= nbd_attr_policy,
	},
	{
		.c_id		= NBD_CMD_STATUS,
		.c_name		= "LIST DEVICES",
		.c_msg_parser	= handle_netlink,
		.c_maxattr	= NBD_ATTR_MAX,
		.c_attr_policy	= nbd_attr_policy,
	},
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static struct genl_ops nbd_ops = {
	.o_name		= "nbd",
	.o_cmds		= nbd_cmds,
	.o_ncmds	= ARRAY_SIZE(nbd_cmds),
};

static struct nl_sock *
nbd_setup_netlink(void)
{
	struct nl_sock *sock;
	int ret;

	sock = nbd_socket_init();
	if (!sock)
		return NULL;

	ret = genl_register_family(&nbd_ops);
	if (ret < 0) {
		fprintf(stderr, "couldn't register family\n");
		goto err;
	}

	ret = genl_ops_resolve(sock, &nbd_ops);
	if (ret < 0) {
		fprintf(stderr, "couldn't resolve ops, is rbd.ko loaded?\n");
		goto err;
	}

	ret = genl_ctrl_resolve_grp(sock, "nbd", "nbd_mc_group");
	if (ret < 0) {
		fprintf(stderr, "couldn't resolve group, is nbd.ko loaded?\n");
		goto err;
	}

	ret = nl_socket_add_membership(sock, ret);
	if (ret < 0) {
		fprintf(stderr, "couldn't add membership\n");
		goto err;
	}

	return sock;

err:
	nl_socket_free(sock);
	return NULL;
}

static int
nbd_devices_query(void)
{
	struct nl_msg *msg;

	msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "Couldn't allocate netlink message, %s!\n",
			strerror(errno));
		goto nla_put_failure;
	}

	genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, driver_id, 0, 0,
		    NBD_CMD_STATUS, 0);
	/* -1 means list all the devices allocated(mapped and umapped) in kernel space */
	NLA_PUT_U32(msg, NBD_ATTR_INDEX, -1);


	if (nl_send_sync(sock, msg) < 0)
		fprintf(stderr, "Failed to setup device, check dmesg\n");

	return 0;

nla_put_failure:
	nl_socket_free(sock);
	return -1;
}

static int
nbd_device_connect(int sock_fd, __u64 size, __u32 blksize, int timeout,
		   int index)
{
	struct nlattr *sock_attr;
	struct nlattr *sock_opt;
	struct nl_msg *msg;
	int flags = 0;


	msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "Couldn't allocate netlink message, %s!\n",
			strerror(errno));
		goto nla_put_failure;
	}

	genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, driver_id, 0, 0,
		    NBD_CMD_CONNECT, 0);

	/* -1 means alloc the device dynamically */
	if (index < -1)
		index = -1;
	NLA_PUT_U32(msg, NBD_ATTR_INDEX, index);
	NLA_PUT_U64(msg, NBD_ATTR_SIZE_BYTES, size);
	NLA_PUT_U64(msg, NBD_ATTR_BLOCK_SIZE_BYTES, blksize);
	NLA_PUT_U64(msg, NBD_ATTR_SERVER_FLAGS, flags);
	if (timeout)
		NLA_PUT_U64(msg, NBD_ATTR_TIMEOUT, timeout);

	sock_attr = nla_nest_start(msg, NBD_ATTR_SOCKETS);
	if (!sock_attr) {
		fprintf(stderr, "Couldn't nest the socket!\n");
		goto nla_put_failure;
	}
	sock_opt = nla_nest_start(msg, NBD_SOCK_ITEM);
	if (!sock_opt) {
		fprintf(stderr, "Couldn't nest the socket item!\n");
		goto nla_put_failure;
	}

	NLA_PUT_U32(msg, NBD_SOCK_FD, sock_fd);
	nla_nest_end(msg, sock_opt);
	nla_nest_end(msg, sock_attr);

	if (nl_send_sync(sock, msg) < 0) {
		fprintf(stderr, "Failed to setup device, check dmesg!\n");
		goto nla_put_failure;
	}
	return 0;

nla_put_failure:
	return -1;
}

static int
nbd_client_init(char *volume, char *host, char *file, int timeout, int sock_fd,
		int index)
{
	struct stat st;
	__u64 size;
	__u32 blk_size;
	int ret = -1;
	struct glfs *glfs;

	if (!volume || !host || !file)
		return -1;

	sock = nbd_setup_netlink();
	if (!sock)
		return -1;

	glfs = nbd_volume_init(volume, host);
	if (!glfs) {
		ret = -1;
		goto err;
	}

	ret = glfs_lstat(glfs, file, &st);
	if (ret) {
		fprintf(stderr, "glfs_lstat failed!\n");
		ret = -1;
		goto err;
	}

	size = st.st_size;
	blk_size = st.st_blksize;

	return nbd_device_connect(sock_fd, size, blk_size, timeout, index);
err:
	glfs_fini(glfs);
	nl_socket_free(sock);
	return ret;
}

static int
umap_device(int index)
{
	struct nl_msg *msg;
	int ret = 0;

	msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "Couldn't allocate netlink message!\n");
		ret = -1;
		goto nla_put_failure;
	}

	genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, driver_id, 0, 0,
		    NBD_CMD_DISCONNECT, 0);
	NLA_PUT_U32(msg, NBD_ATTR_INDEX, index);
	if (nl_send_sync(sock, msg) < 0) {
		fprintf(stderr, "Failed to disconnect device, check dmsg\n");
		ret = -1;
	}

nla_put_failure:
	return ret;
}

GThreadPool *nbd_thread_pool;
#define NBD_MAX_THREAD_DEF 4
#define NBD_MAX_THREAD_MAX 16

static void
sighup_handler(const int sig) {
	return;
}

static void
sig_handler(const int sig) {
	if (sig == SIGINT || sig == SIGTERM)
		umap_device(nbd_device_index);
}

struct pool_request {
	__u32 magic;
	__u32 cmd;
	__u32 flags;
	__u64 offset;
	__u32 len;
	char handle[8];

	glfs_t *glfs;
	glfs_fd_t *gfd;
	int sock_fd;
	void *data;
};

static int nbd_socket_read(int fd, void *buf, size_t count)
{
	size_t cnt = 0;

	while (cnt < count) {
		ssize_t r = read(fd, buf, count - cnt);
		if (r <= 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			if (r == 0) {
				/* EOF */
				return cnt;
			}
			return -errno;
		}
		cnt += r;
		buf = (char *)buf + r;
	}
	return cnt;
}

static int nbd_socket_write(int fd, void *buf, size_t count)
{
	while (count > 0) {
		ssize_t r = write(fd, buf, count);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		count -= r;
		buf = (char *)buf + r;
	}
	return 0;
}

static void
glfs_async_cbk(glfs_fd_t *gfd, ssize_t ret, struct glfs_stat *prestat,
	       struct glfs_stat *poststat, void *data)
{
	struct pool_request *req = data;
	struct nbd_reply reply;

	reply.magic = htonl(NBD_REPLY_MAGIC);
	reply.error = htonl(ret < 0 ? ret : 0);
	memcpy(&(reply.handle), &(req->handle), sizeof(req->handle));

	pthread_spin_lock(&nbd_write_lock);
	nbd_socket_write(req->sock_fd, &reply, sizeof(struct nbd_reply));
	if(req->cmd == NBD_CMD_READ && !reply.error)
		nbd_socket_write(req->sock_fd, req->data, req->len);
	pthread_spin_unlock(&nbd_write_lock);

	free(req->data);
	free(req);
}

static void
handle_request(gpointer data, gpointer user_data)
{
	struct pool_request *req;

	if (!data)
		return;

	req = (struct pool_request*)data;

	switch (req->cmd) {
	case NBD_CMD_WRITE:
		glfs_pwrite_async(req->gfd, req->data, req->len, req->offset,
				  ALLOWED_BSOFLAGS, glfs_async_cbk, req);
		break;
	case NBD_CMD_READ:
		glfs_pread_async(req->gfd, req->data, req->len, req->offset,
				 SEEK_SET, glfs_async_cbk, req);
		break;
	case NBD_CMD_FLUSH:
		glfs_fdatasync_async(req->gfd, glfs_async_cbk, req);
		break;
	case NBD_CMD_TRIM:
		glfs_discard_async(req->gfd, req->offset, req->len,
				   glfs_async_cbk, req);
		break;
	default:
		fprintf(stderr,"Invalid request command\n");
		return;
	}
}

static int
nbd_server_init(char *volume, char *host, char *file, int threads, int sock_fd)
{
	struct pool_request *req;
	struct nbd_request request;
	int ret = -1;
	struct sigaction sa;
	glfs_fd_t *gfd = NULL;
	glfs_t *glfs = NULL;

	if (!volume || !host || !file)
		return -1;

	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGINT, &sa, NULL) == -1)
		fprintf(stderr, "sigaction: %m\n");

	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGTERM, &sa, NULL) == -1)
		fprintf(stderr, "sigaction: %m\n");

	sa.sa_handler = sighup_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGHUP, &sa, NULL) == -1)
		fprintf(stderr, "sigaction: %m\n");

	nbd_thread_pool = g_thread_pool_new(handle_request, NULL, threads,
					    false, NULL);
	if (!nbd_thread_pool) {
		fprintf(stderr, "Creating new thread pool failed!\n");
		return -1;
	}

	pthread_spin_init(&nbd_write_lock, 0);

	glfs = nbd_volume_init(volume, host);
	if (!glfs) {
		ret = -1;
		goto out;
	}

	gfd = glfs_open(glfs, file, ALLOWED_BSOFLAGS);
	if (!gfd) {
		fprintf(stderr, "Failed to open the file\n");
		ret = -1;
		goto out;
	}

	while (1) {
		memset(&request, 0, sizeof(struct nbd_request));
		ret = nbd_socket_read(sock_fd, &request,
				      sizeof(struct nbd_request));
		if (ret != sizeof(struct nbd_request)) {
			if (!ret)
				continue;
			ret = -1;
			goto out;
		}

		if (request.magic != htonl(NBD_REQUEST_MAGIC))
			fprintf(stderr, "invalid nbd request header!\n");

		if(request.type == htonl(NBD_CMD_DISC)) {
			fprintf(stderr, "Unmap request received!\n");
			ret = 0;
			goto out;
		}

		req = calloc(1, sizeof(struct pool_request));
		if (!req) {
			fprintf(stderr, "Failed to alloc memory for pool request!\n");
			ret = -1;
			goto out;
		}

		req->glfs = glfs;
		req->gfd = gfd;
		req->sock_fd = sock_fd;
		req->offset = be64toh(request.from);
		req->cmd = ntohl(request.type) & NBD_CMD_MASK_COMMAND;
		req->flags = ntohl(request.type) & ~NBD_CMD_MASK_COMMAND;
		req->len = ntohl(request.len);
		memcpy(&(req->handle), &(request.handle), sizeof(request.handle));
		req->data = NULL;

		if(req->cmd == NBD_CMD_READ || req->cmd == NBD_CMD_WRITE) {
			req->data = malloc(req->len);
			if (!req->data) {
				fprintf(stderr, "Failed to alloc memory for data!\n");
				free(req);
				ret = -1;
				goto out;
			}
		}

		if(req->cmd == NBD_CMD_WRITE)
			nbd_socket_read(sock_fd, req->data, req->len);

		g_thread_pool_push(nbd_thread_pool, req, NULL);
	}

out:
	glfs_close(gfd);
	glfs_fini(glfs);
	g_thread_pool_free(nbd_thread_pool, false, true);
	pthread_spin_destroy(&nbd_write_lock);
	return ret;
}

static int
nbd_map_device(int count, char **options)
{
	char *volume = NULL;
	char *host = NULL;
	char *file = NULL;
	int ret = 0;
	int sock_fd[2];
	bool daemonlize = true;
	int threads = NBD_MAX_THREAD_DEF;
	int timeout = 0;
	int dev_index = -1;
	int ind;

	ret = nbd_parse_volfile(options[2], &volume, &host, &file);
	if (ret) {
		ret = -1;
		goto out;
	}

	ind = 3;
	while (ind < count) {
		if (!strncmp("/dev/nbd", options[ind], strlen("/dev/nbd"))) {
			if (sscanf(options[ind], "/dev/nbd%d", &dev_index) != 1) {
				fprintf(stderr,
					"Invalid nbd-device, will alloc it by default!\n");

				dev_index = -1;
			}

			ind += 1;
		} else if (!strcmp("daemon", options[ind])) {
			if (!strcmp("on", options[ind + 1])) {
				daemonlize = true;
			} else if (!strcmp("off", options[ind + 1])) {
				daemonlize = false;
			} else {
				fprintf(stderr,
					"Invalid daemon, will set it as default off!\n");
				daemonlize = false;
			}

			ind += 2;
		} else if (!strcmp("threads", options[ind])) {
			threads = atoi(options[ind + 1]);
			if (threads <= 0) {
				fprintf(stderr,
					"Invalid threads, will set it as default %d!\n",
					NBD_MAX_THREAD_DEF);
				threads = NBD_MAX_THREAD_DEF;
			}

			if (threads > NBD_MAX_THREAD_MAX) {
				fprintf(stderr,
					"Currently the max threads is %d!\n",
					NBD_MAX_THREAD_MAX);
				threads = NBD_MAX_THREAD_MAX;
			}

			ind += 2;
		} else if (!strcmp("timeout", options[ind])) {
			timeout = atoi(options[ind + 1]);
			if (timeout < 0) {
				fprintf(stderr,
					"Invalid timeout, will set it as default 0!\n");
				timeout = 0;
			}
			ind += 2;
		} else {
			fprintf(stderr,
				"Invalid argument '%s'!\n", options[ind]);
			ret = -1;
			goto out;
		}
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock_fd) == -1) {
		fprintf(stderr, "socketpair failed, %s!\n",
			strerror(errno));
		ret = -1;
		goto out;
	}

	ret = nbd_client_init(volume, host, file, timeout, sock_fd[0],
			      dev_index);
	if (ret) {
		ret = -1;
		goto out;
	}

	if (daemonlize && daemon(0, 0) < 0) {
		fprintf(stderr, "Failed to daemonlize!\n");
		ret = -1;
		goto out;
	}

	ret = nbd_server_init(volume, host, file, threads, sock_fd[1]);
	if (ret) {
		ret = -1;
		goto out;
	}

	return 0;
out:
	free(volume);
	free(host);
	free(file);
	return ret;
}

static int
nbd_umap_device(int count, char **options)
{
	int index = -1;

	if (count != 3) {
		fprintf(stderr, "Invalid arguments for umap command!\n");
		return -1;
	}

	if (sscanf(options[2], "/dev/nbd%d", &index) != 1) {
		fprintf(stderr, "Invalid nbd device target!\n");
		return -1;
	}

	if (index < 0) {
		fprintf(stderr, "Invalid nbd device target!\n");
		return -1;
	}

	sock = nbd_setup_netlink();
	if (!sock)
		return -1;

	return umap_device(index);
}

static int
nbd_list_devices(int count, char **options)
{
	if (count != 3) {
		fprintf(stderr, "Invalid arguments for list command!\n");
		return -1;
	}

	if (!strcmp(options[2], "map")) {
		nbd_list_type = NBD_LIST_MAPPED;
	} else if (!strcmp(options[2], "umap")) {
		nbd_list_type = NBD_LIST_UNMAPPED;
	} else if (!strcmp(options[2], "all")) {
		nbd_list_type = NBD_LIST_ALL;
	} else {
		fprintf(stderr, "Invalid argument for list!\n");
		return -1;
	}

	sock = nbd_setup_netlink();
	if (!sock)
		return -1;

	return nbd_devices_query();
}

int
main(int argc, char *argv[])
{
	int ret = 0;
	nbd_command cmd;

	if (argc <= 1) {
		fprintf(stderr, "Too few options!\n\n" );
		usage();
		exit(EXIT_FAILURE);
	}

	cmd = nbd_command_lookup(argv[1]);
	if (cmd < 0) {
		fprintf(stderr, "Invalid command!\n\n" );
		usage();
		exit(EXIT_FAILURE);
	}

	switch(cmd) {
	case NBD_HELP:
		usage();
		exit(EXIT_SUCCESS);
	case NBD_CREATE:
		ret = nbd_create_file(argc, argv);
		break;
	case NBD_DELETE:
		ret = nbd_delete_file(argc, argv);
		break;
	case NBD_MAP:
		ret = nbd_map_device(argc, argv);
		break;
	case NBD_UNMAP:
		ret = nbd_umap_device(argc, argv);
		break;
	case NBD_LIST:
		ret = nbd_list_devices(argc, argv);
		break;
	case NBD_VERSION:
		printf("%s\n", version_info);
		break;
	case NBD_OPT_MAX:
	default:
		fprintf(stderr, "Invalid command!\n\n" );
		usage();
		exit(EXIT_FAILURE);
	}

	return ret;
}
