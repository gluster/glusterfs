/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _XDR_NFS3_H
#define _XDR_NFS3_H

#include <rpc/rpc.h>
#include <sys/types.h>

#define NFS3_FHSIZE             64
#define NFS3_COOKIEVERFSIZE     8
#define NFS3_CREATEVERFSIZE     8
#define NFS3_WRITEVERFSIZE      8

#define NFS3_ENTRY3_FIXED_SIZE  24
#define NFS3_POSTOPATTR_SIZE    88
#define NFS3_READDIR_RESOK_SIZE (NFS3_POSTOPATTR_SIZE + sizeof (bool_t) + NFS3_COOKIEVERFSIZE)

/* In size of post_op_fh3, the length of the file handle will have to be
 * included separately since we have variable length fh. Here we only account
 * for the field for handle_follows and for the file handle length field.
 */
#define NFS3_POSTOPFH3_FIXED_SIZE     (sizeof (bool_t) + sizeof (uint32_t))

/* Similarly, the size of the entry will have to include the variable length
 * file handle and the length of the entry name.
 */
#define NFS3_ENTRYP3_FIXED_SIZE  (NFS3_ENTRY3_FIXED_SIZE + NFS3_POSTOPATTR_SIZE + NFS3_POSTOPFH3_FIXED_SIZE)

typedef uint64_t uint64;
typedef int64_t int64;
typedef uint32_t uint32;
typedef int32_t int32;
typedef char *filename3;
typedef char *nfspath3;
typedef uint64 fileid3;
typedef uint64 cookie3;
typedef char cookieverf3[NFS3_COOKIEVERFSIZE];
typedef char createverf3[NFS3_CREATEVERFSIZE];
typedef char writeverf3[NFS3_WRITEVERFSIZE];
typedef uint32 uid3;
typedef uint32 gid3;
typedef uint64 size3;
typedef uint64 offset3;
typedef uint32 mode3;
typedef uint32 count3;

#define NFS3MODE_SETXUID        0x00800
#define NFS3MODE_SETXGID        0x00400
#define NFS3MODE_SAVESWAPTXT    0x00200
#define NFS3MODE_ROWNER         0x00100
#define NFS3MODE_WOWNER         0x00080
#define NFS3MODE_XOWNER         0x00040
#define NFS3MODE_RGROUP         0x00020
#define NFS3MODE_WGROUP         0x00010
#define NFS3MODE_XGROUP         0x00008
#define NFS3MODE_ROTHER         0x00004
#define NFS3MODE_WOTHER         0x00002
#define NFS3MODE_XOTHER         0x00001

enum nfsstat3 {
	NFS3_OK = 0,
	NFS3ERR_PERM = 1,
	NFS3ERR_NOENT = 2,
	NFS3ERR_IO = 5,
	NFS3ERR_NXIO = 6,
	NFS3ERR_ACCES = 13,
	NFS3ERR_EXIST = 17,
	NFS3ERR_XDEV = 18,
	NFS3ERR_NODEV = 19,
	NFS3ERR_NOTDIR = 20,
	NFS3ERR_ISDIR = 21,
	NFS3ERR_INVAL = 22,
	NFS3ERR_FBIG = 27,
	NFS3ERR_NOSPC = 28,
	NFS3ERR_ROFS = 30,
	NFS3ERR_MLINK = 31,
	NFS3ERR_NAMETOOLONG = 63,
	NFS3ERR_NOTEMPTY = 66,
	NFS3ERR_DQUOT = 69,
	NFS3ERR_STALE = 70,
	NFS3ERR_REMOTE = 71,
	NFS3ERR_BADHANDLE = 10001,
	NFS3ERR_NOT_SYNC = 10002,
	NFS3ERR_BAD_COOKIE = 10003,
	NFS3ERR_NOTSUPP = 10004,
	NFS3ERR_TOOSMALL = 10005,
	NFS3ERR_SERVERFAULT = 10006,
	NFS3ERR_BADTYPE = 10007,
	NFS3ERR_JUKEBOX = 10008,
	NFS3ERR_END_OF_LIST = -1,
};
typedef enum nfsstat3 nfsstat3;

enum ftype3 {
	NF3REG = 1,
	NF3DIR = 2,
	NF3BLK = 3,
	NF3CHR = 4,
	NF3LNK = 5,
	NF3SOCK = 6,
	NF3FIFO = 7,
};
typedef enum ftype3 ftype3;

struct specdata3 {
	uint32 specdata1;
	uint32 specdata2;
};
typedef struct specdata3 specdata3;

struct nfs_fh3 {
	struct {
		u_int data_len;
		char *data_val;
	} data;
};
typedef struct nfs_fh3 nfs_fh3;

