(debug-enable 'backtrace)
; say hi to the caller.
(define (gf-hi str)
  " Say Hi "
  (display (string-append "Hi "str)))

(add-hook! gf-hi-hook gf-hi)