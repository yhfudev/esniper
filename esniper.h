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

#ifndef ESNIPER_H_INCLUDED
#define ESNIPER_H_INCLUDED

#include "options.h"
#include "util.h"

/* minimum bid time, in seconds before end of auction */
#define MIN_BIDTIME 5
/* default bid time */
#define DEFAULT_BIDTIME 10

extern const char DEFAULT_CONF_FILE[];
extern const char HOSTNAME[];
extern const char BID_HOSTNAME[];

/* this structure holds all values from command line or config entries */
typedef struct {
	char *username;
	char *password;
	int bidtime;
	int quantity;
	char *conffilename;
	char *auctfilename;
	int bid;
	int reduce;
	int debug;
	int usage;
	int batch;
	int encrypted;
	proxy_t proxy;
	char *logdir;
} option_t;

extern option_t options;

#define log(x) if(!options.debug);else dlog x

#endif /* ESNIPER_H_INCLUDED */
