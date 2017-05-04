/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


%{

#include "glusterfsd.h"
#include "y.tab.h"
#include <string.h>

%}
CHROOT_     chroot
SCRATCH_    scratch
LISTEN_PORT listen
KEY_LEN     key-len
INTERCONNECT_PROTOCOL interconnect-protocol
DIR_        dir
%%
\#.*                  ;
{CHROOT_}[-]{DIR_}       return CHROOT;
{SCRATCH_}[-]{DIR_}      return SCRATCH;
{KEY_LEN}             return KEY_LENGTH;
{LISTEN_PORT}         return PORT;
{INTERCONNECT_PROTOCOL} return PROTOCOL;
[a-zA-Z0-9_\./:\-]+      {cclval = (int)strdup (cctext) ; return ID; }
[ \t\n]+              ;
%%
