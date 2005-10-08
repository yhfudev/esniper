/*
 * Copyright (c) 2002, 2003, 2004, Scott Nicol <esniper@users.sf.net>
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

#include "auction.h"
#include "buffer.h"
#include "http.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#if defined(WIN32)
#	define strcasecmp(s1, s2) stricmp((s1), (s2))
#	define sleep(t) _sleep((t) * 1000)
#	define strncasecmp(s1, s2, len) strnicmp((s1), (s2), (len))
#else
#	include <unistd.h>
#endif

#define newRemain(aip) (aip->endTime - time(NULL) - aip->latency - options.bidtime)

static time_t loginTime = 0;	/* Time of last login */

static int acceptBid(const char *pagename, auctionInfo *aip);
static int bid(auctionInfo *aip);
static int ebayLogin(auctionInfo *aip);
static void freeTableRow(char **row);
static char *getIdInternal(char *s, size_t len);
static int getInfoTiming(auctionInfo *aip, const char *user, time_t *timeToFirstByte);
static int getIntFromString(const char *s);
static char *getNonTag(memBuf_t *mp);
static char *getNonTagFromString(const char *s);
static char *getPageName(memBuf_t *mp);
static char *getPageNameInternal(char *var);
static int getQuantity(int want, int available);
static long getSeconds(char *timestr);
static char *getTableCell(memBuf_t *mp);
static const char *getTableEnd(memBuf_t *mp);
static char **getTableRow(memBuf_t *mp);
static const char *getTag(memBuf_t *mp);
static const char *getTableStart(memBuf_t *mp);
static int makeBidError(const char *pagename, auctionInfo *aip);
static int match(memBuf_t *mp, const char *str);
static int numColumns(char **row);
static int parseBidHistory(memBuf_t *mp, auctionInfo *aip, const char *user, time_t start, time_t *timeToFirstByte);
static int parseBid(memBuf_t *mp, auctionInfo *aip);
static int preBid(auctionInfo *aip);
static int printMyItemsRow(char **row, int printNewline);
static int watch(auctionInfo *aip);

/*
 * attempt to match some input, neglecting case, ignoring \r and \n.
 * returns 0 on success, -1 on failure
 */
static int
match(memBuf_t *mp, const char *str)
{
	const char *cursor;
	int c;

	log(("\n\nmatch(\"%s\")\n\n", str));

	cursor = str;
	while ((c = memGetc(mp)) != EOF) {
		if (options.debug)
			logChar(c);
		if (tolower(c) == (int)*cursor) {
			if (*++cursor == '\0') {
				if (options.debug)
					logChar(EOF);
				return 0;
			}
		} else if (c != '\n' && c != '\r')
			cursor = str;
	}
	if (options.debug)
		logChar(EOF);
	return -1;
}

static const char PAGENAME[] = "var pageName = \"";
static const char PAGEID[] = "Page id: ";
static const char SRCID[] = "srcId: ";

/*
 * Get pagename variable, or NULL if not found.
 */
static char *
getPageName(memBuf_t *mp)
{
	const char *line;

	log(("getPageName():\n"));
	while ((line = getTag(mp))) {
		char *tmp;

		if (strncmp(line, "!--", 3))
			continue;
		if ((tmp = strstr(line, PAGENAME))) {
			tmp = getPageNameInternal(tmp);
			log(("getPageName(): pagename = %s\n", nullStr(tmp)));
			return tmp;
		}
	}
	log(("getPageName(): Cannot find pagename, returning NULL\n"));
	return NULL;
}

/*
 * Get page info, including pagename variable, page id and srcid comments.
 */
pageInfo_t *
getPageInfo(memBuf_t *mp)
{
	const char *line;
	pageInfo_t p = {NULL, NULL, NULL}, *pp;
	int needPageName = 1;
	int needPageId = 1;
	int needSrcId = 1;
	int needMore = 3;

	log(("getPageInfo():\n"));
	while (needMore && (line = getTag(mp))) {
		char *tmp;

		if (strncmp(line, "!--", 3))
			continue;
		if (needPageName && (tmp = strstr(line, PAGENAME))) {
			if ((tmp = getPageNameInternal(tmp))) {
				--needMore;
				--needPageName;
				p.pageName = myStrdup(tmp);
			}
		} else if (needPageId && (tmp = strstr(line, PAGEID))) {
			if ((tmp = getIdInternal(tmp, sizeof(PAGEID)))) {
				--needMore;
				--needPageId;
				p.pageId = myStrdup(tmp);
			}
		} else if (needSrcId && (tmp = strstr(line, SRCID))) {
			if ((tmp = getIdInternal(tmp, sizeof(SRCID)))) {
				--needMore;
				--needSrcId;
				p.srcId = myStrdup(tmp);
			}
		}
	}
	log(("getPageInfo(): pageName = %s, pageId = %s, srcId = %s\n", nullStr(p.pageName), nullStr(p.pageId), nullStr(p.srcId)));
	if (needMore == 3)
		return NULL;
	pp = (pageInfo_t *)myMalloc(sizeof(pageInfo_t));
	pp->pageName = p.pageName;
	pp->pageId = p.pageId;
	pp->srcId = p.srcId;
	return pp;
}

static char *
getIdInternal(char *s, size_t len)
{
	char *id = s + len - 1;
	char *dash = strchr(id, '-');

	if (!*dash) {
		log(("getIdInternal(): Cannot find trailing dash: %s\n", id));
		return NULL;
	}
	*dash = '\0';
	log(("getIdInternal(): id = %s\n", id));
	return id;
}

static char *
getPageNameInternal(char *s)
{
	char *pagename = s + sizeof(PAGENAME) - 1;
	char *quote = strchr(pagename, '"');

	if (!*quote) {
		log(("getPageNameInternal(): Cannot find trailing quote in pagename: %s\n", pagename));
		return NULL;
	}
	*quote = '\0';
	log(("getPageName(): pagename = %s\n", pagename));
	return pagename;
}

/*
 * Free a pageInfo_t and it's internal members.
 */
void
freePageInfo(pageInfo_t *pp)
{
	if (pp) {
		free(pp->pageName);
		free(pp->pageId);
		free(pp->srcId);
		free(pp);
	}
}

/*
 * Get next tag text, eliminating leading and trailing whitespace
 * and leaving only a single space for all internal whitespace.
 */
