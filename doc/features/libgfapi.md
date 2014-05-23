One of the known methods to access glusterfs is via fuse module. However, it has some overhead or performance issues because of the number of context switches which need to be performed to complete one i/o transaction[1].


To over come this limitation, a new method called ‘libgfapi’ is introduced. libgfapi support is available from GlusterFS-3.4 release.

libgfapi is a userspace library for accessing data in glusterfs. libgfapi library perform IO on gluster volumes directly without FUSE mount. It is a filesystem like api and runs/sits in application process context. libgfapi eliminates the fuse and the kernel vfs layer from the glusterfs volume access. The speed and latency have improved with libgfapi access. [1]


Using libgfapi, various user-space filesystems (like NFS-Ganesha or Samba) or the virtualizer (like QEMU) can interact with GlusterFS which serves as back-end filesystem. Currently below projects integrate with glusterfs using libgfapi interfaces.


* qemu storage layer
* Samba VFS plugin
* NFS-Ganesha

All the APIs in libgfapi make use of `struct glfs` object. This object
contains information about volume name, glusterfs context associated,
subvols in the graph etc which makes it unique for each volume.


For any application to make use of libgfapi, it should typically start
with the below APIs in the following order -

* To create a new glfs object :

    glfs_t *glfs_new (const char *volname) ;

    glfs_new() returns glfs_t object.


* On this newly created glfs_t, you need to be either set a volfile path
   (glfs_set_volfile) or a volfile server (glfs_set_volfile_server).
    Incase of failures, the corresponding cleanup routine is
    "glfs_unset_volfile_server"

    int glfs_set_volfile (glfs_t *fs, const char *volfile);

    int glfs_set_volfile_server (glfs_t *fs, const char *transport,const char *host, int port) ;

    int glfs_unset_volfile_server (glfs_t *fs, const char *transport,const char *host, int port) ;

*  Specify logging parameters using glfs_set_logging():

    int glfs_set_logging (glfs_t *fs, const char *logfile, int loglevel) ;

* Initializes the glfs_t object using glfs_init()
    int glfs_init (glfs_t *fs) ;