struct nfstime3 {
	uint32 seconds;
	uint32 nseconds;
};
typedef struct nfstime3 nfstime3;

struct fattr3 {
	ftype3 type;
	mode3 mode;
	uint32 nlink;
	uid3 uid;
	gid3 gid;
	size3 size;
	size3 used;
	specdata3 rdev;
	uint64 fsid;
	fileid3 fileid;
	nfstime3 atime;
	nfstime3 mtime;
	nfstime3 ctime;
};
typedef struct fattr3 fattr3;

struct post_op_attr {
	bool_t attributes_follow;
	union {
		fattr3 attributes;
	} post_op_attr_u;
};
typedef struct post_op_attr post_op_attr;

struct wcc_attr {
	size3 size;
	nfstime3 mtime;
	nfstime3 ctime;
};
typedef struct wcc_attr wcc_attr;

struct pre_op_attr {
	bool_t attributes_follow;
	union {
		wcc_attr attributes;
	} pre_op_attr_u;
};
typedef struct pre_op_attr pre_op_attr;

struct wcc_data {
	pre_op_attr before;
	post_op_attr after;
};
typedef struct wcc_data wcc_data;

struct post_op_fh3 {
	bool_t handle_follows;
	union {
		nfs_fh3 handle;
	} post_op_fh3_u;
};
typedef struct post_op_fh3 post_op_fh3;

enum time_how {
	DONT_CHANGE = 0,
	SET_TO_SERVER_TIME = 1,
	SET_TO_CLIENT_TIME = 2,
};
typedef enum time_how time_how;

struct set_mode3 {
	bool_t set_it;
	union {
		mode3 mode;
	} set_mode3_u;
};
typedef struct set_mode3 set_mode3;

struct set_uid3 {
	bool_t set_it;
	union {
		uid3 uid;
	} set_uid3_u;
};
typedef struct set_uid3 set_uid3;

struct set_gid3 {
	bool_t set_it;
	union {
		gid3 gid;
	} set_gid3_u;
};
typedef struct set_gid3 set_gid3;

struct set_size3 {
	bool_t set_it;
	union {
		size3 size;
	} set_size3_u;
};
typedef struct set_size3 set_size3;

struct set_atime {
	time_how set_it;
	union {
		nfstime3 atime;
	} set_atime_u;
};
typedef struct set_atime set_atime;

struct set_mtime {
	time_how set_it;
	union {
		nfstime3 mtime;
	} set_mtime_u;
};
typedef struct set_mtime set_mtime;

struct sattr3 {
	set_mode3 mode;
	set_uid3 uid;
	set_gid3 gid;
	set_size3 size;
	set_atime atime;
	set_mtime mtime;
};
typedef struct sattr3 sattr3;

struct diropargs3 {
	nfs_fh3 dir;
	filename3 name;
};
typedef struct diropargs3 diropargs3;

struct getattr3args {
	nfs_fh3 object;
};
typedef struct getattr3args getattr3args;

struct getattr3resok {
	fattr3 obj_attributes;
};
typedef struct getattr3resok getattr3resok;

struct getattr3res {
	nfsstat3 status;
	union {
		getattr3resok resok;
	} getattr3res_u;
};
typedef struct getattr3res getattr3res;

struct sattrguard3 {
	bool_t check;
	union {
		nfstime3 obj_ctime;
	} sattrguard3_u;
};
typedef struct sattrguard3 sattrguard3;

struct setattr3args {
	nfs_fh3 object;
	sattr3 new_attributes;
	sattrguard3 guard;
};
typedef struct setattr3args setattr3args;

struct setattr3resok {
	wcc_data obj_wcc;
};
typedef struct setattr3resok setattr3resok;

struct setattr3resfail {
	wcc_data obj_wcc;
};
typedef struct setattr3resfail setattr3resfail;

struct setattr3res {
	nfsstat3 status;
	union {
		setattr3resok resok;
		setattr3resfail resfail;
	} setattr3res_u;
};
typedef struct setattr3res setattr3res;

struct lookup3args {
	diropargs3 what;
};
typedef struct lookup3args lookup3args;

struct lookup3resok {
	nfs_fh3 object;
	post_op_attr obj_attributes;
	post_op_attr dir_attributes;
};
typedef struct lookup3resok lookup3resok;

struct lookup3resfail {
	post_op_attr dir_attributes;
};
typedef struct lookup3resfail lookup3resfail;

struct lookup3res {
	nfsstat3 status;
	union {
		lookup3resok resok;
		lookup3resfail resfail;
	} lookup3res_u;
};
typedef struct lookup3res lookup3res;
#define ACCESS3_READ 0x0001
#define ACCESS3_LOOKUP 0x0002
#define ACCESS3_MODIFY 0x0004
#define ACCESS3_EXTEND 0x0008
#define ACCESS3_DELETE 0x0010
#define ACCESS3_EXECUTE 0x0020