static const char *
getTag(memBuf_t *mp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;
	int inStr = 0, comment = 0, c;

	if (memEof(mp)) {
		log(("getTag(): returning NULL\n"));
		return NULL;
	}
	while ((c = memGetc(mp)) != EOF && c != '<')
		;
	if (c == EOF) {
		log(("getTag(): returning NULL\n"));
		return NULL;
	}

	/* first char - check for comment */
	c = memGetc(mp);
	if (c == '>') {
		log(("getTag(): returning empty tag\n"));
		return "";
	} else if (c == EOF) {
		log(("getTag(): returning NULL\n"));
		return NULL;
	}
	addchar(buf, bufsize, count, c);
	if (c == '!') {
		int c2 = memGetc(mp);

		if (c2 == '>' || c2 == EOF) {
			term(buf, bufsize, count);
			log(("getTag(): returning %s\n", buf));
			return buf;
		}
		addchar(buf, bufsize, count, c2);
		if (c2 == '-') {
			int c3 = memGetc(mp);

			if (c3 == '>' || c3 == EOF) {
				term(buf, bufsize, count);
				log(("getTag(): returning %s\n", buf));
				return buf;
			}
			addchar(buf, bufsize, count, c3);
			comment = 1;
		}
	}

	if (comment) {
		while ((c = memGetc(mp)) != EOF) {
			if (c=='>' && buf[count-1]=='-' && buf[count-2]=='-') {
				term(buf, bufsize, count);
				log(("getTag(): returning %s\n", buf));
				return buf;
			}
			if (isspace(c) && buf[count-1] == ' ')
				continue;
			addchar(buf, bufsize, count, c);
		}
	} else {
		while ((c = memGetc(mp)) != EOF) {
			switch (c) {
			case '\\':
				addchar(buf, bufsize, count, c);
				c = memGetc(mp);
				if (c == EOF) {
					term(buf, bufsize, count);
					log(("getTag(): returning %s\n", buf));
					return buf;
				}
				addchar(buf, bufsize, count, c);
				break;
			case '>':
				if (inStr)
					addchar(buf, bufsize, count, c);
				else {
					term(buf, bufsize, count);
					log(("getTag(): returning %s\n", buf));
					return buf;
				}
				break;
			case ' ':
			case '\n':
			case '\r':
			case '\t':
			case '\v':
				if (inStr)
					addchar(buf, bufsize, count, c);
				else if (count > 0 && buf[count-1] != ' ')
					addchar(buf, bufsize, count, ' ');
				break;
			case '"':
				inStr = !inStr;
				/* fall through */
			default:
				addchar(buf, bufsize, count, c);
			}
		}
	}
	term(buf, bufsize, count);
	log(("getTag(): returning %s\n", count ? buf : "NULL"));
	return count ? buf : NULL;
}

/*
 * Get next non-tag text, eliminating leading and trailing whitespace
 * and leaving only a single space for all internal whitespace.
 */
static char *
getNonTag(memBuf_t *mp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0, amp = 0;
	int c;

	if (memEof(mp)) {
		log(("getNonTag(): returning NULL\n"));
		return NULL;
	}
	while ((c = memGetc(mp)) != EOF) {
		switch (c) {
		case '<':
			memUngetc(mp);
			if (count) {
				if (buf[count-1] == ' ')
					--count;
				term(buf, bufsize, count);
				log(("getNonTag(): returning %s\n", buf));
				return buf;
			} else
				(void)getTag(mp);
			break;
		case ' ':
		case '\n':
		case '\r':
		case '\t':
		case '\v':
		case 0xA0: /* iso-8859-1 nbsp */
			if (count && buf[count-1] != ' ')
				addchar(buf, bufsize, count, ' ');
			break;
		case ';':
			if (amp > 0) {
				char *cp = &buf[amp];

				term(buf, bufsize, count);
				if (*cp == '#') {
					buf[amp-1] = atoi(cp+1);
					count = amp;
				} else if (!strcmp(cp, "amp")) {
					count = amp;
				} else if (!strcmp(cp, "gt")) {
					buf[amp-1] = '>';
					count = amp;
				} else if (!strcmp(cp, "lt")) {
					buf[amp-1] = '<';
					count = amp;
				} else if (!strcmp(cp, "nbsp")) {
					buf[amp-1] = ' ';
					count = amp;
					if (count && buf[count-1] == ' ')
						--count;
				} else if (!strcmp(cp, "quot")) {
					buf[amp-1] = '&';
					count = amp;
				} else
					addchar(buf, bufsize, count, c);
				amp = 0;
			} else
				addchar(buf, bufsize, count, c);
			break;
		case '&':
			amp = count + 1;
			/* fall through */
		default:
			addchar(buf, bufsize, count, c);
		}
	}
	if (count && buf[count-1] == ' ')
		--count;
	term(buf, bufsize, count);
	log(("getNonTag(): returning %s\n", count ? buf : "NULL"));
	return count ? buf : NULL;
} /* getNonTag() */

static char *
getNonTagFromString(const char *s)
{
	memBuf_t *mp = strToMemBuf(s);
	char *ret = myStrdup(getNonTag(mp));

	clearMembuf(mp);
	return ret;
}

static int
getIntFromString(const char *s)
{
	memBuf_t *mp = strToMemBuf(s);
	char *num = getNonTag(mp);
	int ret = atoi(num);

	clearMembuf(mp);
	return ret;
}

/*
 * Calculate quantity to bid on.  If it is a dutch auction, never
 * bid on more than 1 less item than what is available.
 */
static int
getQuantity(int want, int available)
{
	if (want == 1 || available == 1)
		return 1;
	if (available > want)
		return want;
	return available - 1;
}

static long
getSeconds(char *timestr)
{
	static char second[] = "sec";
	static char minute[] = "min";
	static char hour[] = "hour";
	static char day[] = "day";
	static char ended[] = "ended";
	long accum = 0;
	long num;

	/* Time is blank in transition between "Time left: 1 seconds" and
	 * "Time left: auction has ended".  I don't know if blank means
	 * the auction is over, or it still running with less than 1 second.
	 * I'll make the safer assumption and say that there is 1 second
	 * remaining.
	 */
	if (!*timestr)
		return 1;
	if (strstr(timestr, ended))
		return 0;
	while (*timestr) {
		num = strtol(timestr, &timestr, 10);
		while (isspace((int)*timestr))
			++timestr;
		if (!strncmp(timestr, second, sizeof(second) - 1))
			return(accum + num);
		else if (!strncmp(timestr, minute, sizeof(minute) - 1))
			accum += num * 60;
		else if (!strncmp(timestr, hour, sizeof(hour) - 1))
			accum += num * 3600;
		else if (!strncmp(timestr, day, sizeof(day) - 1))
			accum += num * 86400;
		else
			return -1;
		while (*timestr && !isdigit((int)*timestr))
			++timestr;
	}

	return accum;
}

/*
 * Search to end of table, returning /table tag (or NULL if not found).
 * Embedded tables are skipped.
 */
static const char *
getTableEnd(memBuf_t *mp)
{
	int nesting = 1;
	const char *cp;

	while ((cp = getTag(mp))) {
		if (!strcmp(cp, "/table")) {
			if (--nesting == 0)
				return cp;
		} else if (!strncmp(cp, "table", 5) &&
			   (isspace((int)*(cp+5)) || *(cp+5) == '\0')) {
			++nesting;
		}
	}
	return NULL;
}

/*
 * Search for next table item.  Return NULL at end of a row, and another NULL
 * at the end of a table.
 */
