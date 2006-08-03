#!./glusterfs-shell -s
!#
(display (gf-read (gf-open (gf-init "/home/benkicode/volume.spec") "/bot.txt" O_RDONLY) 100 0))