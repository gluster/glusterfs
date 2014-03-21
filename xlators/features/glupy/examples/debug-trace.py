import sys
import stat
from uuid import UUID
from time import strftime, localtime
from gluster.glupy import *

# This translator was written primarily to test the fop entry point definitions
# and structure definitions in 'glupy.py'.

# It is similar to the C language debug-trace translator, which logs the
# arguments passed to the fops and their corresponding cbk functions.

dl.get_id.restype = c_long
dl.get_id.argtypes = [ POINTER(call_frame_t) ]

dl.get_rootunique.restype = c_uint64
dl.get_rootunique.argtypes = [ POINTER(call_frame_t) ]

def uuid2str (gfid):
        return str(UUID(''.join(map("{0:02x}".format, gfid))))


def st_mode_from_ia (prot, filetype):
        st_mode = 0
        type_bit = 0
        prot_bit = 0

        if filetype == IA_IFREG:
                type_bit = stat.S_IFREG
        elif filetype == IA_IFDIR:
                type_bit = stat.S_IFDIR
        elif filetype == IA_IFLNK:
                type_bit = stat.S_IFLNK
        elif filetype == IA_IFBLK:
                type_bit = stat.S_IFBLK
        elif filetype == IA_IFCHR:
                type_bit = stat.S_IFCHR
        elif filetype == IA_IFIFO:
                type_bit = stat.S_IFIFO
        elif filetype == IA_IFSOCK:
                type_bit = stat.S_IFSOCK
        elif filetype == IA_INVAL:
                pass


        if prot.suid:
                prot_bit |= stat.S_ISUID
        if prot.sgid:
                prot_bit |= stat.S_ISGID
        if prot.sticky:
                prot_bit |= stat.S_ISVTX

        if prot.owner.read:
                prot_bit |= stat.S_IRUSR
        if prot.owner.write:
                prot_bit |= stat.S_IWUSR
        if prot.owner.execn:
                prot_bit |= stat.S_IXUSR

        if prot.group.read:
                prot_bit |= stat.S_IRGRP
        if prot.group.write:
                prot_bit |= stat.S_IWGRP
        if prot.group.execn:
                prot_bit |= stat.S_IXGRP

        if prot.other.read:
                prot_bit |= stat.S_IROTH
        if prot.other.write:
                prot_bit |= stat.S_IWOTH
        if prot.other.execn:
                prot_bit |= stat.S_IXOTH

        st_mode = (type_bit | prot_bit)

        return st_mode


def trace_stat2str (buf):
        gfid = uuid2str(buf.contents.ia_gfid)
        mode = st_mode_from_ia(buf.contents.ia_prot, buf.contents.ia_type)
        atime_buf = strftime("[%b %d %H:%M:%S]",
                             localtime(buf.contents.ia_atime))
        mtime_buf = strftime("[%b %d %H:%M:%S]",
                             localtime(buf.contents.ia_mtime))
        ctime_buf = strftime("[%b %d %H:%M:%S]",
                             localtime(buf.contents.ia_ctime))
        return ("(gfid={0:s}, ino={1:d}, mode={2:o}, nlink={3:d}, uid ={4:d}, "+
                "gid ={5:d}, size={6:d}, blocks={7:d}, atime={8:s}, mtime={9:s}, "+
                "ctime={10:s})").format(gfid, buf.contents.ia_no, mode,
                                        buf.contents.ia_nlink,
                                        buf.contents.ia_uid,
                                        buf.contents.ia_gid,
                                        buf.contents.ia_size,
                                        buf.contents.ia_blocks,
                                        atime_buf, mtime_buf,
                                        ctime_buf)