static char *
getTableCell(memBuf_t *mp)
{
	int nesting = 1;
	const char *cp, *start = mp->readptr, *end = NULL;
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;

	while ((cp = getTag(mp))) {
		if (nesting == 1 && !strncmp(cp, "td", 2) &&
		    (isspace((int)*(cp+2)) || *(cp+2) == '\0')) {
			/* found <td>, now must find </td> */
			start = mp->readptr;
		} else if (nesting == 1 && !strcmp(cp, "/td")) {
			/* end of this item */
			for (end = mp->readptr - 1; *end != '<'; --end)
				;
			for (cp = start; cp < end; ++cp) {
				addchar(buf, bufsize, count, *cp);
			}
			term(buf, bufsize, count);
			return buf;
		} else if (nesting == 1 && !strcmp(cp, "/tr")) {
			/* end of this row */
			return NULL;
		} else if (!strcmp(cp, "/table")) {
			/* end of this table? */
			if (--nesting == 0)
				return NULL;
		} else if (!strncmp(cp, "table", 5) &&
			   (isspace((int)*(cp+5)) || *(cp+5) == '\0')) {
			++nesting;
		}
	}
	/* error? */
	return NULL;
}

/*
 * Return NULL-terminated table row, or NULL at end of table.
 * All cells are malloc'ed and should be freed by the calling function.
 */
static char **
getTableRow(memBuf_t *mp)
{
	char **ret = NULL, *cp = NULL;
	size_t size = 0, i = 0;

	do {
		cp = getTableCell(mp);
		if (cp || i) {
			if (i >= size) {
				size += 10;
				ret = (char **)myRealloc(ret, size * sizeof(char *));
			}
			ret[i++] = myStrdup(cp);
		}
	} while ((cp));
	return ret;
}

/*
 * Return number of columns in row, or -1 if null.
 */
static int
numColumns(char **row)
{
	int ncols = 0;

	if (!row)
		return -1;
	while (row[ncols++])
		;
	return --ncols;
}

/*
 * Free a TableRow allocated by getTableRow
 */
static void
freeTableRow(char **row) {
	char **cpp;

	for (cpp = row; *cpp; ++cpp)
		free(*cpp);
	free(row);
}

/*
 * Search for next table tag.
 */
static const char *
getTableStart(memBuf_t *mp)
{
	const char *cp;

	while ((cp = getTag(mp))) {
		if (!strncmp(cp, "table", 5) &&
		    (isspace((int)*(cp+5)) || *(cp+5) == '\0'))
			return cp;
	}
	return NULL;
}

static const char PRIVATE[] = "private auction - bidders' identities protected";

/*
 * parseBidHistory(): parses bid history page (pageName: PageViewBids)
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) - sets auctionError
 */
