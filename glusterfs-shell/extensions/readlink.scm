#!./glusterfs-shell -s
!#
(display (gf-readlink (gf-init "/home/benkicode/volume.spec") "/mylink"))
(newline)