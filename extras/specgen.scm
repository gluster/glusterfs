#!/usr/bin/guile -s
!#

;;; Copyright (c) 2007-2011 Gluster Inc. <http://www.gluster.com>
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


(define (volume args)
  (apply
   (lambda (name type options)
     (lambda args
       (display "volume ") (display name) (newline)
       (display "  type ") (display type) (newline)
       (map (lambda (key-value-cons)
	      (let ((key (car key-value-cons))
		    (value (cdr key-value-cons)))
		(display "  option ") (display key) (display " ")
		(display value) (newline)))
	    options)
       (if (> (length args) 0)
	   (begin
	     (display "  subvolumes ")
	     (map (lambda (subvol)
		    (display subvol) (display " "))
		  args)
	     (newline)))
       (display "end-volume") (newline) (newline)
       name))
   args))

;; define volumes with names/type/options and bind to a symbol
;; relate them seperately (see below)
;; more convinient to seperate volume definition and relation

(define wb (volume '(wb0
		     performance/write-behind
		     ((aggregate-size . 0)
		      (flush-behind . off)
		      ))))

(define ra (volume '(ra0
		     performance/read-ahead
		     ((page-size . 128KB)
		      (page-count . 1)
		      ))))

(define ioc (volume '(ioc0
		      performance/io-cache
		      ((page-size . 128KB)
		       (cache-size . 64MB)
		      ))))

(define iot (volume '(iot0
		      performance/io-threads
		      ()
		      )))

(define client1 (volume '(client1
			  protocol/client
			  ((transport-type . tcp/client)
			   (remote-host . localhost)
			   (remote-subvolume . brick1)
			   ))))

(define client2 (volume '(client2
			  protocol/client
			  ((transport-type . tcp/client)
			   (remote-host . localhost)
			   (remote-subvolume . brick2)
			   ))))

(define unify (volume '(unify0
			cluster/unify
			((scheduler . rr)
			 ))))

;; relate the symbols to output a spec file
;; note: relating with symbols lets you change volume name in one place

(wb (ra (ioc (iot (unify (client1)
			 (client2))))))