static int
parseBidHistory(memBuf_t *mp, auctionInfo *aip, const char *user, time_t start, time_t *timeToFirstByte)
{
	char *line;
	char **row;
	int foundHeader = 0;	/* found header for bid table */
	int ret = 0;		/* 0 = OK, 1 = failed */
	char *pagename;

	resetAuctionError(aip);

	if (timeToFirstByte)
		*timeToFirstByte = getTimeToFirstByte(mp);

	/*
	 * Auction title
	 */
	pagename = getPageName(mp);
	if (!strcmp(pagename, "PageViewBids")) {
		/* bid history or expired/bad auction number */
		while ((line = getNonTag(mp))) {
			if (!strcmp(line, "Bid History"))
				break;
			if (!strcmp(line, "Unknown Item"))
				return auctionError(aip, ae_baditem, NULL);
		}
		if (!line)
			return auctionError(aip, ae_notitle, NULL);
	} else if (!strcmp(pagename, "PageViewTransactions")) {
		/* transaction history -- buy it now only */
	} else if (!strcmp(pagename, "PageSignIn")) {
		return auctionError(aip, ae_mustsignin, NULL);
	}

	/*
	 * Auction item
	 */
	while ((line = getNonTag(mp))) {
		if (!strcmp(line, "Item title:")) {
			line = getNonTag(mp);
			break;
		}
	}
	if (!line)
		return auctionError(aip, ae_notitle, NULL);
	free(aip->title);
	aip->title = myStrdup(line);
	while ((line = getNonTag(mp))) {
		char *tmp;

		if (line[strlen(line) - 1] == ':')
			break;
		tmp = aip->title;
		aip->title = myStrdup2(tmp, line);
		free(tmp);
	}
	printLog(stdout, "Auction %s: %s\n", aip->auction, aip->title);

	/*
	 * Quantity/Current price/Time remaining
	 */
	aip->quantity = 1;	/* If quantity not found, assume 1 */
	for (; line; line = getNonTag(mp)) {
		if (!strcmp("Quantity:", line) ||
		    !strcmp("Quantity left:", line)) {
			line = getNonTag(mp);
			if (!line || (aip->quantity = atoi(line)) < 1)
				return auctionError(aip, ae_noquantity, NULL);
			log(("quantity: %d", aip->quantity));
		} else if (!strcmp("Currently:", line)) {
			line = getNonTag(mp);
			if (!line)
				return auctionError(aip, ae_noprice, NULL);
			log(("Currently: %s\n", line));
			aip->price = atof(priceFixup(line, aip));
			if (aip->price < 0.01)
				return auctionError(aip, ae_convprice, line);
		} else if (!strcmp("Time left:", line)) {
			/* Use getTableCell() instead of getNonTag(), because
			 * for about a second at the end of an auction the
			 * time is left blank.  getTableCell() will give you
			 * a cell, even if it is empty.  getNonTag() will
			 * happily skip over to the next real text (like
			 * "Reserve not met", or whatever).
			 */
			getTableCell(mp); /* end of "Time left:" cell */
			getTableCell(mp); /* spacer */
			free(aip->remainRaw);
			aip->remainRaw = getNonTagFromString(getTableCell(mp));
			break;
		}
	}
	if (!aip->remainRaw)
		return auctionError(aip, ae_notime, NULL);
	if ((aip->remain = getSeconds(aip->remainRaw)) < 0)
		return auctionError(aip, ae_badtime, aip->remainRaw);
	printLog(stdout, "Time remaining: %s (%ld seconds)\n", line, aip->remain);
	if (aip->remain) {
		struct tm *tmPtr;
		char timestr[20];

		aip->endTime = start + aip->remain;
		/* formated time/date output */
		tmPtr = localtime(&(aip->endTime));
		strftime(timestr , 20, "%d/%m/%Y %H:%M:%S", tmPtr);
		printLog(stdout, "End time: %s\n", timestr);
	} else
		aip->endTime = aip->remain;

	if (!(line = getNonTag(mp)))
		return auctionError(aip, ae_nohighbid, NULL);
	if (!strcmp("Reserve not met", line))
		aip->reserve = 1;
	else {
		aip->reserve = 0;
		if ((foundHeader = !strcmp("User ID", line)))
			/* skip over first line */
			freeTableRow(getTableRow(mp));
	}

	/*
	 * Determine high bidder
	 *
	 * Format of high bidder table is:
	 *
	 *	Single item auction:
	 *	    Header line:
	 *		""
	 *		"User ID"
	 *		"Bid Amount"
	 *		"Date of bid"
	 *		""
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<amount>
	 *			<date>
	 *			""
	 *	    (plus multiple rows of 1 column between entries)
	 *
	 *	    If there are no bids:
	 *			""
	 *			"No bids have been placed."
	 *
	 *	    If the auction is private, the user names are:
	 *		"private auction - bidders' identities protected"
	 *
	 *	Purchase (buy-it-now only):
	 *	    Header line:
	 *		""
	 *		"User ID"
	 *		"Bid Amount"
	 *		"Qty"
	 *		"Date of bid"
	 *		""
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<amount>
	 *			<quantity>
	 *			<date>
	 *			""
	 *	    (plus multiple rows of 1 column between entries)
	 *
	 *	    If there are no bids:
	 *			""
	 *			"No purchases have been made."
	 *
	 *	    If the auction is private, the user names are:
	 *		"private auction - bidders' identities protected"
	 *
	 *	Dutch auction:
	 *	    Header line:
	 *		""
	 *		"User ID"
	 *		"Bid Amount"
	 *		"Quantity wanted"
	 *		"Quantity winning"
	 *		"Date of Bid"
	 *		""
	 *
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<amount>
	 *			<quantity wanted>
	 *			<quantity winning>
	 *			<date>
	 *			""
	 *	    (plus multiple rows of 1 column between entries)
	 *
	 *	    If there are no bids:
	 *			""
	 *			"No bids have been placed."
	 *
	 *	    Dutch auctions cannot be private.
	 *
	 *	If there are no bids, the text "No bids have been placed."
	 *	will be the first entry in the table.
	 */
	/* find header line */
	while (!foundHeader && getTableStart(mp)) {
		while (!foundHeader && (row = getTableRow(mp))) {
			int ncolumns = numColumns(row);
			char *rawHeader = (ncolumns >= 5) ? row[1] : NULL;
			char *header = getNonTagFromString(rawHeader);

			foundHeader = header && !strcmp(header, "User ID");
			freeTableRow(row);
			free(header);
		}
	}
	if (!foundHeader) {
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "Cannot find bid table header");
		return auctionError(aip, ae_nohighbid, NULL);
	}
	/* skip over initial single-column rows */
	while ((row = getTableRow(mp))) {
		if (numColumns(row) == 1) {
			freeTableRow(row);
			continue;
		}
		break;
	}
	/* roll through table */
	switch (numColumns(row)) {
	case 2:	/* auction with no bids */
	    {
		char *s = getNonTagFromString(row[1]);

		if (!strcmp("No bids have been placed.", s) ||
		    !strcmp("No purchases have been made.", s)) {
			aip->quantityBid = 0;
			aip->bids = 0;
			/* can't determine starting bid on history page */
			aip->price = 0;
			printf("# of bids: 0\n"
			       "Currently: --  (your maximum bid: %s)\n"
			       "High bidder: -- (not %s)\n",
			       aip->bidPriceStr, user);
		} else {
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "Unrecognized bid table line");
			ret = auctionError(aip, ae_nohighbid, NULL);
		}
		freeTableRow(row);
		free(s);
		break;
	    }
	case 5: /* single auction with bids */
	    {
		/* blank, user, price, date, blank */
		char *winner = getNonTagFromString(row[1]);
		char *currently = getNonTagFromString(row[2]);

		aip->quantityBid = 1;

		/* current price */
		aip->price = atof(priceFixup(currently, aip));
		if (aip->price < 0.01) {
			free(winner);
			free(currently);
			return auctionError(aip, ae_convprice, currently);
		}
		printLog(stdout, "Currently: %s  (your maximum bid: %s)\n",
			 currently, aip->bidPriceStr);
		free(currently);

		/* winning user */
		if (!strcmp(winner, PRIVATE)) {
			free(winner);
			winner = myStrdup((aip->price <= aip->bidPrice &&
					    (aip->bidResult == 0 ||
					     (aip->bidResult == -1 && aip->endTime - time(NULL) < options.bidtime))) ?  user : "[private]");
		}
		freeTableRow(row);

		/* count bids */
		for (aip->bids = 1; (row = getTableRow(mp)); ) {
			if (numColumns(row) == 5)
				++aip->bids;
			freeTableRow(row);
		}
		printLog(stdout, "# of bids: %d\n", aip->bids);

		/* print high bidder */
		if (strcasecmp(winner, user)) {
			printLog(stdout, "High bidder: %s (NOT %s)\n",
				 winner, user);
			aip->winning = 0;
			if (!aip->remain)
				aip->won = 0;
		} else if (aip->reserve) {
			printLog(stdout, "High bidder: %s (reserve not met)\n",
				 winner);
			aip->winning = 0;
			if (!aip->remain)
				aip->won = 0;
		} else {
			printLog(stdout, "High bidder: %s!!!\n", winner);
			aip->winning = 1;
			if (!aip->remain)
				aip->won = 1;
		}
		free(winner);
		break;
	    }
	case 6:	/* purchase */
	    {
		char *currently = getNonTagFromString(row[2]);

		aip->bids = 0;
		aip->quantityBid = 0;
		aip->won = 0;
		aip->winning = 0;
		/* find your purchase, count number of purchases */
		/* blank, user, price, quantity, date, blank */
		for (; row; row = getTableRow(mp)) {
			if (numColumns(row) == 6) {
				int quantity = getIntFromString(row[3]);
				char *bidder;

				++aip->bids;
				aip->quantityBid += quantity;
				bidder = getNonTagFromString(row[1]);
				if (!strcasecmp(bidder, user))
					aip->won = aip->winning = quantity;
				free(bidder);
			}
			freeTableRow(row);
		}
		printf("# of bids: %d\n", aip->bids);
		printf("Currently: %s  (your maximum bid: %s)\n",
		       currently, aip->bidPriceStr);
		free(currently);
		switch (aip->winning) {
		case 0:
			printLog(stdout, "High bidder: various purchasers (NOT %s)\n", user);
			break;
		case 1:
			printLog(stdout, "High bidder: %s!!!\n", user);
			break;
		default:
			printLog(stdout, "High bidder: %s!!! (%d items)\n", user, aip->winning);
			break;
		}
		break;
	    }
	case 7: /* dutch with bids */
	    {
		int wanted = 0;
		char *currently = NULL;

		aip->bids = 0;
		aip->quantityBid = 0;
		aip->won = 0;
		aip->winning = 0;
		/* find your bid, count number of bids */
		/* blank, user, price, wanted, winning, date, blank */
		for (; row; row = getTableRow(mp)) {
			if (numColumns(row) == 7) {
				int bidderWinning = getIntFromString(row[4]);

				++aip->bids;
				if (bidderWinning > 0) {
					char *bidder;

					aip->quantityBid += bidderWinning;
					free(currently);
					bidder = getNonTagFromString(row[1]);
					currently = getNonTagFromString(row[2]);
					if (!strcasecmp(bidder, user)) {
						wanted = getIntFromString(row[3]);
						aip->winning = bidderWinning;
					}
					free(bidder);
				}
			}
			freeTableRow(row);
		}
		if (!aip->remain)
			aip->won = aip->winning;
		printf("# of bids: %d\n", aip->bids);
		printf("Currently: %s  (your maximum bid: %s)\n",
		       currently, aip->bidPriceStr);
		free(currently);
		if (aip->winning > 0) {
			if (aip->winning == wanted)
				printLog(stdout, "High bidder: %s!!!\n", user);
			else
				printLog(stdout, "High bidder: %s!!! (%d out of %d items)\n", user, aip->winning, wanted);
		} else
			printLog(stdout, "High bidder: various dutch bidders (NOT %s)\n", user);
		break;
	    }
	default:
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "%d columns in bid table", numColumns(row));
		ret = auctionError(aip, ae_nohighbid, NULL);
		freeTableRow(row);
	}

	return ret;
} /* parseBidHistory() */

