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

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfsd.log"

#define GLUSTERFSD_SPEC_DIR    "/var/state/glusterfs"
#define GLUSTERFSD_SPEC_PATH   "/var/state/glusterfs/client-volume.spec"

#define GF_YES 1
#define GF_NO  0


struct held_locks {
  struct held_locks *next;
  int8_t *path;
};

/* private structure per socket (transport object)
   used as transport_t->xl_private
 */

struct proto_srv_priv {
  dict_t *fctxl;
  struct held_locks *locks;
  struct xlator *bound_xl; /* to be set after an authenticated SETVOLUME */
};



