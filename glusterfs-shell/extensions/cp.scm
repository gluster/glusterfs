#!./glusterfs-shell -s
!#
(define (cp-files)
  (let ((volume (gf-init "/home/benkicode/volume.spec")))
    (let ((rctxt  (gf-open volume "/scrap/os-test.py" O_RDONLY))
	  (wctxt  (gf-open volume "/scrap/filtered-calls-created" (logior O_CREAT O_WRONLY))))
      (let ((readbuf (gf-read rctxt 1000 0)))
	(gf-write wctxt readbuf (string-length readbuf) 0)))))

(cp-files)