struct access3args {
	nfs_fh3 object;
	uint32 access;
};
typedef struct access3args access3args;

struct access3resok {
	post_op_attr obj_attributes;
	uint32 access;
};
typedef struct access3resok access3resok;

struct access3resfail {
	post_op_attr obj_attributes;
};
typedef struct access3resfail access3resfail;

struct access3res {
	nfsstat3 status;
	union {
		access3resok resok;
		access3resfail resfail;
	} access3res_u;
};
typedef struct access3res access3res;

struct readlink3args {
	nfs_fh3 symlink;
};
typedef struct readlink3args readlink3args;

struct readlink3resok {
	post_op_attr symlink_attributes;
	nfspath3 data;
};
typedef struct readlink3resok readlink3resok;

struct readlink3resfail {
	post_op_attr symlink_attributes;
};
typedef struct readlink3resfail readlink3resfail;

struct readlink3res {
	nfsstat3 status;
	union {
		readlink3resok resok;
		readlink3resfail resfail;
	} readlink3res_u;
};
typedef struct readlink3res readlink3res;

struct read3args {
	nfs_fh3 file;
	offset3 offset;
	count3 count;
};
typedef struct read3args read3args;

struct read3resok {
	post_op_attr file_attributes;
	count3 count;
	bool_t eof;
	struct {
		u_int data_len;
		char *data_val;
	} data;
};
typedef struct read3resok read3resok;

struct read3resfail {
	post_op_attr file_attributes;
};
typedef struct read3resfail read3resfail;

struct read3res {
	nfsstat3 status;
	union {
		read3resok resok;
		read3resfail resfail;
	} read3res_u;
};
typedef struct read3res read3res;

enum stable_how {
	UNSTABLE = 0,
	DATA_SYNC = 1,
	FILE_SYNC = 2,
};
typedef enum stable_how stable_how;

struct write3args {
	nfs_fh3 file;
	offset3 offset;
	count3 count;
	stable_how stable;
	struct {
		u_int data_len;
		char *data_val;
	} data;
};
typedef struct write3args write3args;

/* Generally, the protocol allows the file handle to be less than 64 bytes but
 * our server does not return file handles less than 64b so we can safely say
 * sizeof (nfs_fh3) rather than first trying to extract the fh size of the
 * network followed by a sized-read of the file handle.
 */
#define NFS3_WRITE3ARGS_SIZE    (sizeof (uint32_t) + NFS3_FHSIZE + sizeof (offset3) + sizeof (count3) + sizeof (uint32_t))
struct write3resok {
	wcc_data file_wcc;
	count3 count;
	stable_how committed;
	writeverf3 verf;
};
typedef struct write3resok write3resok;

struct write3resfail {
	wcc_data file_wcc;
};
typedef struct write3resfail write3resfail;

struct write3res {
	nfsstat3 status;
	union {
		write3resok resok;
		write3resfail resfail;
	} write3res_u;
};
typedef struct write3res write3res;

enum createmode3 {
	UNCHECKED = 0,
	GUARDED = 1,
	EXCLUSIVE = 2,
};
typedef enum createmode3 createmode3;

struct createhow3 {
	createmode3 mode;
	union {
		sattr3 obj_attributes;
		createverf3 verf;
	} createhow3_u;
};
typedef struct createhow3 createhow3;

struct create3args {
	diropargs3 where;
	createhow3 how;
};
typedef struct create3args create3args;

struct create3resok {
	post_op_fh3 obj;
	post_op_attr obj_attributes;
	wcc_data dir_wcc;
};
typedef struct create3resok create3resok;

struct create3resfail {
	wcc_data dir_wcc;
};
typedef struct create3resfail create3resfail;

struct create3res {
	nfsstat3 status;
	union {
		create3resok resok;
		create3resfail resfail;
	} create3res_u;
};
typedef struct create3res create3res;

struct mkdir3args {
	diropargs3 where;
	sattr3 attributes;
};
typedef struct mkdir3args mkdir3args;

struct mkdir3resok {
	post_op_fh3 obj;
	post_op_attr obj_attributes;
	wcc_data dir_wcc;
};
typedef struct mkdir3resok mkdir3resok;

struct mkdir3resfail {
	wcc_data dir_wcc;
};
typedef struct mkdir3resfail mkdir3resfail;

struct mkdir3res {
	nfsstat3 status;
	union {
		mkdir3resok resok;
		mkdir3resfail resfail;
	} mkdir3res_u;
};
typedef struct mkdir3res mkdir3res;

struct symlinkdata3 {
	sattr3 symlink_attributes;
	nfspath3 symlink_data;
};
typedef struct symlinkdata3 symlinkdata3;

