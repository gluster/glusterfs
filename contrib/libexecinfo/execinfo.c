/*
 * Copyright (c) 2003 Maxim Sobolev <sobomax@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: execinfo.c,v 1.3 2004/07/19 05:21:09 sobomax Exp $
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifndef HAVE_BACKTRACE
#include <sys/types.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>

#include "execinfo_compat.h"

#define D10(x) ceil(log10(((x) == 0) ? 2 : ((x) + 1)))

static void *
getreturnaddr(int level)
{
        switch (level) {
        case 0: return __builtin_return_address(1);
        case 1: return __builtin_return_address(2);
        case 2: return __builtin_return_address(3);
        case 3: return __builtin_return_address(4);
        case 4: return __builtin_return_address(5);
        case 5: return __builtin_return_address(6);
        case 6: return __builtin_return_address(7);
        case 7: return __builtin_return_address(8);
        case 8: return __builtin_return_address(9);
        case 9: return __builtin_return_address(10);
        case 10: return __builtin_return_address(11);
        case 11: return __builtin_return_address(12);
        case 12: return __builtin_return_address(13);
        case 13: return __builtin_return_address(14);
        case 14: return __builtin_return_address(15);
        case 15: return __builtin_return_address(16);
        case 16: return __builtin_return_address(17);
        case 17: return __builtin_return_address(18);
        case 18: return __builtin_return_address(19);
        case 19: return __builtin_return_address(20);
        case 20: return __builtin_return_address(21);
        case 21: return __builtin_return_address(22);
        case 22: return __builtin_return_address(23);
        case 23: return __builtin_return_address(24);
        case 24: return __builtin_return_address(25);
        case 25: return __builtin_return_address(26);
        case 26: return __builtin_return_address(27);
        case 27: return __builtin_return_address(28);
        case 28: return __builtin_return_address(29);
        case 29: return __builtin_return_address(30);
        case 30: return __builtin_return_address(31);
        case 31: return __builtin_return_address(32);
        case 32: return __builtin_return_address(33);
        case 33: return __builtin_return_address(34);
        case 34: return __builtin_return_address(35);
        case 35: return __builtin_return_address(36);
        case 36: return __builtin_return_address(37);
        case 37: return __builtin_return_address(38);
        case 38: return __builtin_return_address(39);
        case 39: return __builtin_return_address(40);
        case 40: return __builtin_return_address(41);
        case 41: return __builtin_return_address(42);
        case 42: return __builtin_return_address(43);
        case 43: return __builtin_return_address(44);
        case 44: return __builtin_return_address(45);
        case 45: return __builtin_return_address(46);
        case 46: return __builtin_return_address(47);
        case 47: return __builtin_return_address(48);
        case 48: return __builtin_return_address(49);
        case 49: return __builtin_return_address(50);
        case 50: return __builtin_return_address(51);
        case 51: return __builtin_return_address(52);
        case 52: return __builtin_return_address(53);
        case 53: return __builtin_return_address(54);
        case 54: return __builtin_return_address(55);
        case 55: return __builtin_return_address(56);
        case 56: return __builtin_return_address(57);
        case 57: return __builtin_return_address(58);
        case 58: return __builtin_return_address(59);
        case 59: return __builtin_return_address(60);
        case 60: return __builtin_return_address(61);
        case 61: return __builtin_return_address(62);
        case 62: return __builtin_return_address(63);
        case 63: return __builtin_return_address(64);
        case 64: return __builtin_return_address(65);
        case 65: return __builtin_return_address(66);
        case 66: return __builtin_return_address(67);
        case 67: return __builtin_return_address(68);
        case 68: return __builtin_return_address(69);
        case 69: return __builtin_return_address(70);
        case 70: return __builtin_return_address(71);
        case 71: return __builtin_return_address(72);
        case 72: return __builtin_return_address(73);
        case 73: return __builtin_return_address(74);
        case 74: return __builtin_return_address(75);
        case 75: return __builtin_return_address(76);
        case 76: return __builtin_return_address(77);
        case 77: return __builtin_return_address(78);
        case 78: return __builtin_return_address(79);
        case 79: return __builtin_return_address(80);
        case 80: return __builtin_return_address(81);
        case 81: return __builtin_return_address(82);
        case 82: return __builtin_return_address(83);
        case 83: return __builtin_return_address(84);
        case 84: return __builtin_return_address(85);
        case 85: return __builtin_return_address(86);
        case 86: return __builtin_return_address(87);
        case 87: return __builtin_return_address(88);
        case 88: return __builtin_return_address(89);
        case 89: return __builtin_return_address(90);
        case 90: return __builtin_return_address(91);
        case 91: return __builtin_return_address(92);
        case 92: return __builtin_return_address(93);
        case 93: return __builtin_return_address(94);
        case 94: return __builtin_return_address(95);
        case 95: return __builtin_return_address(96);
        case 96: return __builtin_return_address(97);
        case 97: return __builtin_return_address(98);
        case 98: return __builtin_return_address(99);
        case 99: return __builtin_return_address(100);
        case 100: return __builtin_return_address(101);
        case 101: return __builtin_return_address(102);
        case 102: return __builtin_return_address(103);
        case 103: return __builtin_return_address(104);
        case 104: return __builtin_return_address(105);
        case 105: return __builtin_return_address(106);
        case 106: return __builtin_return_address(107);
        case 107: return __builtin_return_address(108);
        case 108: return __builtin_return_address(109);
        case 109: return __builtin_return_address(110);
        case 110: return __builtin_return_address(111);
        case 111: return __builtin_return_address(112);
        case 112: return __builtin_return_address(113);
        case 113: return __builtin_return_address(114);
        case 114: return __builtin_return_address(115);
        case 115: return __builtin_return_address(116);
        case 116: return __builtin_return_address(117);
        case 117: return __builtin_return_address(118);
        case 118: return __builtin_return_address(119);
        case 119: return __builtin_return_address(120);
        case 120: return __builtin_return_address(121);
        case 121: return __builtin_return_address(122);
        case 122: return __builtin_return_address(123);
        case 123: return __builtin_return_address(124);
        case 124: return __builtin_return_address(125);
        case 125: return __builtin_return_address(126);
        case 126: return __builtin_return_address(127);
        case 127: return __builtin_return_address(128);
        default: return NULL;
        }
}

static void *
getframeaddr(int level)
{

        switch (level) {
        case 0: return __builtin_frame_address(1);
        case 1: return __builtin_frame_address(2);
        case 2: return __builtin_frame_address(3);
        case 3: return __builtin_frame_address(4);
        case 4: return __builtin_frame_address(5);
        case 5: return __builtin_frame_address(6);
        case 6: return __builtin_frame_address(7);
        case 7: return __builtin_frame_address(8);
        case 8: return __builtin_frame_address(9);
        case 9: return __builtin_frame_address(10);
        case 10: return __builtin_frame_address(11);
        case 11: return __builtin_frame_address(12);
        case 12: return __builtin_frame_address(13);
        case 13: return __builtin_frame_address(14);
        case 14: return __builtin_frame_address(15);
        case 15: return __builtin_frame_address(16);
        case 16: return __builtin_frame_address(17);
        case 17: return __builtin_frame_address(18);
        case 18: return __builtin_frame_address(19);
        case 19: return __builtin_frame_address(20);
        case 20: return __builtin_frame_address(21);
        case 21: return __builtin_frame_address(22);
        case 22: return __builtin_frame_address(23);
        case 23: return __builtin_frame_address(24);
        case 24: return __builtin_frame_address(25);
        case 25: return __builtin_frame_address(26);
        case 26: return __builtin_frame_address(27);
        case 27: return __builtin_frame_address(28);
        case 28: return __builtin_frame_address(29);
        case 29: return __builtin_frame_address(30);
        case 30: return __builtin_frame_address(31);
        case 31: return __builtin_frame_address(32);
        case 32: return __builtin_frame_address(33);
        case 33: return __builtin_frame_address(34);
        case 34: return __builtin_frame_address(35);
        case 35: return __builtin_frame_address(36);
        case 36: return __builtin_frame_address(37);
        case 37: return __builtin_frame_address(38);
        case 38: return __builtin_frame_address(39);
        case 39: return __builtin_frame_address(40);
        case 40: return __builtin_frame_address(41);
        case 41: return __builtin_frame_address(42);
        case 42: return __builtin_frame_address(43);
        case 43: return __builtin_frame_address(44);
        case 44: return __builtin_frame_address(45);
        case 45: return __builtin_frame_address(46);
        case 46: return __builtin_frame_address(47);
        case 47: return __builtin_frame_address(48);
        case 48: return __builtin_frame_address(49);
        case 49: return __builtin_frame_address(50);
        case 50: return __builtin_frame_address(51);
        case 51: return __builtin_frame_address(52);
        case 52: return __builtin_frame_address(53);
        case 53: return __builtin_frame_address(54);
        case 54: return __builtin_frame_address(55);
        case 55: return __builtin_frame_address(56);
        case 56: return __builtin_frame_address(57);
        case 57: return __builtin_frame_address(58);
        case 58: return __builtin_frame_address(59);
        case 59: return __builtin_frame_address(60);
        case 60: return __builtin_frame_address(61);
        case 61: return __builtin_frame_address(62);
        case 62: return __builtin_frame_address(63);
        case 63: return __builtin_frame_address(64);
        case 64: return __builtin_frame_address(65);
        case 65: return __builtin_frame_address(66);
        case 66: return __builtin_frame_address(67);
        case 67: return __builtin_frame_address(68);
        case 68: return __builtin_frame_address(69);
        case 69: return __builtin_frame_address(70);
        case 70: return __builtin_frame_address(71);
        case 71: return __builtin_frame_address(72);
        case 72: return __builtin_frame_address(73);
        case 73: return __builtin_frame_address(74);
        case 74: return __builtin_frame_address(75);
        case 75: return __builtin_frame_address(76);
        case 76: return __builtin_frame_address(77);
        case 77: return __builtin_frame_address(78);
        case 78: return __builtin_frame_address(79);
        case 79: return __builtin_frame_address(80);
        case 80: return __builtin_frame_address(81);
        case 81: return __builtin_frame_address(82);
        case 82: return __builtin_frame_address(83);
        case 83: return __builtin_frame_address(84);
        case 84: return __builtin_frame_address(85);
        case 85: return __builtin_frame_address(86);
        case 86: return __builtin_frame_address(87);
        case 87: return __builtin_frame_address(88);
        case 88: return __builtin_frame_address(89);
        case 89: return __builtin_frame_address(90);
        case 90: return __builtin_frame_address(91);
        case 91: return __builtin_frame_address(92);
        case 92: return __builtin_frame_address(93);
        case 93: return __builtin_frame_address(94);
        case 94: return __builtin_frame_address(95);
        case 95: return __builtin_frame_address(96);
        case 96: return __builtin_frame_address(97);
        case 97: return __builtin_frame_address(98);
        case 98: return __builtin_frame_address(99);
        case 99: return __builtin_frame_address(100);
        case 100: return __builtin_frame_address(101);
        case 101: return __builtin_frame_address(102);
        case 102: return __builtin_frame_address(103);
        case 103: return __builtin_frame_address(104);
        case 104: return __builtin_frame_address(105);
        case 105: return __builtin_frame_address(106);
        case 106: return __builtin_frame_address(107);
        case 107: return __builtin_frame_address(108);
        case 108: return __builtin_frame_address(109);
        case 109: return __builtin_frame_address(110);
        case 110: return __builtin_frame_address(111);
        case 111: return __builtin_frame_address(112);
        case 112: return __builtin_frame_address(113);
        case 113: return __builtin_frame_address(114);
        case 114: return __builtin_frame_address(115);
        case 115: return __builtin_frame_address(116);
        case 116: return __builtin_frame_address(117);
        case 117: return __builtin_frame_address(118);
        case 118: return __builtin_frame_address(119);
        case 119: return __builtin_frame_address(120);
        case 120: return __builtin_frame_address(121);
        case 121: return __builtin_frame_address(122);
        case 122: return __builtin_frame_address(123);
        case 123: return __builtin_frame_address(124);
        case 124: return __builtin_frame_address(125);
        case 125: return __builtin_frame_address(126);
        case 126: return __builtin_frame_address(127);
        case 127: return __builtin_frame_address(128);
        default: return NULL;
        }
}

static inline void *
realloc_safe(void *ptr, size_t size)
{
        void *nptr;

        nptr = realloc (ptr, size);
        if (nptr == NULL)
                free (ptr);
        return nptr;
}

int
backtrace(void **buffer, int size)
{
        int i;

        for (i = 1; getframeaddr(i + 1) != NULL && i != size + 1; i++) {
                buffer[i - 1] = getreturnaddr(i);
                if (buffer[i - 1] == NULL)
                        break;
        }
        return i - 1;
}

char **
backtrace_symbols(void *const *buffer, int size)
{
        size_t clen, alen;
        int i, offset;
        char **rval;
        Dl_info info;

        clen = size * sizeof(char *);
        rval = malloc(clen);
        if (rval == NULL)
                return NULL;
        for (i = 0; i < size; i++) {
                if (dladdr(buffer[i], &info) != 0) {
                        if (info.dli_sname == NULL)
                                info.dli_sname = "???";
                        if (info.dli_saddr == NULL)
                                info.dli_saddr = buffer[i];
                        offset = buffer[i] - info.dli_saddr;
                        /* "0x01234567 <function+offset> at filename" */
                        alen = 2 +                      /* "0x" */
                                (sizeof(void *) * 2) +   /* "01234567" */
                                2 +                      /* " <" */
                                strlen(info.dli_sname) + /* "function" */
                                1 +                      /* "+" */
                                10 +                     /* "offset */
                                5 +                      /* "> at " */
                                strlen(info.dli_fname) + /* "filename" */
                                1;                       /* "\0" */
                        rval = realloc_safe(rval, clen + alen);
                        if (rval == NULL)
                                return NULL;
                        snprintf((char *) rval + clen, alen, "%p <%s+%d> at %s",
                                 buffer[i], info.dli_sname, offset, info.dli_fname);
                } else {
                        alen = 2 +                      /* "0x" */
                                (sizeof(void *) * 2) +   /* "01234567" */
                                1;                       /* "\0" */
                        rval = realloc_safe(rval, clen + alen);
                        if (rval == NULL)
                                return NULL;
                        snprintf((char *) rval + clen, alen, "%p", buffer[i]);
                }
                rval[i] = (char *) clen;
                clen += alen;
        }

        for (i = 0; i < size; i++)
                rval[i] += (long) rval;

        return rval;
}

