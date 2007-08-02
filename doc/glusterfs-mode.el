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

(defvar glusterfsfs-mode-hook nil)

;; (defvar glusterfs-mode-map
;;   (let ((glusterfs-mode-map (make-keymap)))
;;     (define-key glusterfs-mode-map "\C-j" 'newline-and-indent)
;;     glusterfs-mode-map)
;;   "Keymap for WPDL major mode")

(add-to-list 'auto-mode-alist '("\\.vol\\'" . glusterfs-mode))

(defconst glusterfs-font-lock-keywords-1
  (list
   ; "option" "volume" "end-volume" "subvolumes" "type"
   '("\\<\\(option\\|volume\\|subvolumes\\|type\\|end-volume\\)\\>" . font-lock-builtin-face))
   ;'((regexp-opt (" option " "^volume " "^end-volume" "subvolumes " " type ") t) . font-lock-builtin-face))
  "Minimal highlighting expressions for GlusterFS mode.")

(defconst glusterfs-font-lock-keywords-2
  (append glusterfs-font-lock-keywords-1
	  (list
	; "cluster/{unify,afr,stripe}" 
	; "performance/{io-cache,io-threads,write-behind,read-ahead,stat-prefetch}"
	; "protocol/{client/server}"
	; "features/{trash,posix-locks,fixed-id,filter}"
	; "stroage/posix"
	; "encryption/rot-13"
        ; "debug/trace"
	   '("\\<\\(cluster/\\(unify\\|afr\\|stripe\\)\\|\\performance/\\(io-\\(cache\\|threads\\)\\|write-behind\\|read-ahead\\|stat-prefetch\\)\\|protocol/\\(server\\|client\\)\\|features/\\(trash\\|posix-locks\\|fixed-id\\|filter\\)\\|storage/posix\\|encryption/rot-13\\|debug/trace\\)\\>" . font-lock-keyword-face)))
  "Additional Keywords to highlight in GlusterFS mode.")

(defconst glusterfs-font-lock-keywords-3
  (append glusterfs-font-lock-keywords-2
	  (list
      ; "replicate" "namespace" "scheduler" "remote-subvolume" "remote-host" 
      ; "auth.ip" "block-size" "remote-port" "listen-port" "transport-type"
      ; "limits.min-free-disk" "directory"
	; TODO: add all the keys here.
	   '("\\<\\(replicate\\|\\namespace\\|scheduler\\|remote-\\(host\\|subvolume\\|port\\)\\|auth-ip\\|block-size\\|listen-port\\|transport-type\\|limits.min-free-disk\\|directory\\)\\>" . font-lock-constant-face)))
  "option keys in GlusterFS mode.")

(defvar glusterfs-font-lock-keywords glusterfs-font-lock-keywords-3
  "Default highlighting expressions for GlusterFS mode.")

(defvar glusterfs-mode-syntax-table
  (let ((glusterfs-mode-syntax-table (make-syntax-table)))
    ;; currently supports '//' as comment.. don't know how to add '#'
    (modify-syntax-entry ?\# "<"  glusterfs-mode-syntax-table)
    (modify-syntax-entry ?* ". 23"  glusterfs-mode-syntax-table)
    (modify-syntax-entry ?\n ">#"  glusterfs-mode-syntax-table)
    glusterfs-mode-syntax-table)
  "Syntax table for glusterfs-mode")

;; TODO: add an indentation table

(defun glusterfs-mode ()
  (interactive)
  (kill-all-local-variables)
  ;; (use-local-map glusterfs-mode-map)
  (set-syntax-table glusterfs-mode-syntax-table)
;;  (set (make-local-variable 'indent-line-function) 'glusterfs-indent-line)  
  (set (make-local-variable 'font-lock-defaults) '(glusterfs-font-lock-keywords))
  (setq major-mode 'glusterfs-mode)
  (setq mode-name "GlusterFS")
  (run-hooks 'glusterfs-mode-hook))

(provide 'glusterfs-mode)