static const char GETINFO[] = "http://offer.ebay.com/ws/eBayISAPI.dll?ViewBids&item=";

/*
 * getInfo(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
int
getInfo(auctionInfo *aip, const char *user)
{
	return getInfoTiming(aip, user, NULL);
}

/*
 * getInfoTiming(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
static int
getInfoTiming(auctionInfo *aip, const char *user, time_t *timeToFirstByte)
{
	memBuf_t *mp;
	int i, ret;
	time_t start;

	log(("\n\n*** getInfo auction %s price %s user %s\n", aip->auction, aip->bidPriceStr, user));

	for (i = 0; i < 3; ++i) {
		if (!aip->query)
			aip->query = myStrdup2(GETINFO, aip->auction);
		start = time(NULL);
		if (!(mp = httpGet(aip->query, NULL)))
			return auctionError(aip, ae_curlerror, NULL);

		ret = parseBidHistory(mp, aip, user, start, timeToFirstByte);
		clearMembuf(mp);
		if (i == 0 && ret == 1 && aip->auctionError == ae_mustsignin) {
			if (ebayLogin(aip))
				break;
		} else if (aip->auctionError == ae_notime)
			/* Blank time remaining -- give it another chance */
			sleep(2);
		else
			break;
	}
	return ret;
}

/*
 * Note: quant=1 is just to dupe eBay into allowing the pre-bid to get
 *	 through.  Actual quantity will be sent with bid.
 */
static const char PRE_BID_URL[] = "http://offer.ebay.com/ws/eBayISAPI.dll?MfcISAPICommand=MakeBid&fb=2&co_partner_id=&item=%s&maxbid=%s&quant=%s";

/*
 * Get bid key
 *
 * returns 0 on success, 1 on failure.
 */
static int
preBid(auctionInfo *aip)
{
	memBuf_t *mp;
	int quantity = getQuantity(aip->quantity, options.quantity);
	char quantityStr[12];	/* must hold an int */
	size_t urlLen;
	char *url;
	int ret = 0;
	int found = 0;

	sprintf(quantityStr, "%d", quantity);
	urlLen = sizeof(PRE_BID_URL) + strlen(aip->auction) + strlen(aip->bidPriceStr) + strlen(quantityStr) - 6;
	url = (char *)myMalloc(urlLen);
	sprintf(url, PRE_BID_URL, aip->auction, aip->bidPriceStr, quantityStr);
	log(("\n\n*** preBid(): url is %s\n", url));
	mp = httpGet(url, NULL);
	free(url);
	if (!mp)
		return auctionError(aip, ae_curlerror, NULL);

	/* pagename should be PageReviewBidBottomButton, but don't check it */
	while (found < 7 && !match(mp, "<input type=\"hidden\" name=\"")) {
		if (!strncmp(mp->readptr, "key\"", 4)) {
			char *cp, *tmpkey;

			found |= 0x01;
			match(mp, "value=\"");
			tmpkey = getUntil(mp, '\"');
			log(("  reported key is: %s\n", tmpkey));

			/* translate key for URL */
			free(aip->bidkey);
			aip->bidkey = (char *)myMalloc(strlen(tmpkey)*3 + 1);
			for (cp = aip->bidkey; *tmpkey; ++tmpkey) {
				if (*tmpkey == '$') {
					*cp++ = '%';
					*cp++ = '2';
					*cp++ = '4';
				} else
					*cp++ = *tmpkey;
			}
			*cp = '\0';

			log(("\n\ntranslated key is: %s\n\n", aip->bidkey));
		} else if (!strncmp(mp->readptr, "uiid\"", 5)) {
			found |= 0x02;
			match(mp, "value=\"");
			free(aip->biduiid);
			aip->biduiid = myStrdup(getUntil(mp, '\"'));
			log(("preBid(): biduiid is \"%s\"", aip->biduiid));
		} else if (!strncmp(mp->readptr, "pass\"", 5)) {
			found |= 0x04;
			match(mp, "value=\"");
			free(aip->bidpass);
			aip->bidpass = myStrdup(getUntil(mp, '\"'));
			log(("preBid(): bidpass is \"%s\"", aip->bidpass));
		}
	}
	if (found < 7) {
		char *pagename;

		mp->readptr = mp->memory;
		pagename = getPageName(mp);
		if ((ret = makeBidError(pagename, aip)) < 0) {
			ret = auctionError(aip, ae_bidkey, NULL);
			bugReport("preBid", __FILE__, __LINE__, aip, mp, "cannot find bid key, uiid or password, found = %d", found);
		}
	}
	clearMembuf(mp);
	return ret;
}

static const char LOGIN_1_URL[] = "https://signin.ebay.com/ws/eBayISAPI.dll?SignIn";
static const char LOGIN_2_URL[] = "https://signin.ebay.com/ws/eBayISAPI.dll?SignInWelcome&userid=%s&pass=%s&keepMeSignInOption=1";

/*
 * Ebay login
 *
 * returns 0 on success, 1 on failure.
 */