#### FOPs APIs available with libgfapi :



    int glfs_get_volumeid (struct glfs *fs, char *volid, size_t size);

    int glfs_setfsuid (uid_t fsuid) ;

    int glfs_setfsgid (gid_t fsgid) ;

    int glfs_setfsgroups (size_t size, const gid_t *list) ;

    glfs_fd_t *glfs_open (glfs_t *fs, const char *path, int flags) ;

    glfs_fd_t *glfs_creat (glfs_t *fs, const char *path, int flags,mode_t mode) ;

    int glfs_close (glfs_fd_t *fd) ;

    glfs_t *glfs_from_glfd (glfs_fd_t *fd) ;

    int glfs_set_xlator_option (glfs_t *fs, const char *xlator, const char *key,const char *value) ;

    typedef void (*glfs_io_cbk) (glfs_fd_t *fd, ssize_t ret, void *data);

    ssize_t glfs_read (glfs_fd_t *fd, void *buf,size_t count, int flags) ;

    ssize_t glfs_write (glfs_fd_t *fd, const void *buf,size_t count, int flags) ;

    int glfs_read_async (glfs_fd_t *fd, void *buf, size_t count, int flags, glfs_io_cbk fn, void *data) ;

    int glfs_write_async (glfs_fd_t *fd, const void *buf, size_t count, int flags, glfs_io_cbk fn, void *data) ;

    ssize_t glfs_readv (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,int flags) ;

    ssize_t glfs_writev (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,int flags) ;

    int glfs_readv_async (glfs_fd_t *fd, const struct iovec *iov, int count, int flags, glfs_io_cbk fn, void *data) ;

    int glfs_writev_async (glfs_fd_t *fd, const struct iovec *iov, int count, int flags, glfs_io_cbk fn, void *data) ;

    ssize_t glfs_pread (glfs_fd_t *fd, void *buf, size_t count, off_t offset,int flags) ;

    ssize_t glfs_pwrite (glfs_fd_t *fd, const void *buf, size_t count, off_t offset, int flags) ;

    int glfs_pread_async (glfs_fd_t *fd, void *buf, size_t count, off_t offset,int flags, glfs_io_cbk fn, void *data) ;

    int glfs_pwrite_async (glfs_fd_t *fd, const void *buf, int count, off_t offset,int flags, glfs_io_cbk fn, void *data) ;

    ssize_t glfs_preadv (glfs_fd_t *fd, const struct iovec *iov, int iovcnt, int count, off_t offset, int flags,glfs_io_cbk fn, void *data) ;

    ssize_t glfs_pwritev (glfs_fd_t *fd, const struct iovec *iov, int iovcnt,int count, off_t offset, int flags, glfs_io_cbk fn, void *data) ;

    int glfs_preadv_async (glfs_fd_t *fd, const struct iovec *iov, glfs_io_cbk fn, void *data) ;

    int glfs_pwritev_async (glfs_fd_t *fd, const struct iovec *iov, glfs_io_cbk fn, void *data) ;

    off_t glfs_lseek (glfs_fd_t *fd, off_t offset, int whence) ;

    int glfs_truncate (glfs_t *fs, const char *path, off_t length) ;

    int glfs_ftruncate (glfs_fd_t *fd, off_t length) ;

    int glfs_ftruncate_async (glfs_fd_t *fd, off_t length, glfs_io_cbk fn,void *data) ;

    int glfs_lstat (glfs_t *fs, const char *path, struct stat *buf) ;

    int glfs_stat (glfs_t *fs, const char *path, struct stat *buf) ;

    int glfs_fstat (glfs_fd_t *fd, struct stat *buf) ;

    int glfs_fsync (glfs_fd_t *fd) ;

    int glfs_fsync_async (glfs_fd_t *fd, glfs_io_cbk fn, void *data) ;

    int glfs_fdatasync (glfs_fd_t *fd) ;

    int glfs_fdatasync_async (glfs_fd_t *fd, glfs_io_cbk fn, void *data) ;

    int glfs_access (glfs_t *fs, const char *path, int mode) ;

    int glfs_symlink (glfs_t *fs, const char *oldpath, const char *newpath) ;

    int glfs_readlink (glfs_t *fs, const char *path,char *buf, size_t bufsiz) ;

    int glfs_mknod (glfs_t *fs, const char *path, mode_t mode, dev_t dev) ;

    int glfs_mkdir (glfs_t *fs, const char *path, mode_t mode) ;

    int glfs_unlink (glfs_t *fs, const char *path) ;

    int glfs_rmdir (glfs_t *fs, const char *path) ;

    int glfs_rename (glfs_t *fs, const char *oldpath, const char *newpath) ;

    int glfs_link (glfs_t *fs, const char *oldpath, const char *newpath) ;

    glfs_fd_t *glfs_opendir (glfs_t *fs, const char *path) ;

    int glfs_readdir_r (glfs_fd_t *fd, struct dirent *dirent,struct dirent **result) ;

    int glfs_readdirplus_r (glfs_fd_t *fd, struct stat *stat, struct dirent *dirent, struct dirent **result) ;

    struct dirent *glfs_readdir (glfs_fd_t *fd) ;

    struct dirent *glfs_readdirplus (glfs_fd_t *fd, struct stat *stat) ;

    long glfs_telldir (glfs_fd_t *fd) ;

    void glfs_seekdir (glfs_fd_t *fd, long offset) ;

    int glfs_closedir (glfs_fd_t *fd) ;

    int glfs_statvfs (glfs_t *fs, const char *path, struct statvfs *buf) ;

    int glfs_chmod (glfs_t *fs, const char *path, mode_t mode) ;

    int glfs_fchmod (glfs_fd_t *fd, mode_t mode) ;

    int glfs_chown (glfs_t *fs, const char *path, uid_t uid, gid_t gid) ;

    int glfs_lchown (glfs_t *fs, const char *path, uid_t uid, gid_t gid) ;

    int glfs_fchown (glfs_fd_t *fd, uid_t uid, gid_t gid) ;

    int glfs_utimens (glfs_t *fs, const char *path,struct timespec times[2]) ;

    int glfs_lutimens (glfs_t *fs, const char *path,struct timespec times[2]) ;

    int glfs_futimens (glfs_fd_t *fd, struct timespec times[2]) ;

    ssize_t glfs_getxattr (glfs_t *fs, const char *path, const char *name,void *value, size_t size) ;

    ssize_t glfs_lgetxattr (glfs_t *fs, const char *path, const char *name,void *value, size_t size) ;

    ssize_t glfs_fgetxattr (glfs_fd_t *fd, const char *name,void *value, size_t size) ;

    ssize_t glfs_listxattr (glfs_t *fs, const char *path,void *value, size_t size) ;

    ssize_t glfs_llistxattr (glfs_t *fs, const char *path, void *value,size_t size) ;

    ssize_t glfs_flistxattr (glfs_fd_t *fd, void *value, size_t size) ;

    int glfs_setxattr (glfs_t *fs, const char *path, const char *name,const void *value, size_t size, int flags) ;

    int glfs_lsetxattr (glfs_t *fs, const char *path, const char *name,const void *value, size_t size, int flags) ;

    int glfs_fsetxattr (glfs_fd_t *fd, const char *name,const void *value, size_t size, int flags) ;

    int glfs_removexattr (glfs_t *fs, const char *path, const char *name) ;

    int glfs_lremovexattr (glfs_t *fs, const char *path, const char *name) ;

    int glfs_fremovexattr (glfs_fd_t *fd, const char *name) ;

    int glfs_fallocate(glfs_fd_t *fd, int keep_size, off_t offset, size_t len) ;

    int glfs_discard(glfs_fd_t *fd, off_t offset, size_t len) ;

    int glfs_discard_async (glfs_fd_t *fd, off_t length, size_t lent, glfs_io_cbk fn, void *data) ;

    int glfs_zerofill(glfs_fd_t *fd, off_t offset, off_t len) ;

    int glfs_zerofill_async (glfs_fd_t *fd, off_t length, off_t len, glfs_io_cbk fn, void *data) ;

    char *glfs_getcwd (glfs_t *fs, char *buf, size_t size) ;

    int glfs_chdir (glfs_t *fs, const char *path) ;

    int glfs_fchdir (glfs_fd_t *fd) ;

    char *glfs_realpath (glfs_t *fs, const char *path, char *resolved_path) ;

    int glfs_posix_lock (glfs_fd_t *fd, int cmd, struct flock *flock) ;

    glfs_fd_t *glfs_dup (glfs_fd_t *fd) ;


    struct glfs_object *glfs_h_lookupat (struct glfs *fs,struct glfs_object *parent,
				     const char *path,
                                     struct stat *stat) ;

    struct glfs_object *glfs_h_creat (struct glfs *fs, struct glfs_object *parent,
				  const char *path, int flags, mode_t mode,
				  struct stat *sb) ;

    struct glfs_object *glfs_h_mkdir (struct glfs *fs, struct glfs_object *parent,
				  const char *path, mode_t flags,
				  struct stat *sb) ;

    struct glfs_object *glfs_h_mknod (struct glfs *fs, struct glfs_object *parent,
				  const char *path, mode_t mode, dev_t dev,
				  struct stat *sb) ;

    struct glfs_object *glfs_h_symlink (struct glfs *fs, struct glfs_object *parent,
				    const char *name, const char *data,
				    struct stat *stat) ;


    int glfs_h_unlink (struct glfs *fs, struct glfs_object *parent,
		   const char *path) ;

    int glfs_h_close (struct glfs_object *object) ;

    int glfs_caller_specific_init (void *uid_caller_key, void *gid_caller_key,
			       void *future) ;

    int glfs_h_truncate (struct glfs *fs, struct glfs_object *object,
                     off_t offset) ;

    int glfs_h_stat(struct glfs *fs, struct glfs_object *object,
                struct stat *stat) ;

    int glfs_h_getattrs (struct glfs *fs, struct glfs_object *object,
		     struct stat *stat) ;

    int glfs_h_getxattrs (struct glfs *fs, struct glfs_object *object,
		      const char *name, void *value,
		      size_t size) ;

    int glfs_h_setattrs (struct glfs *fs, struct glfs_object *object,
		     struct stat *sb, int valid) ;

    int glfs_h_setxattrs (struct glfs *fs, struct glfs_object *object,
		      const char *name, const void *value,
		      size_t size, int flags) ;

    int glfs_h_readlink (struct glfs *fs, struct glfs_object *object, char *buf,
		     size_t bufsiz) ;

    int glfs_h_link (struct glfs *fs, struct glfs_object *linktgt,
		 struct glfs_object *parent, const char *name) ;

    int glfs_h_rename (struct glfs *fs, struct glfs_object *olddir,
		   const char *oldname, struct glfs_object *newdir,
		   const char *newname) ;

    int glfs_h_removexattrs (struct glfs *fs, struct glfs_object *object,
			 const char *name) ;

    ssize_t glfs_h_extract_handle (struct glfs_object *object,
			       unsigned char *handle, int len) ;

    struct glfs_object *glfs_h_create_from_handle (struct glfs *fs,
					       unsigned char *handle, int len,
					       struct stat *stat) ;


    struct glfs_fd *glfs_h_opendir (struct glfs *fs,
                                struct glfs_object *object) ;

    struct glfs_fd *glfs_h_open (struct glfs *fs, struct glfs_object *object,
			     int flags) ;

