" glusterfs.vim: GNU Vim Syntax file for GlusterFS .vol specification
"  Copyright (c) 2007777777Red Hat, Inc. <http://www.redhat.com>
"  This file is part of GlusterFS.
"
"  This file is licensed to you under your choice of the GNU Lesser
"  General Public License, version 3 or any later version (LGPLv3 or
"  later), or the GNU General Public License, version 2 (GPLv2), in all
"  cases as published by the Free Software Foundation.
"
" Last Modified: Wed Aug  1 00:47:10 IST 2007
" Version: 0.8 

syntax clear
syntax case match

setlocal iskeyword+=-
setlocal iskeyword+=%
setlocal iskeyword+=.
setlocal iskeyword+=*
setlocal iskeyword+=:
setlocal iskeyword+=,


"************************************************************************
" Initially, consider everything an error. Then start eliminating one
"   field after the other. Whatever is not eliminated (due to defined
"   properties) is an error - Multiples Values for a key
"************************************************************************
syn match glusterfsError /[^ 	]\+/ skipwhite
syn match glusterfsComment "#.*" contains=glusterfsTodo

syn keyword	glusterfsTodo	contained TODO FIXME NOTE

"------------------------------------------------------------------------
" 'Type' Begin
"------------------------------------------------------------------------
" Handle all the 'Type' keys and values. Here, a '/' is used to separate
" the key-value pair, they are clubbed together for convenience
syn match glusterfsType "^\s*type\s\+" skipwhite nextgroup=glusterfsTypeKeyVal

syn match glusterfsTypeKeyVal contained "\<protocol/\(client\|server\)\>"
syn match glusterfsTypeKeyVal contained "\<cluster/\(unify\|afr\|stripe\)\>"
syn match glusterfsTypeKeyVal contained "\<debug/\(trace\)\>"
syn match glusterfsTypeKeyVal contained "\<encryption/\(rot-13\)\>"
syn match glusterfsTypeKeyVal contained "\<storage/\(posix\)\>"
"syn match glusterfsTypeKeyVal contained "\<features/\(trash\)\>"
syn match glusterfsTypeKeyVal contained "\<features/\(trash\|posix-locks\|fixed-id\|filter\)\>"
syn match glusterfsTypeKeyVal contained "\<performance/\(io-threads\|write-behind\|io-cache\|read-ahead\)\>"
"------------------------------------------------------------------------
" 'Type' End
"------------------------------------------------------------------------


"************************************************************************

"------------------------------------------------------------------------
" 'Volume' Begin
"------------------------------------------------------------------------
" NOTE 1: Only one volume name allowed after 'volume' keyword
" NOTE 2: Multiple volumes allowed after 'subvolumes'
" NOTE 3: Some other options (like remote-subvolume, namespace etc) use
"   volume name (single)
syn match glusterfsVol "^\s*volume\s\+" nextgroup=glusterfsVolName
syn match glusterfsVolName "\<\k\+" contained

syn match glusterfsVol "^\s*subvolumes\s\+" skipwhite nextgroup=glusterfsSubVolName
syn match glusterfsSubVolName "\<\k\+\>" skipwhite contained nextgroup=glusterfsSubVolName

syn match glusterfsVol "^\s*end-volume\>"
"------------------------------------------------------------------------
" 'Volume' End
"------------------------------------------------------------------------





"------------------------------------------------------------------------
" 'Options' Begin
"------------------------------------------------------------------------
syn match glusterfsOpt "^\s*option\s\+" nextgroup=glusterfsOptKey


syn keyword glusterfsOptKey contained transport-type skipwhite nextgroup=glusterfsOptValTransportType
syn match glusterfsOptValTransportType contained "\<\(tcp\|ib\-verbs\|ib-sdp\)/\(client\|server\)\>"

syn keyword glusterfsOptKey contained remote-subvolume skipwhite nextgroup=glusterfsVolName

syn keyword glusterfsOptKey contained auth.addr.ra8.allow auth.addr.ra7.allow auth.addr.ra6.allow auth.addr.ra5.allow auth.addr.ra4.allow auth.addr.ra3.allow auth.addr.ra2.allow auth.addr.ra1.allow auth.addr.brick-ns.allow skipwhite nextgroup=glusterfsOptVal

syn keyword glusterfsOptKey contained client-volume-filename directory trash-dir skipwhite nextgroup=glusterfsOpt_Path
syn match glusterfsOpt_Path contained "\s\+\f\+\>"

syn keyword glusterfsOptKey contained debug self-heal encrypt-write decrypt-read mandatory nextgroup=glusterfsOpt_OnOff
syn match glusterfsOpt_OnOff contained "\s\+\(on\|off\)\>"

syn keyword glusterfsOptKey contained flush-behind non-blocking-connect nextgroup=glusterfsOpt_OnOffNoYes
syn keyword glusterfsOpt_OnOffNoYes contained on off no yes

syn keyword glusterfsOptKey contained page-size cache-size nextgroup=glusterfsOpt_Size

