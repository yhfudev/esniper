/*
 * Copyright (c) 2002, 2003, Scott Nicol <esniper@sourceforge.net>
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include "auctioninfo.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct {
	char *host;
	int port;
} proxy_t;

extern FILE *logfile;

extern void *myMalloc(size_t);
extern void *myRealloc(void *buf, size_t size);
extern char *myStrdup(const char *);
extern char *myStrndup(const char *, size_t len);
extern char *myStrdup2(const char *, const char *);
extern char *myStrdup3(const char *, const char *, const char *);
extern char *myStrdup4(const char *, const char *, const char *, const char *);

extern void logClose(void);
extern void logOpen(const char *progname, const auctionInfo *aip);
extern void vlog(const char *fmt, va_list arglist);
extern void dlog(const char *fmt, ...);
extern void printLog(FILE *fp, const char *fmt, ...);
extern void logChar(int c);

extern const char *nullStr(const char *);
extern char *timestamp(void);
extern int skipline(FILE *fp);
extern char *prompt(const char *p, int noecho);
extern int boolValue(const char *value);
extern int parseProxy(const char *value, proxy_t *proxy);

extern void clearPassword(void);
extern void encryptPassword(void);
extern void decryptPassword(void);

#if defined(__CYGWIN__) || defined(WIN32)
extern char *basename(char *);
extern char *dirname(char *);
#else
#include <libgen.h>
#endif

#endif /* UTIL_H_INCLUDED */