struct symlink3args {
	diropargs3 where;
	symlinkdata3 symlink;
};
typedef struct symlink3args symlink3args;

struct symlink3resok {
	post_op_fh3 obj;
	post_op_attr obj_attributes;
	wcc_data dir_wcc;
};
typedef struct symlink3resok symlink3resok;

struct symlink3resfail {
	wcc_data dir_wcc;
};
typedef struct symlink3resfail symlink3resfail;

struct symlink3res {
	nfsstat3 status;
	union {
		symlink3resok resok;
		symlink3resfail resfail;
	} symlink3res_u;
};
typedef struct symlink3res symlink3res;

struct devicedata3 {
	sattr3 dev_attributes;
	specdata3 spec;
};
typedef struct devicedata3 devicedata3;

struct mknoddata3 {
	ftype3 type;
	union {
		devicedata3 device;
		sattr3 pipe_attributes;
	} mknoddata3_u;
};
typedef struct mknoddata3 mknoddata3;

struct mknod3args {
	diropargs3 where;
	mknoddata3 what;
};
typedef struct mknod3args mknod3args;

struct mknod3resok {
	post_op_fh3 obj;
	post_op_attr obj_attributes;
	wcc_data dir_wcc;
};
typedef struct mknod3resok mknod3resok;

struct mknod3resfail {
	wcc_data dir_wcc;
};
typedef struct mknod3resfail mknod3resfail;

struct mknod3res {
	nfsstat3 status;
	union {
		mknod3resok resok;
		mknod3resfail resfail;
	} mknod3res_u;
};
typedef struct mknod3res mknod3res;

struct remove3args {
	diropargs3 object;
};
typedef struct remove3args remove3args;

struct remove3resok {
	wcc_data dir_wcc;
};
typedef struct remove3resok remove3resok;

struct remove3resfail {
	wcc_data dir_wcc;
};
typedef struct remove3resfail remove3resfail;

struct remove3res {
	nfsstat3 status;
	union {
		remove3resok resok;
		remove3resfail resfail;
	} remove3res_u;
};
typedef struct remove3res remove3res;

struct rmdir3args {
	diropargs3 object;
};
typedef struct rmdir3args rmdir3args;

struct rmdir3resok {
	wcc_data dir_wcc;
};
typedef struct rmdir3resok rmdir3resok;

struct rmdir3resfail {
	wcc_data dir_wcc;
};
typedef struct rmdir3resfail rmdir3resfail;

struct rmdir3res {
	nfsstat3 status;
	union {
		rmdir3resok resok;
		rmdir3resfail resfail;
	} rmdir3res_u;
};
typedef struct rmdir3res rmdir3res;

struct rename3args {
	diropargs3 from;
	diropargs3 to;
};
typedef struct rename3args rename3args;

struct rename3resok {
	wcc_data fromdir_wcc;
	wcc_data todir_wcc;
};
typedef struct rename3resok rename3resok;

struct rename3resfail {
	wcc_data fromdir_wcc;
	wcc_data todir_wcc;
};
typedef struct rename3resfail rename3resfail;

struct rename3res {
	nfsstat3 status;
	union {
		rename3resok resok;
		rename3resfail resfail;
	} rename3res_u;
};
typedef struct rename3res rename3res;

struct link3args {
	nfs_fh3 file;
	diropargs3 link;
};
typedef struct link3args link3args;

struct link3resok {
	post_op_attr file_attributes;
	wcc_data linkdir_wcc;
};
typedef struct link3resok link3resok;

struct link3resfail {
	post_op_attr file_attributes;
	wcc_data linkdir_wcc;
};
typedef struct link3resfail link3resfail;

struct link3res {
	nfsstat3 status;
	union {
		link3resok resok;
		link3resfail resfail;
	} link3res_u;
};
typedef struct link3res link3res;

struct readdir3args {
	nfs_fh3 dir;
	cookie3 cookie;
	cookieverf3 cookieverf;
	count3 count;
};
typedef struct readdir3args readdir3args;

struct entry3 {
	fileid3 fileid;
	filename3 name;
	cookie3 cookie;
	struct entry3 *nextentry;
};
typedef struct entry3 entry3;

struct dirlist3 {
	entry3 *entries;
	bool_t eof;
};
typedef struct dirlist3 dirlist3;

struct readdir3resok {
	post_op_attr dir_attributes;
	cookieverf3 cookieverf;
	dirlist3 reply;
};
typedef struct readdir3resok readdir3resok;

struct readdir3resfail {
	post_op_attr dir_attributes;
};
typedef struct readdir3resfail readdir3resfail;