static int
ebayLogin(auctionInfo *aip)
{
	memBuf_t *mp;
	size_t urlLen;
	char *url, *logUrl;
	pageInfo_t *pp;
	int ret = 0;
	char *password;

	if (!(mp = httpGet(LOGIN_1_URL, NULL)))
		return auctionError(aip, ae_curlerror, NULL);

	clearMembuf(mp);

	urlLen = sizeof(LOGIN_2_URL) + strlen(options.username);
	password = getPassword();
	url = malloc(urlLen + strlen(password));
	logUrl = malloc(urlLen + 5);

	sprintf(url, LOGIN_2_URL, options.username, password);
	freePassword(password);
	sprintf(logUrl, LOGIN_2_URL, options.username, "*****");

	mp = httpGet(url, logUrl);
	free(url);
	free(logUrl);
	if (!mp)
		return auctionError(aip, ae_curlerror, NULL);

	if ((pp = getPageInfo(mp))) {
		log(("ebayLogin(): pagename = \"%s\", pageid = \"%s\", srcid = \"%s\"", nullStr(pp->pageName), nullStr(pp->pageId), nullStr(pp->srcId)));
		/*
		 * Pagename is usually MyeBaySummary, but it seems as though
		 * it can be any MyeBay page, and eBay is not consistent with
		 * naming of MyeBay pages (MyeBay, MyEbay, myebay, ...) so
		 * esniper must use strncasecmp().
		 */
		if (pp->srcId &&
		    (!strncasecmp(pp->pageName, "MyeBay", 6) ||
		    !strcmp(pp->srcId, "SignInAlertSupressor")))
			loginTime = time(NULL);
		else if (pp->pageName && !strcmp(pp->pageName, "PageSignIn"))
			ret = auctionError(aip, ae_login, NULL);
		else {
			ret = auctionError(aip, ae_login, NULL);
			bugReport("ebayLogin", __FILE__, __LINE__, aip, mp, "unknown pageinfo");
		}
	} else {
		log(("ebayLogin(): pageinfo is NULL\n"));
		ret = auctionError(aip, ae_login, NULL);
		bugReport("ebayLogin", __FILE__, __LINE__, aip, mp, "pageinfo is NULL");
	}
	clearMembuf(mp);
	freePageInfo(pp);
	return ret;
}

/*
 * acceptBid: handle all known AcceptBid pages.
 *
 * Returns -1 if page not recognized, 0 if bid accepted, 1 if bid not accepted.
 */
static int
acceptBid(const char *pagename, auctionInfo *aip)
{
	static const char ACCEPTBID[] = "AcceptBid_";
	static const char HIGHBID[] = "HighBidder";
	static const char OUTBID[] = "Outbid";
	static const char RESERVENOTMET[] = "ReserveNotMet";

	if (!pagename ||
	    strncmp(pagename, ACCEPTBID, sizeof(ACCEPTBID) - 1))
		return -1;
	pagename += sizeof(ACCEPTBID) - 1;
	/*
	 * valid pagenames include AcceptBid_HighBidder,
	 * AcceptBid_HighBidder_rebid, possibly others.
	 */
	if (!strncmp(pagename, HIGHBID, sizeof(HIGHBID) - 1))
		return aip->bidResult = 0;
	/*
	 * valid pagenames include AcceptBid_Outbid, AcceptBid_Outbid_rebid,
	 * possibly others.
	 */
	if (!strncmp(pagename, OUTBID, sizeof(OUTBID) - 1))
		return aip->bidResult = auctionError(aip, ae_outbid, NULL);
	/*
	 * valid pagenames include AcceptBid_ReserveNotMet,
	 * AcceptBid_ReserveNotMet_rebid, possibly others.
	 */
	if (!strncmp(pagename, RESERVENOTMET, sizeof(RESERVENOTMET) - 1))
		return aip->bidResult = auctionError(aip, ae_reservenotmet, NULL);
	/* unknown AcceptBid page */
	return -1;
}

/*
 * makeBidError: handle all known MakeBidError pages.
 *
 * Returns -1 if page not recognized, 0 if bid accepted, 1 if bid not accepted.
 */
static int
makeBidError(const char *pagename, auctionInfo *aip)
{
	static const char MAKEBIDERROR[] = "MakeBidError";

	if (!pagename ||
	    strncmp(pagename, MAKEBIDERROR, sizeof(MAKEBIDERROR) - 1))
		return -1;
	pagename += sizeof(MAKEBIDERROR) - 1;
	if (!*pagename ||
	    !strcmp(pagename, "AuctionEnded"))
		return aip->bidResult = auctionError(aip, ae_ended, NULL);
	if (!strcmp(pagename, "Password"))
		return aip->bidResult = auctionError(aip, ae_badpass, NULL);
	if (!strcmp(pagename, "MinBid"))
		return aip->bidResult = auctionError(aip, ae_bidprice, NULL);
	if (!strcmp(pagename, "BuyerBlockPref"))
		return aip->bidResult = auctionError(aip, ae_buyerblockpref, NULL);
	if (!strcmp(pagename, "BuyerBlockPrefDoesNotShipToLocation"))
		return aip->bidResult = auctionError(aip, ae_buyerblockprefdoesnotshiptolocation, NULL);
	if (!strcmp(pagename, "BuyerBlockPrefNoLinkedPaypalAccount"))
		return aip->bidResult = auctionError(aip, ae_buyerblockprefnolinkedpaypalaccount, NULL);
	if (!strcmp(pagename, "HighBidder"))
		return aip->bidResult = auctionError(aip, ae_highbidder, NULL);
	if (!strcmp(pagename, "CannotBidOnItem"))
		return aip->bidResult = auctionError(aip, ae_cannotbid, NULL);
	if (!strcmp(pagename, "DutchSameBidQuantity"))
		return aip->bidResult = auctionError(aip, ae_dutchsamebidquantity, NULL);
	/* unknown MakeBidError page */
	return -1;
}

/*
 * Parse bid result.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
static int
parseBid(memBuf_t *mp, auctionInfo *aip)
{
	/*
	 * The following sometimes have more characters after them, for
	 * example AcceptBid_HighBidder_rebid (you were already the high
	 * bidder and placed another bid).
	 */
	char *pagename = getPageName(mp);
	int ret;

	aip->bidResult = -1;
	log(("parseBid(): pagename = %s\n", pagename));
	if ((ret = acceptBid(pagename, aip)) >= 0 ||
	    (ret = makeBidError(pagename, aip)) >= 0)
		return ret;
	bugReport("parseBid", __FILE__, __LINE__, aip, mp, "unknown pagename");
	printLog(stdout, "Cannot determine result of bid\n");
	return 0;	/* prevent another bid */
} /* parseBid() */

static const char BID_URL[] = "http://offer.ebay.com/ws/eBayISAPI.dll?MfcISAPICommand=MakeBid&BIN_button=Confirm%%20Bid&item=%s&key=%s&maxbid=%s&quant=%s&user=%s&pass=%s&uiid=%s&javascriptenabled=0&mode=1";