For more details on these apis please refer glfs.h and glfs-handles.h  in the source tree (api/src/) of glusterfs:

* Incase of failures or to close the connection and destroy glfs_t
object, use glfs_fini.

    int glfs_fini (glfs_t *fs) ;


All the fileops are typically divided into below categories

* a) Handle based Operations -

These APIs create/make use of a glfs_object (referred as handles) unique
to each file within a volume.
The structure glfs_object contains inode pointer and gfid.

For example: Since NFS protocol uses file handles to access files, these APIs are
mainly used by NFS-Ganesha server.

Eg:

    struct glfs_object *glfs_h_lookupat (struct glfs *fs,
                                      struct glfs_object *parent,
                                      const char *path,
                                      struct stat *stat);

    struct glfs_object *glfs_h_creat (struct glfs *fs,
                                   struct glfs_object *parent,
                                   const char *path,
                                   int flags, mode_t mode,
                                   struct stat *sb);

    struct glfs_object *glfs_h_mkdir (struct glfs *fs,
                                struct glfs_object *parent,
                                const char *path, mode_t flags,
                                struct stat *sb);



* b) File path/descriptor based Operations -

These APIs make use of file path/descriptor to determine the file on
which it needs to operate on.

For example: Samba uses these APIs for file operations.

Examples of the APIs using file path -

    int glfs_chdir (glfs_t *fs, const char *path) ;

    char *glfs_realpath (glfs_t *fs, const char *path, char *resolved_path) ;

Once the file is opened, the file-descriptor generated is used for
further operations.

Eg:

    int glfs_posix_lock (glfs_fd_t *fd, int cmd, struct flock *flock) ;
    glfs_fd_t *glfs_dup (glfs_fd_t *fd) ;



#### libgfapi bindings :

libgfapi bindings are available for below languages:

    - Go
    - Java
    - python [2]
    - Ruby

For more details on these bindings,please refer :

    #http://www.gluster.org/community/documentation/index.php/Language_Bindings

References:

[1] http://humblec.com/libgfapi-interface-glusterfs/
[2] http://www.gluster.org/2014/04/play-with-libgfapi-and-its-python-bindings/