struct readdir3res {
	nfsstat3 status;
	union {
		readdir3resok resok;
		readdir3resfail resfail;
	} readdir3res_u;
};
typedef struct readdir3res readdir3res;

struct readdirp3args {
	nfs_fh3 dir;
	cookie3 cookie;
	cookieverf3 cookieverf;
	count3 dircount;
	count3 maxcount;
};
typedef struct readdirp3args readdirp3args;

struct entryp3 {
	fileid3 fileid;
	filename3 name;
	cookie3 cookie;
	post_op_attr name_attributes;
	post_op_fh3 name_handle;
	struct entryp3 *nextentry;
};
typedef struct entryp3 entryp3;

struct dirlistp3 {
	entryp3 *entries;
	bool_t eof;
};
typedef struct dirlistp3 dirlistp3;

struct readdirp3resok {
	post_op_attr dir_attributes;
	cookieverf3 cookieverf;
	dirlistp3   reply;
};
typedef struct readdirp3resok readdirp3resok;

struct readdirp3resfail {
	post_op_attr dir_attributes;
};
typedef struct readdirp3resfail readdirp3resfail;

struct readdirp3res {
	nfsstat3 status;
	union {
		readdirp3resok resok;
		readdirp3resfail resfail;
	} readdirp3res_u;
};
typedef struct readdirp3res readdirp3res;

struct fsstat3args {
	nfs_fh3 fsroot;
};
typedef struct fsstat3args fsstat3args;

struct fsstat3resok {
	post_op_attr obj_attributes;
	size3 tbytes;
	size3 fbytes;
	size3 abytes;
	size3 tfiles;
	size3 ffiles;
	size3 afiles;
	uint32 invarsec;
};
typedef struct fsstat3resok fsstat3resok;

struct fsstat3resfail {
	post_op_attr obj_attributes;
};
typedef struct fsstat3resfail fsstat3resfail;

struct fsstat3res {
	nfsstat3 status;
	union {
		fsstat3resok resok;
		fsstat3resfail resfail;
	} fsstat3res_u;
};
typedef struct fsstat3res fsstat3res;
#define FSF3_LINK 0x0001
#define FSF3_SYMLINK 0x0002
#define FSF3_HOMOGENEOUS 0x0008
#define FSF3_CANSETTIME 0x0010

struct fsinfo3args {
	nfs_fh3 fsroot;
};
typedef struct fsinfo3args fsinfo3args;

struct fsinfo3resok {
	post_op_attr obj_attributes;
	uint32 rtmax;
	uint32 rtpref;
	uint32 rtmult;
	uint32 wtmax;
	uint32 wtpref;
	uint32 wtmult;
	uint32 dtpref;
	size3 maxfilesize;
	nfstime3 time_delta;
	uint32 properties;
};
typedef struct fsinfo3resok fsinfo3resok;

struct fsinfo3resfail {
	post_op_attr obj_attributes;
};
typedef struct fsinfo3resfail fsinfo3resfail;

struct fsinfo3res {
	nfsstat3 status;
	union {
		fsinfo3resok resok;
		fsinfo3resfail resfail;
	} fsinfo3res_u;
};
typedef struct fsinfo3res fsinfo3res;

struct pathconf3args {
	nfs_fh3 object;
};
typedef struct pathconf3args pathconf3args;

struct pathconf3resok {
	post_op_attr obj_attributes;
	uint32 linkmax;
	uint32 name_max;
	bool_t no_trunc;
	bool_t chown_restricted;
	bool_t case_insensitive;
	bool_t case_preserving;
};
typedef struct pathconf3resok pathconf3resok;

struct pathconf3resfail {
	post_op_attr obj_attributes;
};
typedef struct pathconf3resfail pathconf3resfail;

struct pathconf3res {
	nfsstat3 status;
	union {
		pathconf3resok resok;
		pathconf3resfail resfail;
	} pathconf3res_u;
};
typedef struct pathconf3res pathconf3res;

struct commit3args {
	nfs_fh3 file;
	offset3 offset;
	count3 count;
};
typedef struct commit3args commit3args;

struct commit3resok {
	wcc_data file_wcc;
	writeverf3 verf;
};
typedef struct commit3resok commit3resok;

struct commit3resfail {
	wcc_data file_wcc;
};
typedef struct commit3resfail commit3resfail;

struct commit3res {
	nfsstat3 status;
	union {
		commit3resok resok;
		commit3resfail resfail;
	} commit3res_u;
};
typedef struct commit3res commit3res;
#define MNTPATHLEN 1024
#define MNTNAMLEN 255
#define FHSIZE3 NFS3_FHSIZE

typedef struct {
	u_int fhandle3_len;
	char *fhandle3_val;
} fhandle3;

typedef char *dirpath;

typedef char *name;

