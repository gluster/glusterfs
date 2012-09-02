# Excerpt from autoconf/autoconf/programs.m4
# This file is part of Autoconf.                       -*- Autoconf -*-
# Checking for programs.

# Copyright (C) 1992, 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001,
# 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010 Free Software
# Foundation, Inc.

# This file is part of Autoconf.  This program is free
# software; you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# Under Section 7 of GPL version 3, you are granted additional
# permissions described in the Autoconf Configure Script Exception,
# version 3.0, as published by the Free Software Foundation.
#
# You should have received a copy of the GNU General Public License
# and a copy of the Autoconf Configure Script Exception along with
# this program; see the files COPYINGv3 and COPYING.EXCEPTION
# respectively.  If not, see <http://www.gnu.org/licenses/>.

# Written by David MacKenzie, with help from
# Franc,ois Pinard, Karl Berry, Richard Pixley, Ian Lance Taylor,
# Roland McGrath, Noah Friedman, david d zuhn, and many others.

# AC_PROG_MKDIR_P
# ---------------
# Check whether `mkdir -p' is known to be thread-safe, and fall back to
# install-sh -d otherwise.
#
# Automake 1.8 used `mkdir -m 0755 -p --' to ensure that directories
# created by `make install' are always world readable, even if the
# installer happens to have an overly restrictive umask (e.g. 077).
# This was a mistake.  There are at least two reasons why we must not
# use `-m 0755':
#   - it causes special bits like SGID to be ignored,
#   - it may be too restrictive (some setups expect 775 directories).
#
# Do not use -m 0755 and let people choose whatever they expect by
# setting umask.
#
# We cannot accept any implementation of `mkdir' that recognizes `-p'.
# Some implementations (such as Solaris 8's) are vulnerable to race conditions:
# if a parallel make tries to run `mkdir -p a/b' and `mkdir -p a/c'
# concurrently, both version can detect that a/ is missing, but only
# one can create it and the other will error out.  Consequently we
# restrict ourselves to known race-free implementations.
#
# Automake used to define mkdir_p as `mkdir -p .', in order to
# allow $(mkdir_p) to be used without argument.  As in
#   $(mkdir_p) $(somedir)
# where $(somedir) is conditionally defined.  However we don't do
# that for MKDIR_P.
#  1. before we restricted the check to GNU mkdir, `mkdir -p .' was
#     reported to fail in read-only directories.  The system where this
#     happened has been forgotten.
#  2. in practice we call $(MKDIR_P) on directories such as
#       $(MKDIR_P) "$(DESTDIR)$(somedir)"
#     and we don't want to create $(DESTDIR) if $(somedir) is empty.
#     To support the latter case, we have to write
#       test -z "$(somedir)" || $(MKDIR_P) "$(DESTDIR)$(somedir)"
#     so $(MKDIR_P) always has an argument.
#     We will have better chances of detecting a missing test if
#     $(MKDIR_P) complains about missing arguments.
#  3. $(MKDIR_P) is named after `mkdir -p' and we don't expect this
#     to accept no argument.
#  4. having something like `mkdir .' in the output is unsightly.
#
# On NextStep and OpenStep, the `mkdir' command does not
# recognize any option.  It will interpret all options as
# directories to create.
AN_MAKEVAR([MKDIR_P], [AC_PROG_MKDIR_P])
AC_DEFUN_ONCE([AC_PROG_MKDIR_P],
[AC_REQUIRE([AC_CONFIG_AUX_DIR_DEFAULT])dnl

AC_MSG_CHECKING([for a thread-safe mkdir -p])
if test -z "$MKDIR_P"; then
  AC_CACHE_VAL([ac_cv_path_mkdir],
    [_AS_PATH_WALK([$PATH$PATH_SEPARATOR/opt/sfw/bin],
      [for ac_prog in mkdir gmkdir; do
	 for ac_exec_ext in '' $ac_executable_extensions; do
	   AS_EXECUTABLE_P(["$as_dir/$ac_prog$ac_exec_ext"]) || continue
	   case `"$as_dir/$ac_prog$ac_exec_ext" --version 2>&1` in #(
	     'mkdir (GNU coreutils) '* | \
	     'mkdir (coreutils) '* | \
	     'mkdir (fileutils) '4.1*)
	       ac_cv_path_mkdir=$as_dir/$ac_prog$ac_exec_ext
	       break 3;;
	   esac
	 done
       done])])
  test -d ./--version && rmdir ./--version
  if test "${ac_cv_path_mkdir+set}" = set; then
    MKDIR_P="$ac_cv_path_mkdir -p"
  else
    # As a last resort, use the slow shell script.  Don't cache a
    # value for MKDIR_P within a source directory, because that will
    # break other packages using the cache if that directory is
    # removed, or if the value is a relative name.
    MKDIR_P="$ac_install_sh -d"
  fi
fi
dnl status.m4 does special magic for MKDIR_P instead of AC_SUBST,
dnl to get relative names right.  However, also AC_SUBST here so
dnl that Automake versions before 1.10 will pick it up (they do not
dnl trace AC_SUBST_TRACE).
dnl FIXME: Remove this once we drop support for Automake < 1.10.
AC_SUBST([MKDIR_P])dnl
AC_MSG_RESULT([$MKDIR_P])
])# AC_PROG_MKDIR_P


# From automake/m4/mkdirp.m4
##                                                          -*- Autoconf -*-
# Copyright (C) 2003, 2004, 2005, 2006  Free Software Foundation, Inc.
#
# This file is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

# AM_PROG_MKDIR_P
# ---------------
# Check for `mkdir -p'.
AC_DEFUN([AM_PROG_MKDIR_P],
[
AC_REQUIRE([AC_PROG_MKDIR_P])dnl
dnl Automake 1.8 to 1.9.6 used to define mkdir_p.  We now use MKDIR_P,
dnl while keeping a definition of mkdir_p for backward compatibility.
dnl @MKDIR_P@ is magic: AC_OUTPUT adjusts its value for each Makefile.
dnl However we cannot define mkdir_p as $(MKDIR_P) for the sake of
dnl Makefile.ins that do not define MKDIR_P, so we do our own
dnl adjustment using top_builddir (which is defined more often than
dnl MKDIR_P).
AC_SUBST([mkdir_p], ["$MKDIR_P"])dnl
case $mkdir_p in
  [[\\/$]]* | ?:[[\\/]]*) ;;
  */*) mkdir_p="\$(top_builddir)/$mkdir_p" ;;
esac
])
