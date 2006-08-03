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
;(gfysh-command-proc "gowda" "shaata")