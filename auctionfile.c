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

/*
 * This program will "snipe" an auction on eBay, automatically placing
 * your bid a few seconds before the auction ends.
 *
 * For updates, bug reports, etc, please go to esniper.sourceforge.net.
 */

#include "auctionfile.h"
#include "buffer.h"
#include "util.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * readAuctionFile(): read a file listing auctions to watch.
 *
 * returns: number of auctions to watch
 */
int
readAuctionFile(const char *filename, auctionInfo ***aip)
{
	FILE *fp = fopen(filename, "r");
	char *buf = NULL;
	size_t bufsize = 0, count = 0;
	int c, i, j, line, numAuctions = 0;

	if (fp == NULL) {
		fprintf(stderr, "Cannot open auction file %s: %s\n", filename,
			strerror(errno));
		return -1;
	}

	while (numAuctions != -1 && (c = getc(fp)) != EOF) {
		if (isspace(c))
			continue;
		/* skip comments and anything starting with a letter,
		 * assuming this is a config entry */
		if ((c == '#') || isalpha(c))
			c = skipline(fp);
		else if (isdigit(c)) {
			++numAuctions;
			/* get auction number */
			line = count;
			do {
				addchar(buf, bufsize, count, c);
			} while (isdigit(c = getc(fp)));
			for (; isspace(c) && c != '\n'; c = getc(fp))
				;
			if (c == '#')	/* comment? */
				c = skipline(fp);
			/* no price? */
			if (c == EOF || c == '\n') {
				/* use price of previous auction */
				if (numAuctions == 1) {
					fprintf(stderr, "Cannot find price on first auction\n");
					numAuctions = -1;
				}
			} else {
				addchar(buf, bufsize, count, ' ');
				/* get price */
				for (; isdigit(c) || c == '.'; c = getc(fp))
					addchar(buf, bufsize, count, c);
				for (; isspace(c) && c != '\n'; c = getc(fp))
					;
				if (c == '#')	/* comment? */
					c = skipline(fp);
				if (c != EOF && c != '\n') {
					term(buf, bufsize, count);
					fprintf(stderr, "Invalid auction line: %s\n", &buf[line]);
					numAuctions = -1;
				}
			}
			addchar(buf, bufsize, count, '\n');
		} else {
			fprintf(stderr, "Invalid auction line: ");
			do {
				putc(c, stderr);
			} while ((c = getc(fp)) != EOF && c != '\n');
			putc('\n', stderr);
			numAuctions = -1;
		}
		if (c == EOF)
			break;
	}
	fclose(fp);

	if (numAuctions > 0) {
		*aip = (auctionInfo **)myMalloc(
					sizeof(auctionInfo *) * numAuctions);

		for (i = 0, j = 0; i < numAuctions; ++i, ++j) {
			char *auction, *bidPriceStr;

			auction = &buf[j];
			for (; !isspace((int)(buf[j])); ++j)
				;
			if (buf[j] == '\n') {
				buf[j] = '\0';
				bidPriceStr = (*aip)[i-1]->bidPriceStr;
			} else {
				buf[j] = '\0';
				bidPriceStr = &buf[++j];
				for (; buf[j] != '\n'; ++j)
					;
				buf[j] = '\0';
			}
			(*aip)[i] = newAuctionInfo(auction, bidPriceStr);
		}
	} else if (numAuctions == 0)
		fprintf(stderr, "Cannot find any auctions!\n");

	free(buf);

	return numAuctions;
} /* readAuctions() */
