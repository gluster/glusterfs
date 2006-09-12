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

#!./glusterfs-shell -s
!#
(define (cp-files)
  (let ((volume (gf-init "/home/benkicode/volume.spec")))
    (let ((rctxt  (gf-open volume "/scrap/os-test.py" O_RDONLY))
	  (wctxt  (gf-open volume "/scrap/filtered-calls-created" (logior O_CREAT O_WRONLY))))
      (let ((readbuf (gf-read rctxt 1000 0)))
	(gf-write wctxt readbuf (string-length readbuf) 0)))))

(cp-files)