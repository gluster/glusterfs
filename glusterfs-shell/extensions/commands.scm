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

; process commands
(define (gf-command-proc command args)
  (if (not args)
      (set! args ""))
  ((lambda (cmd-entry args-entry)
     (if cmd-entry
	 (begin
	   (display (string-append "command is " cmd-entry))
	   (newline)
	   (display (string-append "arguments are " args-entry))
	   (newline)))) command args))

(add-hook! gf-command-hook gf-command-proc)
