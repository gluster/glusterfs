#!./glusterfs-shell -s
!#
;(display (gf-mkdir (gf-init "/home/benkicode/volume.spec") "/gowda-created-this-dir"))
(display (gf-mknod (gf-init "/home/benkicode/volume.spec") "/sha-node" 'char-special #o660 (+ (* 2 256) 2)))