enum mountstat3 {
	MNT3_OK = 0,
	MNT3ERR_PERM = 1,
	MNT3ERR_NOENT = 2,
	MNT3ERR_IO = 5,
	MNT3ERR_ACCES = 13,
	MNT3ERR_NOTDIR = 20,
	MNT3ERR_INVAL = 22,
	MNT3ERR_NAMETOOLONG = 63,
	MNT3ERR_NOTSUPP = 10004,
	MNT3ERR_SERVERFAULT = 10006,
};
typedef enum mountstat3 mountstat3;

struct mountres3_ok {
	fhandle3 fhandle;
	struct {
		u_int auth_flavors_len;
		int *auth_flavors_val;
	} auth_flavors;
};
typedef struct mountres3_ok mountres3_ok;

struct mountres3 {
	mountstat3 fhs_status;
	union {
		mountres3_ok mountinfo;
	} mountres3_u;
};
typedef struct mountres3 mountres3;

typedef struct mountbody *mountlist;

struct mountbody {
	name ml_hostname;
	dirpath ml_directory;
	mountlist ml_next;
};
typedef struct mountbody mountbody;

typedef struct groupnode *groups;

struct groupnode {
	name gr_name;
	groups gr_next;
};
typedef struct groupnode groupnode;

typedef struct exportnode *exports;

struct exportnode {
	dirpath ex_dir;
	groups ex_groups;
	exports ex_next;
};
typedef struct exportnode exportnode;

#define NFS_PROGRAM             100003
#define NFS_V3                  3

#define NFS3_NULL               0
#define NFS3_GETATTR            1
#define NFS3_SETATTR            2
#define NFS3_LOOKUP             3
#define NFS3_ACCESS             4
#define NFS3_READLINK           5
#define NFS3_READ               6
#define NFS3_WRITE              7
#define NFS3_CREATE             8
#define NFS3_MKDIR              9
#define NFS3_SYMLINK            10
#define NFS3_MKNOD              11
#define NFS3_REMOVE             12
#define NFS3_RMDIR              13
#define NFS3_RENAME             14
#define NFS3_LINK               15
#define NFS3_READDIR            16
#define NFS3_READDIRP           17
#define NFS3_FSSTAT             18
#define NFS3_FSINFO             19
#define NFS3_PATHCONF           20
#define NFS3_COMMIT             21
#define NFS3_PROC_COUNT         22

#define MOUNT_PROGRAM           100005
#define MOUNT_V3                3
#define MOUNT_V1                1

#define MOUNT3_NULL             0
#define MOUNT3_MNT              1
#define MOUNT3_DUMP             2
#define MOUNT3_UMNT             3
#define MOUNT3_UMNTALL          4
#define MOUNT3_EXPORT           5
#define MOUNT3_PROC_COUNT       6

#define MOUNT1_NULL             0
#define MOUNT1_MNT              1
#define MOUNT1_DUMP             2
#define MOUNT1_UMNT             3
#define MOUNT1_UMNTALL          4
#define MOUNT1_EXPORT           5
#define MOUNT1_PROC_COUNT       6
/* the xdr functions */