class xlator(Translator):

        def __init__(self, c_this):
                Translator.__init__(self, c_this)
                self.gfids = {}

        def lookup_fop(self, frame, this, loc, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.gfid)
                print("GLUPY TRACE LOOKUP FOP- {0:d}: gfid={1:s}; " +
                      "path={2:s}").format(unique, gfid, loc.contents.path)
                self.gfids[key] = gfid
                dl.wind_lookup(frame, POINTER(xlator_t)(), loc, xdata)
                return 0

        def lookup_cbk(self, frame, cookie, this, op_ret, op_errno,
                       inode, buf, xdata, postparent):
                unique =dl.get_rootunique(frame)
                key =dl.get_id(frame)
                if op_ret == 0:
                        gfid = uuid2str(buf.contents.ia_gfid)
                        statstr = trace_stat2str(buf)
                        postparentstr = trace_stat2str(postparent)
                        print("GLUPY TRACE LOOKUP CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; *buf={3:s}; " +
                              "*postparent={4:s}").format(unique, gfid,
                                                          op_ret, statstr,
                                                          postparentstr)
                else:
                        gfid = self.gfids[key]
                        print("GLUPY TRACE LOOKUP CBK - {0:d}: gfid={1:s};" +
                              " op_ret={2:d}; op_errno={3:d}").format(unique,
                                                                      gfid,
                                                                      op_ret,
                                                                      op_errno)
                del self.gfids[key]
                dl.unwind_lookup(frame, cookie, this, op_ret, op_errno,
                                 inode, buf, xdata, postparent)
                return 0

        def create_fop(self, frame, this, loc, flags, mode, umask, fd,
                       xdata):
                unique = dl.get_rootunique(frame)
                gfid = uuid2str(loc.contents.gfid)
                print("GLUPY TRACE CREATE FOP- {0:d}: gfid={1:s}; path={2:s}; " +
                      "fd={3:s}; flags=0{4:o}; mode=0{5:o}; " +
                      "umask=0{6:o}").format(unique, gfid, loc.contents.path,
                                             fd, flags, mode, umask)
                dl.wind_create(frame, POINTER(xlator_t)(), loc, flags,mode,
                               umask, fd, xdata)
                return 0

        def create_cbk(self, frame, cookie, this, op_ret, op_errno, fd,
                       inode, buf, preparent, postparent, xdata):
                unique = dl.get_rootunique(frame)
                if op_ret >= 0:
                        gfid = uuid2str(inode.contents.gfid)
                        statstr = trace_stat2str(buf)
                        preparentstr = trace_stat2str(preparent)
                        postparentstr = trace_stat2str(postparent)
                        print("GLUPY TRACE CREATE CBK- {0:d}: gfid={1:s};" +
                              " op_ret={2:d}; fd={3:s}; *stbuf={4:s}; " +
                              "*preparent={5:s};" +
                              " *postparent={6:s}").format(unique, gfid, op_ret,
                                                           fd, statstr,
                                                           preparentstr,
                                                           postparentstr)
                else:
                        print ("GLUPY TRACE CREATE CBK- {0:d}: op_ret={1:d}; " +
                              "op_errno={2:d}").format(unique, op_ret, op_errno)
                dl.unwind_create(frame, cookie, this, op_ret, op_errno, fd,
                                 inode, buf, preparent, postparent, xdata)
                return 0

        def open_fop(self, frame, this, loc, flags, fd, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE OPEN FOP- {0:d}: gfid={1:s}; path={2:s}; "+
                      "flags={3:d}; fd={4:s}").format(unique, gfid,
                                                      loc.contents.path, flags,
                                                      fd)
                self.gfids[key] = gfid
                dl.wind_open(frame, POINTER(xlator_t)(), loc, flags, fd, xdata)
                return 0

        def open_cbk(self, frame, cookie, this, op_ret, op_errno, fd, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE OPEN CBK- {0:d}: gfid={1:s}; op_ret={2:d}; "
                      "op_errno={3:d}; *fd={4:s}").format(unique, gfid,
                                                          op_ret, op_errno, fd)
                del self.gfids[key]
                dl.unwind_open(frame, cookie, this, op_ret, op_errno, fd,
                               xdata)
                return 0

        def readv_fop(self, frame, this, fd, size, offset, flags, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(fd.contents.inode.contents.gfid)
                print("GLUPY TRACE READV FOP- {0:d}: gfid={1:s}; "+
                      "fd={2:s}; size ={3:d}; offset={4:d}; " +
                      "flags=0{5:x}").format(unique, gfid, fd, size, offset,
                                             flags)
                self.gfids[key] = gfid
                dl.wind_readv (frame, POINTER(xlator_t)(), fd, size, offset,
                               flags, xdata)
                return 0

        def readv_cbk(self, frame, cookie, this, op_ret, op_errno, vector,
                      count, buf, iobref, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                if op_ret >= 0:
                        statstr = trace_stat2str(buf)
                        print("GLUPY TRACE READV CBK- {0:d}: gfid={1:s}, "+
                              "op_ret={2:d}; *buf={3:s};").format(unique, gfid,
                                                                  op_ret,
                                                                  statstr)

                else:
                        print("GLUPY TRACE READV CBK- {0:d}: gfid={1:s}, "+
                              "op_ret={2:d}; op_errno={3:d}").format(unique,
                                                                     gfid,
                                                                     op_ret,
                                                                     op_errno)
                del self.gfids[key]
                dl.unwind_readv (frame, cookie, this, op_ret, op_errno,
                                 vector, count, buf, iobref, xdata)
                return 0

        def writev_fop(self, frame, this, fd, vector, count, offset, flags,
                       iobref, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(fd.contents.inode.contents.gfid)
                print("GLUPY TRACE  WRITEV FOP- {0:d}: gfid={1:s}; " +
                      "fd={2:s}; count={3:d}; offset={4:d}; " +
                      "flags=0{5:x}").format(unique, gfid, fd, count, offset,
                                             flags)
                self.gfids[key] = gfid
                dl.wind_writev(frame, POINTER(xlator_t)(), fd, vector, count,
                               offset, flags, iobref, xdata)
                return 0

        def writev_cbk(self, frame, cookie, this, op_ret, op_errno, prebuf,
                       postbuf, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                if op_ret >= 0:
                        preopstr = trace_stat2str(prebuf)
                        postopstr = trace_stat2str(postbuf)
                        print("GLUPY TRACE WRITEV CBK- {0:d}: op_ret={1:d}; " +
                              "*prebuf={2:s}; " +
                              "*postbuf={3:s}").format(unique, op_ret, preopstr,
                                                       postopstr)
                else:
                        gfid = self.gfids[key]
                        print("GLUPY TRACE WRITEV CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; op_errno={3:d}").format(unique,
                                                                     gfid,
                                                                     op_ret,
                                                                     op_errno)
                del self.gfids[key]
                dl.unwind_writev (frame, cookie, this, op_ret, op_errno,
                                  prebuf, postbuf, xdata)
                return 0

        def opendir_fop(self, frame, this, loc, fd, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE OPENDIR FOP- {0:d}: gfid={1:s}; path={2:s}; "+
                      "fd={3:s}").format(unique, gfid, loc.contents.path, fd)
                self.gfids[key] = gfid
                dl.wind_opendir(frame, POINTER(xlator_t)(), loc, fd, xdata)
                return 0

        def opendir_cbk(self, frame, cookie, this, op_ret, op_errno, fd,
                        xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE OPENDIR CBK- {0:d}: gfid={1:s}; op_ret={2:d};"+
                      " op_errno={3:d}; fd={4:s}").format(unique, gfid, op_ret,
                                                          op_errno, fd)
                del self.gfids[key]
                dl.unwind_opendir(frame, cookie, this, op_ret, op_errno,
                                  fd, xdata)
                return 0

        def readdir_fop(self, frame, this, fd, size, offset, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(fd.contents.inode.contents.gfid)
                print("GLUPY TRACE READDIR FOP- {0:d}:  gfid={1:s}; fd={2:s}; " +
                      "size={3:d}; offset={4:d}").format(unique, gfid, fd, size,
                                                         offset)
                self.gfids[key] = gfid
                dl.wind_readdir(frame, POINTER(xlator_t)(), fd, size, offset,
                                xdata)
                return 0

        def readdir_cbk(self, frame, cookie, this, op_ret, op_errno, buf,
                        xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE READDIR CBK- {0:d}: gfid={1:s}; op_ret={2:d};"+
                      " op_errno={3:d}").format(unique, gfid, op_ret, op_errno)
                del self.gfids[key]
                dl.unwind_readdir(frame, cookie, this, op_ret, op_errno, buf,
                                  xdata)
                return 0

        def readdirp_fop(self, frame, this, fd, size, offset, dictionary):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(fd.contents.inode.contents.gfid)
                print("GLUPY TRACE READDIRP FOP- {0:d}: gfid={1:s}; fd={2:s}; "+
                      " size={3:d}; offset={4:d}").format(unique, gfid, fd, size,
                                                          offset)
                self.gfids[key] = gfid
                dl.wind_readdirp(frame, POINTER(xlator_t)(), fd, size, offset,
                                 dictionary)
                return 0

        def readdirp_cbk(self, frame, cookie, this, op_ret, op_errno, buf,
                         xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE READDIRP CBK- {0:d}: gfid={1:s}; "+
                      "op_ret={2:d}; op_errno={3:d}").format(unique, gfid,
                                                             op_ret, op_errno)
                del self.gfids[key]
                dl.unwind_readdirp(frame, cookie, this, op_ret, op_errno, buf,
                                  xdata)
                return 0

        def mkdir_fop(self, frame, this, loc, mode, umask, xdata):
                unique = dl.get_rootunique(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE MKDIR FOP- {0:d}: gfid={1:s}; path={2:s}; " +
                      "mode={3:d}; umask=0{4:o}").format(unique, gfid,
                                                         loc.contents.path, mode,
                                                         umask)
                dl.wind_mkdir(frame, POINTER(xlator_t)(), loc, mode, umask,
                              xdata)
                return 0

        def mkdir_cbk(self, frame, cookie, this, op_ret, op_errno, inode, buf,
                      preparent, postparent,  xdata):
                unique = dl.get_rootunique(frame)
                if op_ret == 0:
                        gfid = uuid2str(inode.contents.gfid)
                        statstr = trace_stat2str(buf)
                        preparentstr = trace_stat2str(preparent)
                        postparentstr = trace_stat2str(postparent)
                        print("GLUPY TRACE MKDIR CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; *stbuf={3:s}; *prebuf={4:s}; "+
                              "*postbuf={5:s} ").format(unique, gfid, op_ret,
                                                        statstr,
                                                        preparentstr,
                                                        postparentstr)
                else:
                        print("GLUPY TRACE MKDIR CBK- {0:d}:  op_ret={1:d}; "+
                              "op_errno={2:d}").format(unique, op_ret, op_errno)
                dl.unwind_mkdir(frame, cookie, this, op_ret, op_errno, inode,
                                buf, preparent, postparent, xdata)
                return 0

        def rmdir_fop(self, frame, this, loc, flags, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE RMDIR FOP- {0:d}: gfid={1:s}; path={2:s}; "+
                      "flags={3:d}").format(unique, gfid, loc.contents.path,
                                            flags)
                self.gfids[key] = gfid
                dl.wind_rmdir(frame, POINTER(xlator_t)(), loc, flags, xdata)
                return 0

        def rmdir_cbk(self, frame, cookie, this, op_ret, op_errno, preparent,
                      postparent, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                if op_ret == 0:
                        preparentstr = trace_stat2str(preparent)
                        postparentstr = trace_stat2str(postparent)
                        print("GLUPY TRACE RMDIR CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; *prebuf={3:s}; "+
                              "*postbuf={4:s}").format(unique, gfid, op_ret,
                                                       preparentstr,
                                                       postparentstr)
                else:
                        print("GLUPY TRACE RMDIR CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; op_errno={3:d}").format(unique,
                                                                     gfid,
                                                                     op_ret,
                                                                     op_errno)
                del self.gfids[key]
                dl.unwind_rmdir(frame, cookie, this, op_ret, op_errno,
                                preparent, postparent, xdata)
                return 0

        def stat_fop(self, frame, this, loc, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE STAT FOP- {0:d}: gfid={1:s}; " +
                      " path={2:s}").format(unique, gfid, loc.contents.path)
                self.gfids[key] = gfid
                dl.wind_stat(frame, POINTER(xlator_t)(), loc, xdata)
                return 0

        def stat_cbk(self, frame, cookie, this, op_ret, op_errno, buf,
                     xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                if op_ret == 0:
                        statstr = trace_stat2str(buf)
                        print("GLUPY TRACE STAT CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d};  *buf={3:s};").format(unique,
                                                                   gfid,
                                                                   op_ret,
                                                                   statstr)
                else:
                        print("GLUPY TRACE STAT CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; op_errno={3:d}").format(unique,
                                                                     gfid,
                                                                     op_ret,
                                                                     op_errno)
                del self.gfids[key]
                dl.unwind_stat(frame, cookie, this, op_ret, op_errno,
                               buf, xdata)
                return 0

        def fstat_fop(self, frame, this, fd, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(fd.contents.inode.contents.gfid)
                print("GLUPY TRACE FSTAT FOP- {0:d}:  gfid={1:s}; " +
                      "fd={2:s}").format(unique, gfid, fd)
                self.gfids[key] = gfid
                dl.wind_fstat(frame, POINTER(xlator_t)(), fd, xdata)
                return 0

        def fstat_cbk(self, frame, cookie, this, op_ret, op_errno, buf,
                      xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                if op_ret == 0:
                        statstr = trace_stat2str(buf)
                        print("GLUPY TRACE FSTAT CBK- {0:d}: gfid={1:s} "+
                              " op_ret={2:d}; *buf={3:s}").format(unique,
                                                                  gfid,
                                                                  op_ret,
                                                                  statstr)
                else:
                        print("GLUPY TRACE FSTAT CBK- {0:d}: gfid={1:s} "+
                              "op_ret={2:d}; op_errno={3:d}").format(unique.
                                                                     gfid,
                                                                     op_ret,
                                                                     op_errno)
                del self.gfids[key]
                dl.unwind_fstat(frame, cookie, this, op_ret, op_errno,
                                buf, xdata)
                return 0

        def statfs_fop(self, frame, this, loc, xdata):
                unique = dl.get_rootunique(frame)
                if loc.contents.inode:
                        gfid = uuid2str(loc.contents.inode.contents.gfid)
                else:
                        gfid = "0"
                print("GLUPY TRACE STATFS FOP- {0:d}: gfid={1:s}; "+
                      "path={2:s}").format(unique, gfid, loc.contents.path)
                dl.wind_statfs(frame, POINTER(xlator_t)(), loc, xdata)
                return 0

        def statfs_cbk(self, frame, cookie, this, op_ret, op_errno, buf,
                       xdata):
                unique = dl.get_rootunique(frame)
                if op_ret == 0:
                        #TBD: print buf (pointer to an iovec type object)
                        print("GLUPY TRACE STATFS CBK {0:d}: "+
                              "op_ret={1:d}").format(unique, op_ret)
                else:
                        print("GLUPY TRACE STATFS CBK-  {0:d}"+
                              "op_ret={1:d}; op_errno={2:d}").format(unique,
                                                                     op_ret,
                                                                     op_errno)
                dl.unwind_statfs(frame, cookie, this, op_ret, op_errno,
                                 buf, xdata)
                return 0

        def getxattr_fop(self, frame, this, loc, name, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE GETXATTR FOP- {0:d}: gfid={1:s}; path={2:s};"+
                      " name={3:s}").format(unique, gfid, loc.contents.path,
                                            name)
                self.gfids[key]=gfid
                dl.wind_getxattr(frame, POINTER(xlator_t)(), loc, name, xdata)
                return 0

        def getxattr_cbk(self, frame, cookie, this, op_ret, op_errno,
                         dictionary, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE GETXATTR CBK- {0:d}: gfid={1:s}; "+
                      "op_ret={2:d}; op_errno={3:d}; "+
                      " dictionary={4:s}").format(unique, gfid, op_ret, op_errno,
                                                  dictionary)
                del self.gfids[key]
                dl.unwind_getxattr(frame, cookie, this, op_ret, op_errno,
                                   dictionary, xdata)
                return 0

        def fgetxattr_fop(self, frame, this, fd, name, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(fd.contents.inode.contents.gfid)
                print("GLUPY TRACE FGETXATTR FOP- {0:d}: gfid={1:s}; fd={2:s}; "+
                      "name={3:s}").format(unique, gfid, fd, name)
                self.gfids[key] = gfid
                dl.wind_fgetxattr(frame, POINTER(xlator_t)(), fd, name, xdata)
                return 0

        def fgetxattr_cbk(self, frame, cookie, this, op_ret, op_errno,
                          dictionary, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE FGETXATTR CBK- {0:d}: gfid={1:s}; "+
                      "op_ret={2:d}; op_errno={3:d};"+
                      " dictionary={4:s}").format(unique, gfid, op_ret,
                                                  op_errno, dictionary)
                del self.gfids[key]
                dl.unwind_fgetxattr(frame, cookie, this, op_ret, op_errno,
                                    dictionary, xdata)
                return 0

        def setxattr_fop(self, frame, this, loc, dictionary, flags, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE SETXATTR FOP- {0:d}:  gfid={1:s}; path={2:s};"+
                      " flags={3:d}").format(unique, gfid, loc.contents.path,
                                             flags)
                self.gfids[key] = gfid
                dl.wind_setxattr(frame, POINTER(xlator_t)(), loc, dictionary,
                                 flags, xdata)
                return 0

        def setxattr_cbk(self, frame, cookie, this, op_ret, op_errno, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE SETXATTR CBK- {0:d}: gfid={1:s}; "+
                      "op_ret={2:d}; op_errno={3:d}").format(unique, gfid,
                                                             op_ret, op_errno)
                del self.gfids[key]
                dl.unwind_setxattr(frame, cookie, this, op_ret, op_errno,
                                   xdata)
                return 0

        def fsetxattr_fop(self, frame, this, fd, dictionary, flags, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(fd.contents.inode.contents.gfid)
                print("GLUPY TRACE FSETXATTR FOP- {0:d}: gfid={1:s}; fd={2:p}; "+
                      "flags={3:d}").format(unique, gfid, fd, flags)
                self.gfids[key] = gfid
                dl.wind_fsetxattr(frame, POINTER(xlator_t)(), fd, dictionary,
                                  flags, xdata)
                return 0

        def fsetxattr_cbk(self, frame, cookie, this, op_ret, op_errno, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE FSETXATTR CBK- {0:d}: gfid={1:s};  "+
                      "op_ret={2:d}; op_errno={3:d}").format(unique, gfid,
                                                             op_ret, op_errno)
                del self.gfids[key]
                dl.unwind_fsetxattr(frame, cookie, this, op_ret, op_errno,
                                   xdata)
                return 0

        def removexattr_fop(self, frame, this, loc, name, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE REMOVEXATTR FOP- {0:d}:  gfid={1:s}; "+
                      "path={2:s}; name={3:s}").format(unique, gfid,
                                                       loc.contents.path,
                                                       name)
                self.gfids[key] = gfid
                dl.wind_removexattr(frame, POINTER(xlator_t)(), loc, name,
                                    xdata)
                return 0

        def removexattr_cbk(self, frame, cookie, this, op_ret, op_errno,
                            xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                print("GLUPY TRACE REMOVEXATTR CBK- {0:d}: gfid={1:s} "+
                      " op_ret={2:d}; op_errno={3:d}").format(unique, gfid,
                                                              op_ret, op_errno)
                del self.gfids[key]
                dl.unwind_removexattr(frame, cookie, this, op_ret, op_errno,
                                      xdata)
                return 0

        def link_fop(self, frame, this, oldloc, newloc, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                if (newloc.contents.inode):
                        newgfid = uuid2str(newloc.contents.inode.contents.gfid)
                else:
                        newgfid = "0"
                oldgfid = uuid2str(oldloc.contents.inode.contents.gfid)
                print("GLUPY TRACE LINK FOP-{0:d}: oldgfid={1:s}; oldpath={2:s};"+
                      "newgfid={3:s};"+
                      "newpath={4:s}").format(unique, oldgfid,
                                              oldloc.contents.path,
                                              newgfid,
                                              newloc.contents.path)
                self.gfids[key] =  oldgfid
                dl.wind_link(frame, POINTER(xlator_t)(), oldloc, newloc,
                             xdata)
                return 0

        def link_cbk(self, frame, cookie, this, op_ret, op_errno, inode, buf,
                     preparent, postparent, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                if op_ret == 0:
                        statstr = trace_stat2str(buf)
                        preparentstr = trace_stat2str(preparent)
                        postparentstr = trace_stat2str(postparent)
                        print("GLUPY TRACE LINK CBK- {0:d}: op_ret={1:d} "+
                              "*stbuf={2:s}; *prebuf={3:s}; "+
                              "*postbuf={4:s} ").format(unique, op_ret, statstr,
                                                        preparentstr,
                                                        postparentstr)
                else:
                        print("GLUPY TRACE LINK CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; "+
                              "op_errno={3:d}").format(unique, gfid,
                                                       op_ret, op_errno)
                del self.gfids[key]
                dl.unwind_link(frame, cookie, this, op_ret, op_errno, inode,
                               buf, preparent, postparent, xdata)
                return 0

        def unlink_fop(self, frame, this, loc, xflag, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE UNLINK FOP- {0:d}; gfid={1:s}; path={2:s}; "+
                      "flag={3:d}").format(unique, gfid, loc.contents.path,
                                           xflag)
                self.gfids[key] = gfid
                dl.wind_unlink(frame, POINTER(xlator_t)(), loc, xflag,
                               xdata)
                return 0

        def unlink_cbk(self, frame, cookie, this, op_ret, op_errno,
                       preparent, postparent, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                if op_ret == 0:
                        preparentstr = trace_stat2str(preparent)
                        postparentstr = trace_stat2str(postparent)
                        print("GLUPY TRACE UNLINK CBK- {0:d}: gfid ={1:s}; "+
                              "op_ret={2:d}; *prebuf={3:s}; "+
                              "*postbuf={4:s} ").format(unique, gfid, op_ret,
                                                        preparentstr,
                                                        postparentstr)
                else:
                        print("GLUPY TRACE UNLINK CBK: {0:d}: gfid ={1:s}; "+
                              "op_ret={2:d}; "+
                              "op_errno={3:d}").format(unique, gfid, op_ret,
                                                       op_errno)
                del self.gfids[key]
                dl.unwind_unlink(frame, cookie, this, op_ret, op_errno,
                                 preparent, postparent, xdata)
                return 0

        def readlink_fop(self, frame, this, loc, size, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE READLINK FOP- {0:d}:  gfid={1:s}; path={2:s};"+
                      " size={3:d}").format(unique, gfid, loc.contents.path,
                                            size)
                self.gfids[key] = gfid
                dl.wind_readlink(frame, POINTER(xlator_t)(), loc, size,
                               xdata)
                return 0

        def readlink_cbk(self, frame, cookie, this, op_ret, op_errno,
                         buf, stbuf, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid  = self.gfids[key]
                if op_ret == 0:
                        statstr = trace_stat2str(stbuf)
                        print("GLUPY TRACE READLINK CBK- {0:d}: gfid={1:s} "+
                              " op_ret={2:d}; op_errno={3:d}; *prebuf={4:s}; "+
                              "*postbuf={5:s} ").format(unique, gfid,
                                                        op_ret, op_errno,
                                                        buf, statstr)
                else:
                        print("GLUPY TRACE READLINK CBK- {0:d}: gfid={1:s} "+
                              " op_ret={2:d}; op_errno={3:d}").format(unique,
                                                                      gfid,
                                                                      op_ret,
                                                                      op_errno)
                del self.gfids[key]
                dl.unwind_readlink(frame, cookie, this, op_ret, op_errno, buf,
                                   stbuf, xdata)
                return 0

        def symlink_fop(self, frame, this, linkpath, loc, umask, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = uuid2str(loc.contents.inode.contents.gfid)
                print("GLUPY TRACE SYMLINK FOP- {0:d}: gfid={1:s}; "+
                      "linkpath={2:s}; path={3:s};"+
                      "umask=0{4:o}").format(unique, gfid, linkpath,
                                             loc.contents.path, umask)
                self.gfids[key] = gfid
                dl.wind_symlink(frame, POINTER(xlator_t)(), linkpath, loc,
                                umask, xdata)
                return 0

        def symlink_cbk(self, frame, cookie, this, op_ret, op_errno,
                        inode, buf, preparent, postparent, xdata):
                unique = dl.get_rootunique(frame)
                key = dl.get_id(frame)
                gfid = self.gfids[key]
                if op_ret == 0:
                        statstr = trace_stat2str(buf)
                        preparentstr = trace_stat2str(preparent)
                        postparentstr = trace_stat2str(postparent)
                        print("GLUPY TRACE SYMLINK CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; *stbuf={3:s}; *preparent={4:s}; "+
                              "*postparent={5:s}").format(unique, gfid,
                                                          op_ret, statstr,
                                                          preparentstr,
                                                          postparentstr)
                else:
                        print("GLUPY TRACE SYMLINK CBK- {0:d}: gfid={1:s}; "+
                              "op_ret={2:d}; op_errno={3:d}").format(unique,
                                                                     gfid,
                                                                     op_ret,
                                                                     op_errno)
                del self.gfids[key]
                dl.unwind_symlink(frame, cookie, this, op_ret, op_errno,
                                  inode, buf, preparent, postparent, xdata)
                return 0