/*
 * Place bid.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
static int
bid(auctionInfo *aip)
{
	memBuf_t *mp;
	size_t urlLen;
	char *url, *logUrl, *tmpUsername, *tmpPassword, *tmpUiid;
	int ret;
	int quantity = getQuantity(aip->quantity, options.quantity);
	char quantityStr[12];	/* must hold an int */

	if (!aip->bidkey || !aip->bidpass || !aip->biduiid)
		return auctionError(aip, ae_bidkey, NULL);

	sprintf(quantityStr, "%d", quantity);

	/* create url */
	urlLen = sizeof(BID_URL) + strlen(aip->auction) + strlen(aip->bidkey) + strlen(aip->bidPriceStr) + strlen(quantityStr) + strlen(options.username) + strlen(aip->bidpass) + strlen(aip->biduiid) - 12;
	url = (char *)myMalloc(urlLen);
	sprintf(url, BID_URL, aip->auction, aip->bidkey, aip->bidPriceStr, quantityStr, options.username, aip->bidpass, aip->biduiid);

	logUrl = (char *)myMalloc(urlLen);
	tmpUsername = stars(strlen(options.username));
	tmpPassword = stars(strlen(aip->bidpass));
	tmpUiid = stars(strlen(aip->biduiid));
	sprintf(logUrl, BID_URL, aip->auction, aip->bidkey, aip->bidPriceStr, quantityStr, tmpUsername, tmpPassword, tmpUiid);
	free(tmpUsername);
	free(tmpPassword);
	free(tmpUiid);

	if (!options.bid) {
		printLog(stdout, "Bidding disabled\n");
		log(("\n\nbid(): query url:\n%s\n", logUrl));
		ret = aip->bidResult = 0;
	} else if (!(mp = httpGet(url, logUrl))) {
		ret = auctionError(aip, ae_curlerror, NULL);
	} else {
		ret = parseBid(mp, aip);
		clearMembuf(mp);
	}
	free(url);
	free(logUrl);
	return ret;
} /* bid() */

/*
 * watch(): watch auction until it is time to bid
 *
 * returns:
 *	0 OK
 *	1 Error
 */
static int
watch(auctionInfo *aip)
{
	int errorCount = 0;
	long remain = LONG_MIN;
	unsigned int sleepTime = 0;

	log(("*** WATCHING auction %s price-each %s quantity %d bidtime %ld\n", aip->auction, aip->bidPriceStr, options.quantity, options.bidtime));

	for (;;) {
		time_t tmpLatency;
		time_t start = time(NULL);
		time_t timeToFirstByte = 0;
		int ret = getInfoTiming(aip, options.username, &timeToFirstByte);
		time_t end = time(NULL);

		if (timeToFirstByte == 0)
			timeToFirstByte = end;
		tmpLatency = (timeToFirstByte - start);
		if ((tmpLatency >= 0) && (tmpLatency < 600))
			aip->latency = tmpLatency;
		printLog(stdout, "Latency: %d seconds\n", aip->latency);

		if (ret) {
			printAuctionError(aip, stderr);

			/*
			 * Fatal error?  We allow up to 50 errors, then quit.
			 * eBay "unavailable" doesn't count towards the total.
			 */
			if (aip->auctionError == ae_unavailable) {
				if (remain >= 0)
					remain = newRemain(aip);
				if (remain == LONG_MIN || remain > 86400) {
					/* typical eBay maintenance period
					 * is two hours.  Sleep for half that
					 * amount of time.
					 */
					printLog(stdout, "%s: Will try again, sleeping for an hour\n", timestamp());
					sleepTime = 3600;
					sleep(sleepTime);
					continue;
				}
			} else if (remain == LONG_MIN) {
				/* first time through?  Give it 3 chances then
				 * make the error fatal.
				 */
				int j;

				for (j = 0; ret && j < 3 && aip->auctionError == ae_notitle; ++j)
					ret = getInfo(aip, options.username);
				if (ret)
					return 1;
				remain = newRemain(aip);
			} else {
				/* non-fatal error */
				log(("ERROR %d!!!\n", ++errorCount));
				if (errorCount > 50)
					return auctionError(aip, ae_toomany, NULL);
				printLog(stdout, "Cannot find auction - internet or eBay problem?\nWill try again after sleep.\n");
				remain = newRemain(aip);
			}
		} else if (!isValidBidPrice(aip))
			return auctionError(aip, ae_bidprice, NULL);
		else
			remain = newRemain(aip);

		/*
		 * if we're less than five minutes away and login was
		 * more than five minutes ago, re-login
		 */
		if (remain <= 300 && (time(NULL) - loginTime) > 300) {
			cleanupCurlStuff();
			if (initCurlStuff())
				return auctionError(aip, ae_unknown, NULL);
			if (ebayLogin(aip))
				return 1;
			remain = newRemain(aip);
		}

		/*
		 * if we're less than two minutes away, get bid key
		 */
		if (remain <= 150 && !aip->bidkey && aip->auctionError == ae_none) {
			int i;

			printf("\n");
			for (i = 0; i < 5; ++i) {
				/* ae_bidkey is used when the page loaded
				 * but failed for some unknown reason.
				 * Do not try again in this situation.
				 */
				if (!preBid(aip) ||
				    aip->auctionError == ae_bidkey)
					break;
			}
			if (aip->auctionError != ae_none &&
			    aip->auctionError != ae_highbidder) {
				printLog(stderr, "Cannot get bid key\n");
				return 1;
			}
		}

		remain = newRemain(aip);

		/* it's time!!! */
		if (remain <= 0)
			break;

		/*
		 * Setup sleep schedule so we get updates once a day, then
		 * at 2 hours, 1 hour, 5 minutes, 2 minutes
		 */
		if (remain <= 150)	/* 2 minutes + 30 seconds (slop) */
			sleepTime = remain;
		else if (remain < 720)	/* 5 minutes + 2 minutes (slop) */
			sleepTime = remain - 120;
		else if (remain < 3900)	/* 1 hour + 5 minutes (slop) */
			sleepTime = remain - 600;
		else if (remain < 10800)/* 2 hours + 1 hour (slop) */
			sleepTime = remain - 3600;
		else if (remain < 97200)/* 1 day + 3 hours (slop) */
			sleepTime = remain - 7200;
		else			/* knock off one day */
			sleepTime = 86400;

		printf("%s: ", timestamp());
		if (sleepTime >= 86400)
			printLog(stdout, "Sleeping for a day\n");
		else if (sleepTime >= 3600)
			printLog(stdout, "Sleeping for %d hours %d minutes\n",
				sleepTime/3600, (sleepTime % 3600) / 60);
		else if (sleepTime >= 60)
			printLog(stdout, "Sleeping for %d minutes %d seconds\n",
				sleepTime/60, sleepTime % 60);
		else
			printLog(stdout, "Sleeping for %ld seconds\n", sleepTime);
		sleep(sleepTime);
		printf("\n");

		if ((remain=newRemain(aip)) <= 0)
			break;
	}
	return 0;
} /* watch() */

/*
 * parameters:
 * aip	auction to bid on
 *
 * return number of items won
 */
