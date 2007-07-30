;;; Copyright (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
;;;  
;;; This program is free software; you can redistribute it and/or modify
;;; it under the terms of the GNU General Public License as published by
;;; the Free Software Foundation; either version 2 of the License, or
;;; (at your option) any later version.
;;;  
;;; This program is distributed in the hope that it will be useful,
;;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;;; GNU General Public License for more details.
;;;  
;;; You should have received a copy of the GNU General Public License
;;; along with this program; if not, write to the Free Software
;;; Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
;;;  

;;; This script lets you specify the xlator graph as a Scheme list
;;; and provides a function to generate the spec file for the graph.

(define (gen-options options)
  (define (gen-option option)
    (display "  option ") (display (car option)) (display " ") (display (cadr option))
    (newline))
  (map gen-option options))

(define (gen-subvolumes subs)
  (begin (display "  subvolumes ") 
	 (map (lambda (sub) (display (car sub)) (display " ")) subs)))
 
(define (gen-volume vol)
  (begin 
    (display "volume ") (display (car vol)) (newline)
    (display "  type ") (display (cadr vol)) (newline)
    (gen-options (caddr vol))
    (if (not (null? (cadddr vol)))
	(begin (gen-subvolumes (cadddr vol)) (newline)))
    (display "end-volume") (newline) (newline)))

(define (gen-volume-string vol)
  (with-output-to-string (lambda () (gen-volume vol))))

(define (gen-graph graph)
  (cons (gen-volume-string graph)
	(let ((rest (map (lambda (g) (gen-graph g))
			 (cadddr graph))))
	  (if (null? rest) rest
	      (if (= (length rest) 1) (car rest)
		  (map car rest))))))

(define (gen-spec graph)
  (apply string-append (reverse (gen-graph graph))))

;;; Example usage

;;; The format of the graph is:
;;; xlator ::= (xlator-name type ((option1 value1) (option2 value2) ...)
;;;              (<child xlator1> <child xlator2> ...))

(define server-graph '(server protocol/server ((transport-type tcp/server)
					       (auth.ip.locks.allow *)
					       (debug on))
			      ((locks features/posix-locks ((mandatory on) (debug on))
				      ((posix storage/posix ((directory /home/vikas/export) 
							     (debug on))
					      ()))))))

(define client-graph '(unify cluster/unify ((scheduler rr))
			     ((brick1 protocol/client ((remote-host localhost)
						       (remote-subvolume brick1))
				      ())
			      (brick2 protocol/client ((remote-host localhost)
						       (remote-port 6997)
						       (remote-subvolume brick2))
				      ()))))
(display (gen-spec server-graph))
(display (gen-spec client-graph))