syn keyword glusterfsOptKey contained fixed-gid fixed-uid cache-seconds page-count thread-count aggregate-size listen-port remote-port transport-timeout inode-lru-limit nextgroup=glusterfsOpt_Number

syn keyword glusterfsOptKey contained alu.disk-usage.entry-threshold alu.disk-usage.exit-threshold nextgroup=glusterfsOpt_Size

syn keyword glusterfsOptKey contained alu.order skipwhite nextgroup=glusterfsOptValAluOrder
syn match glusterfsOptValAluOrder contained "\s\+\(\(disk-usage\|write-usage\|read-usage\|open-files-usage\|disk-speed\):\)*\(disk-usage\|write-usage\|read-usage\|open-files-usage\|disk-speed\)\>"

syn keyword glusterfsOptKey contained alu.open-files-usage.entry-threshold alu.open-files-usage.exit-threshold alu.limits.max-open-files rr.refresh-interval random.refresh-interval nufa.refresh-interval nextgroup=glusterfsOpt_Number

syn keyword glusterfsOptKey contained nufa.local-volume-name skipwhite nextgroup=glusterfsVolName

syn keyword glusterfsOptKey contained ib-verbs-work-request-send-size ib-verbs-work-request-recv-size nextgroup=glusterfsOpt_Size
syn match glusterfsOpt_Size contained "\s\+\d\+\([gGmMkK][bB]\)\=\>"

syn keyword glusterfsOptKey contained ib-verbs-work-request-send-count ib-verbs-work-request-recv-count ib-verbs-port nextgroup=glusterfsOpt_Number

syn keyword glusterfsOptKey contained ib-verbs-mtu nextgroup=glusterfsOptValIBVerbsMtu
syn match glusterfsOptValIBVerbsMtu "\s\+\(256\|512\|1024\|2048\|4096\)\>" contained

syn keyword glusterfsOptKey contained ib-verbs-device-name nextgroup=glusterfsOptVal

syn match glusterfsOpt_Number contained "\s\+\d\+\>"

syn keyword glusterfsOptKey contained scheduler skipwhite nextgroup=glusterfsOptValScheduler
syn keyword glusterfsOptValScheduler contained rr alu random nufa

syn keyword glusterfsOptKey contained namespace skipwhite nextgroup=glusterfsVolName

syn keyword glusterfsOptKey contained lock-node skipwhite nextgroup=glusterfsVolName



syn keyword glusterfsOptKey contained alu.write-usage.entry-threshold alu.write-usage.exit-threshold alu.read-usage.entry-threshold alu.read-usage.exit-threshold alu.limits.min-free-disk nextgroup=glusterfsOpt_Percentage

syn keyword glusterfsOptKey contained random.limits.min-free-disk nextgroup=glusterfsOpt_Percentage
syn keyword glusterfsOptKey contained rr.limits.min-disk-free nextgroup=glusterfsOpt_Size

syn keyword glusterfsOptKey contained nufa.limits.min-free-disk nextgroup=glusterfsOpt_Percentage

syn match glusterfsOpt_Percentage contained "\s\+\d\+%\=\>"









syn keyword glusterfsOptKey contained remote-host bind-address nextgroup=glusterfsOpt_IP,glusterfsOpt_Domain
syn match glusterfsOpt_IP contained "\s\+\d\d\=\d\=\.\d\d\=\d\=\.\d\d\=\d\=\.\d\d\=\d\=\>"
syn match glusterfsOpt_Domain contained "\s\+\a[a-zA-Z0-9_-]*\(\.\a\+\)*\>"

syn match glusterfsVolNames "\s*\<\S\+\>" contained skipwhite nextgroup=glusterfsVolNames

syn keyword glusterfsOptKey contained block-size replicate skipwhite nextgroup=glusterfsOpt_Pattern

syn match glusterfsOpt_Pattern contained "\s\+\k\+\>"
syn match glusterfsOptVal contained "\s\+\S\+\>"





hi link glusterfsError Error
hi link glusterfsComment Comment

hi link glusterfsVol keyword

hi link glusterfsVolName function
hi link glusterfsSubVolName function

hi link glusterfsType Keyword
hi link glusterfsTypeKeyVal String

hi link glusterfsOpt Keyword

hi link glusterfsOptKey Special
hi link glusterfsOptVal Normal

hi link glusterfsOptValTransportType String
hi link glusterfsOptValScheduler String
hi link glusterfsOptValAluOrder String
hi link glusterfsOptValIBVerbsMtu String

hi link glusterfsOpt_OnOff String
hi link glusterfsOpt_OnOffNoYes String


" Options that require
hi link glusterfsOpt_Size PreProc
hi link glusterfsOpt_Domain PreProc
hi link glusterfsOpt_Percentage PreProc
hi link glusterfsOpt_IP PreProc
hi link glusterfsOpt_Pattern PreProc
hi link glusterfsOpt_Number Preproc
hi link glusterfsOpt_Path Preproc



let b:current_syntax = "glusterfs"