extern  bool_t xdr_uint64 (XDR *, uint64*);
extern  bool_t xdr_int64 (XDR *, int64*);
extern  bool_t xdr_uint32 (XDR *, uint32*);
extern  bool_t xdr_int32 (XDR *, int32*);
extern  bool_t xdr_filename3 (XDR *, filename3*);
extern  bool_t xdr_nfspath3 (XDR *, nfspath3*);
extern  bool_t xdr_fileid3 (XDR *, fileid3*);
extern  bool_t xdr_cookie3 (XDR *, cookie3*);
extern  bool_t xdr_cookieverf3 (XDR *, cookieverf3);
extern  bool_t xdr_createverf3 (XDR *, createverf3);
extern  bool_t xdr_writeverf3 (XDR *, writeverf3);
extern  bool_t xdr_uid3 (XDR *, uid3*);
extern  bool_t xdr_gid3 (XDR *, gid3*);
extern  bool_t xdr_size3 (XDR *, size3*);
extern  bool_t xdr_offset3 (XDR *, offset3*);
extern  bool_t xdr_mode3 (XDR *, mode3*);
extern  bool_t xdr_count3 (XDR *, count3*);
extern  bool_t xdr_nfsstat3 (XDR *, nfsstat3*);
extern  bool_t xdr_ftype3 (XDR *, ftype3*);
extern  bool_t xdr_specdata3 (XDR *, specdata3*);
extern  bool_t xdr_nfs_fh3 (XDR *, nfs_fh3*);
extern  bool_t xdr_nfstime3 (XDR *, nfstime3*);
extern  bool_t xdr_fattr3 (XDR *, fattr3*);
extern  bool_t xdr_post_op_attr (XDR *, post_op_attr*);
extern  bool_t xdr_wcc_attr (XDR *, wcc_attr*);
extern  bool_t xdr_pre_op_attr (XDR *, pre_op_attr*);
extern  bool_t xdr_wcc_data (XDR *, wcc_data*);
extern  bool_t xdr_post_op_fh3 (XDR *, post_op_fh3*);
extern  bool_t xdr_time_how (XDR *, time_how*);
extern  bool_t xdr_set_mode3 (XDR *, set_mode3*);
extern  bool_t xdr_set_uid3 (XDR *, set_uid3*);
extern  bool_t xdr_set_gid3 (XDR *, set_gid3*);
extern  bool_t xdr_set_size3 (XDR *, set_size3*);
extern  bool_t xdr_set_atime (XDR *, set_atime*);
extern  bool_t xdr_set_mtime (XDR *, set_mtime*);
extern  bool_t xdr_sattr3 (XDR *, sattr3*);
extern  bool_t xdr_diropargs3 (XDR *, diropargs3*);
extern  bool_t xdr_getattr3args (XDR *, getattr3args*);
extern  bool_t xdr_getattr3resok (XDR *, getattr3resok*);
extern  bool_t xdr_getattr3res (XDR *, getattr3res*);
extern  bool_t xdr_sattrguard3 (XDR *, sattrguard3*);
extern  bool_t xdr_setattr3args (XDR *, setattr3args*);
extern  bool_t xdr_setattr3resok (XDR *, setattr3resok*);
extern  bool_t xdr_setattr3resfail (XDR *, setattr3resfail*);
extern  bool_t xdr_setattr3res (XDR *, setattr3res*);
extern  bool_t xdr_lookup3args (XDR *, lookup3args*);
extern  bool_t xdr_lookup3resok (XDR *, lookup3resok*);
extern  bool_t xdr_lookup3resfail (XDR *, lookup3resfail*);
extern  bool_t xdr_lookup3res (XDR *, lookup3res*);
extern  bool_t xdr_access3args (XDR *, access3args*);
extern  bool_t xdr_access3resok (XDR *, access3resok*);
extern  bool_t xdr_access3resfail (XDR *, access3resfail*);
extern  bool_t xdr_access3res (XDR *, access3res*);
extern  bool_t xdr_readlink3args (XDR *, readlink3args*);
extern  bool_t xdr_readlink3resok (XDR *, readlink3resok*);
extern  bool_t xdr_readlink3resfail (XDR *, readlink3resfail*);
extern  bool_t xdr_readlink3res (XDR *, readlink3res*);
extern  bool_t xdr_read3args (XDR *, read3args*);
extern  bool_t xdr_read3resok (XDR *, read3resok*);
extern  bool_t xdr_read3resfail (XDR *, read3resfail*);
extern  bool_t xdr_read3res (XDR *, read3res*);
extern  bool_t xdr_read3res_nocopy (XDR *xdrs, read3res *objp);
extern  bool_t xdr_stable_how (XDR *, stable_how*);
extern  bool_t xdr_write3args (XDR *, write3args*);
extern  bool_t xdr_write3resok (XDR *, write3resok*);
extern  bool_t xdr_write3resfail (XDR *, write3resfail*);
extern  bool_t xdr_write3res (XDR *, write3res*);
extern  bool_t xdr_createmode3 (XDR *, createmode3*);
extern  bool_t xdr_createhow3 (XDR *, createhow3*);
extern  bool_t xdr_create3args (XDR *, create3args*);
extern  bool_t xdr_create3resok (XDR *, create3resok*);
extern  bool_t xdr_create3resfail (XDR *, create3resfail*);
extern  bool_t xdr_create3res (XDR *, create3res*);
extern  bool_t xdr_mkdir3args (XDR *, mkdir3args*);
extern  bool_t xdr_mkdir3resok (XDR *, mkdir3resok*);
extern  bool_t xdr_mkdir3resfail (XDR *, mkdir3resfail*);
extern  bool_t xdr_mkdir3res (XDR *, mkdir3res*);
extern  bool_t xdr_symlinkdata3 (XDR *, symlinkdata3*);
extern  bool_t xdr_symlink3args (XDR *, symlink3args*);
extern  bool_t xdr_symlink3resok (XDR *, symlink3resok*);
extern  bool_t xdr_symlink3resfail (XDR *, symlink3resfail*);
extern  bool_t xdr_symlink3res (XDR *, symlink3res*);
extern  bool_t xdr_devicedata3 (XDR *, devicedata3*);
extern  bool_t xdr_mknoddata3 (XDR *, mknoddata3*);
extern  bool_t xdr_mknod3args (XDR *, mknod3args*);
extern  bool_t xdr_mknod3resok (XDR *, mknod3resok*);
extern  bool_t xdr_mknod3resfail (XDR *, mknod3resfail*);
extern  bool_t xdr_mknod3res (XDR *, mknod3res*);
extern  bool_t xdr_remove3args (XDR *, remove3args*);
extern  bool_t xdr_remove3resok (XDR *, remove3resok*);
extern  bool_t xdr_remove3resfail (XDR *, remove3resfail*);
extern  bool_t xdr_remove3res (XDR *, remove3res*);
extern  bool_t xdr_rmdir3args (XDR *, rmdir3args*);
extern  bool_t xdr_rmdir3resok (XDR *, rmdir3resok*);
extern  bool_t xdr_rmdir3resfail (XDR *, rmdir3resfail*);
extern  bool_t xdr_rmdir3res (XDR *, rmdir3res*);
extern  bool_t xdr_rename3args (XDR *, rename3args*);
extern  bool_t xdr_rename3resok (XDR *, rename3resok*);
extern  bool_t xdr_rename3resfail (XDR *, rename3resfail*);
extern  bool_t xdr_rename3res (XDR *, rename3res*);
extern  bool_t xdr_link3args (XDR *, link3args*);
extern  bool_t xdr_link3resok (XDR *, link3resok*);
extern  bool_t xdr_link3resfail (XDR *, link3resfail*);
extern  bool_t xdr_link3res (XDR *, link3res*);
extern  bool_t xdr_readdir3args (XDR *, readdir3args*);
extern  bool_t xdr_entry3 (XDR *, entry3*);
extern  bool_t xdr_dirlist3 (XDR *, dirlist3*);
extern  bool_t xdr_readdir3resok (XDR *, readdir3resok*);
extern  bool_t xdr_readdir3resfail (XDR *, readdir3resfail*);
extern  bool_t xdr_readdir3res (XDR *, readdir3res*);
extern  bool_t xdr_readdirp3args (XDR *, readdirp3args*);
extern  bool_t xdr_entryp3 (XDR *, entryp3*);
extern  bool_t xdr_dirlistp3 (XDR *, dirlistp3*);
extern  bool_t xdr_readdirp3resok (XDR *, readdirp3resok*);
extern  bool_t xdr_readdirp3resfail (XDR *, readdirp3resfail*);
extern  bool_t xdr_readdirp3res (XDR *, readdirp3res*);
extern  bool_t xdr_fsstat3args (XDR *, fsstat3args*);
extern  bool_t xdr_fsstat3resok (XDR *, fsstat3resok*);
extern  bool_t xdr_fsstat3resfail (XDR *, fsstat3resfail*);
extern  bool_t xdr_fsstat3res (XDR *, fsstat3res*);
extern  bool_t xdr_fsinfo3args (XDR *, fsinfo3args*);
extern  bool_t xdr_fsinfo3resok (XDR *, fsinfo3resok*);
extern  bool_t xdr_fsinfo3resfail (XDR *, fsinfo3resfail*);
extern  bool_t xdr_fsinfo3res (XDR *, fsinfo3res*);
extern  bool_t xdr_pathconf3args (XDR *, pathconf3args*);
extern  bool_t xdr_pathconf3resok (XDR *, pathconf3resok*);
extern  bool_t xdr_pathconf3resfail (XDR *, pathconf3resfail*);
extern  bool_t xdr_pathconf3res (XDR *, pathconf3res*);
extern  bool_t xdr_commit3args (XDR *, commit3args*);
extern  bool_t xdr_commit3resok (XDR *, commit3resok*);
extern  bool_t xdr_commit3resfail (XDR *, commit3resfail*);
extern  bool_t xdr_commit3res (XDR *, commit3res*);
extern  bool_t xdr_fhandle3 (XDR *, fhandle3*);
extern  bool_t xdr_dirpath (XDR *, dirpath*);
extern  bool_t xdr_name (XDR *, name*);
extern  bool_t xdr_mountstat3 (XDR *, mountstat3*);
extern  bool_t xdr_mountres3_ok (XDR *, mountres3_ok*);
extern  bool_t xdr_mountres3 (XDR *, mountres3*);
extern  bool_t xdr_mountlist (XDR *, mountlist*);
extern  bool_t xdr_mountbody (XDR *, mountbody*);
extern  bool_t xdr_groups (XDR *, groups*);
extern  bool_t xdr_groupnode (XDR *, groupnode*);
extern  bool_t xdr_exports (XDR *, exports*);
extern  bool_t xdr_exportnode (XDR *, exportnode*);

extern void xdr_free_exports_list (struct exportnode *first);
extern void xdr_free_mountlist (mountlist ml);

extern void xdr_free_write3args_nocopy (write3args *wa);
#endif
