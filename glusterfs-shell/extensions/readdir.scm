#!./glusterfs-shell -s
!#
(debug-enable 'backtrace)
(display (gf-readdir (gf-init "/home/benkicode/volume.spec") "/forbes"))