void
backtrace_symbols_fd(void *const *buffer, int size, int fd)
{
        int i, len, offset;
        char *buf;
        Dl_info info;

        for (i = 0; i < size; i++) {
                if (dladdr(buffer[i], &info) != 0) {
                        if (info.dli_sname == NULL)
                                info.dli_sname = "???";
                        if (info.dli_saddr == NULL)
                                info.dli_saddr = buffer[i];
                        offset = buffer[i] - info.dli_saddr;
                        /* "0x01234567 <function+offset> at filename" */
                        len = 2 +                      /* "0x" */
                                (sizeof(void *) * 2) +   /* "01234567" */
                                2 +                      /* " <" */
                                strlen(info.dli_sname) + /* "function" */
                                1 +                      /* "+" */
                                D10(offset) +            /* "offset */
                                5 +                      /* "> at " */
                                strlen(info.dli_fname) + /* "filename" */
                                2;                       /* "\n\0" */
                        buf = alloca(len);
                        if (buf == NULL)
                                return;
                        snprintf(buf, len, "%p <%s+%d> at %s\n",
                                 buffer[i], info.dli_sname, offset, info.dli_fname);
                } else {
                        len = 2 +                      /* "0x" */
                                (sizeof(void *) * 2) +   /* "01234567" */
                                2;                       /* "\n\0" */
                        buf = alloca(len);
                        if (buf == NULL)
                                return;
                        snprintf(buf, len, "%p\n", buffer[i]);
                }
                if (write(fd, buf, strlen(buf)) == -1)
                        return;
        }
}
#endif