int
snipeAuction(auctionInfo *aip)
{
	int won = 0;

	if (!aip)
		return 0;

	if (options.debug)
		logOpen(aip, options.logdir);

	log(("auction %s price %s quantity %d user %s bidtime %ld\n",
	     aip->auction, aip->bidPriceStr,
	     options.quantity, options.username, options.bidtime));

	cleanupCurlStuff();
	if (initCurlStuff())
		exit(1);

	if (ebayLogin(aip)) {
		printAuctionError(aip, stderr);
		return 0;
	}

	/* 0 means "now" */
	if ((options.bidtime == 0) ? preBid(aip) : watch(aip)) {
		printAuctionError(aip, stderr);
		if (aip->auctionError != ae_highbidder)
			return 0;
	}

	/* ran out of time! */
	if (aip->endTime <= time(NULL)) {
		(void)auctionError(aip, ae_ended, NULL);
		printAuctionError(aip, stderr);
		return 0;
	}

	if (aip->auctionError != ae_highbidder) {
		printLog(stdout, "\nAuction %s: Bidding...\n", aip->auction);
		if (bid(aip)) {
			/* failed bid */
			printAuctionError(aip, stderr);
			return 0;
		}
	}

	/* view auction after bid.
	 * Stick it in a loop in case our timing is a bit off (due
	 * to wild swings in latency, for instance).
	 */
	for (;;) {
		if (options.bidtime > 0 && options.bidtime < 60) {
			time_t seconds = aip->endTime - time(NULL);

			if (seconds < 0)
				seconds = 0;
			/* extra 2 seconds to make sure auction is over */
			seconds += 2;
			printLog(stdout, "Auction %s: Waiting %d seconds for auction to complete...\n", aip->auction, seconds);
			sleep((unsigned int)seconds);
		}

		printLog(stdout, "\nAuction %s: Post-bid info:\n",
			 aip->auction);
		if (getInfo(aip, options.username))
			printAuctionError(aip, stderr);
		if (aip->remain > 0 && aip->remain < 60 &&
		    options.bidtime > 0 && options.bidtime < 60)
			continue;
		break;
	}

	if (aip->won == -1) {
		won = options.quantity < aip->quantity ?
			options.quantity : aip->quantity;
		printLog(stdout, "\nunknown outcome, assume that you have won %d items\n", won);
	} else {
		won = aip->won;
		printLog(stdout, "\nwon %d item(s)\n", won);
	}
	options.quantity -= won;
	return won;
}

static const char* MYITEMS_URL = "http://my.ebay.com/ws/eBayISAPI.dll?MyeBay&CurrentPage=MyeBayWatching";

#define MAX_TDS 6

/*
 * On first call, use printNewline to 0.  On subsequent calls, use return
 * value from previous call.
 */
static int
printMyItemsRow(char **row, int printNewline)
{
	const char *myitems_description[7][MAX_TDS] = {
		{0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{"Note:\t\t%s\n", 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, 0, "Description:\t%s\n", 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, "Price:\t\t%s\n", "Shipping:\t%s\n", "Bids:\t\t%s\n",
		 "Seller:\t\t%s\n", "Time:\t\t%s\n"}
	};
	int nColumns = numColumns(row);
	int column = 0;
	int ret = printNewline;

	if (nColumns == 4) {
		if (printNewline)
			printf("\n");
		else
			ret = 1;
	}
	for (; row[column]; ++column) {
		memBuf_t *mp;
		char *value = NULL;

		if (column >= MAX_TDS || !myitems_description[nColumns][column])
			continue;
		mp = strToMemBuf(row[column]);
		/* special case: ItemNr encoded in item URL */
		if (nColumns == 4 && column == 2) {
			static const char search[] = "item=";
			char *tmp = strstr(row[column], search);

			if (tmp) {
				int i;

				tmp += sizeof(search) - 1;
				for (; !isdigit(*tmp); ++tmp)
					;
				for (i = 1; isdigit(tmp[i]); ++i)
					;
				value = myStrndup(tmp, (size_t)(i));
				printLog(stdout, "ItemNr:\t\t%s\n", value);
				free(value);
			}
		}
		value = getNonTag(mp);
		/* Note text is 2nd non-tag */
		if (nColumns == 2 && column == 0)
			value = getNonTag(mp);
		printLog(stdout, myitems_description[nColumns][column], value ? value : "");
		clearMembuf(mp);
	}
	return ret;
}

/*
 * TODO: allow user configuration of myItems.
 */
int
printMyItems(void)
{
	memBuf_t *mp;
	const char *table;
	char **row;
	auctionInfo *dummy = newAuctionInfo("0", "0");

	if (ebayLogin(dummy)) {
		printAuctionError(dummy, stderr);
		freeAuction(dummy);
		return 1;
	}
	if (!(mp = httpGet(MYITEMS_URL, NULL))) {
		auctionError(dummy, ae_curlerror, NULL);
		printAuctionError(dummy, stderr);
		freeAuction(dummy);
		return 1;
	}
	while ((table = getTableStart(mp))) {
		int printNewline = 0;

		/* search for table containing my itmes */
		if (!strstr(table, "tableName=\"Watching\""))
			continue;
		/* skip first descriptive table row */
		if ((row = getTableRow(mp)))
			freeTableRow(row);
		else {
			freeAuction(dummy);
			return 0; /* error? */
		}
		while ((row = getTableRow(mp))) {
			printNewline = printMyItemsRow(row, printNewline);
			freeTableRow(row);
		}
	}
	freeAuction(dummy);
	return 0;
}

#if DEBUG
/* secret option - test parser */
void
testParser(int flag)
{
	memBuf_t *mp = readFile(stdin);

	switch (flag) {
	case 1:
	    {
		/* print pagename */
		char *line;

		/* dump non-tag data */
		while ((line = getNonTag(mp)))
			printf("\"%s\"\n", line);

		/* pagename? */
		mp->readptr = mp->memory;
		if ((line = getPageName(mp)))
			printf("\nPAGENAME is \"%s\"\n", line);
		else
			printf("\nPAGENAME is NULL\n");
		break;
	    }
	case 2:
	    {
		/* run through bid history parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		int ret = parseBidHistory(mp, aip, options.username, time(NULL), NULL);

		printf("ret = %d\n", ret);
		printAuctionError(aip, stdout);
		break;
	    }
	case 3:
	    {
		/* run through bid result parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		int ret = parseBid(mp, aip);

		printf("ret = %d\n", ret);
		printAuctionError(aip, stdout);
		break;
	    }
	case 4:
	    {
		/* print bid history table */
		const char *table;
		char **row;
		char *cp;
		int rowNum = 0;

		while ((cp = getNonTag(mp))) {
			if (!strcmp(cp, "Time left:"))
				break;
		}
		if (!cp) {
			printf("time left not found!\n");
			break;
		}
		(void)getTableStart(mp); /* skip one table */
		table = getTableStart(mp);
		if (!table) {
			printf("no table found!\n");
			break;
		}

		printf("table: %s\n", table);
		while ((row = getTableRow(mp))) {
			int columnNum = 0;

			printf("\trow %d:\n", rowNum++);
			for (; row[columnNum]; ++columnNum) {
				memBuf_t *cmp = strToMemBuf(row[columnNum]);
				printf("\t\tcolumn %d: %s\n", columnNum, getNonTag(cmp));
				free(row[columnNum]);
				clearMembuf(cmp);
			}
		}
		break;
	    }
	}
}
#endif
