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
#else
#	include <unistd.h>
#endif

#define newRemain(aip) (aip->endTime - time(NULL) - aip->latency - options.bidtime)

typedef struct {
	char *pageName;
	char *pageId;
	char *srcId;
} pageInfo_t;

static int match(memBuf_t *mp, const char *str);
static const char *gettag(memBuf_t *mp);
static char *getnontag(memBuf_t *mp);
static long getseconds(char *timestr);
static const char *getTableEnd(memBuf_t *mp);
static char *getTableCell(memBuf_t *mp);
static char **getTableRow(memBuf_t *mp);
static const char *getTableStart(memBuf_t *mp);
static int getInfoTiming(auctionInfo *aip, int quantity, const char *user, time_t *timeToFirstByte);
static char *getPageName(memBuf_t *mp);
static char *getIdInternal(char *s, size_t len);
static char *getPageNameInternal(char *var);
static pageInfo_t *getPageInfo(memBuf_t *mp);
static void freePageInfo(pageInfo_t *pp);

static int getQuantity(int want, int available);
static int parseAuction(memBuf_t *mp, auctionInfo *aip, int quantity, const char *user, time_t *timeToFirstByte);
static int parseAuctionInternal(memBuf_t *mp, auctionInfo *aip, int quantity, const char *user, char **currently, char **winner);
static int parseBid(memBuf_t *mp, auctionInfo *aip);

/*
 * attempt to match some input, ignoring \r and \n
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
		if ((char)c == *cursor) {
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
	while ((line = gettag(mp))) {
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
static pageInfo_t *
getPageInfo(memBuf_t *mp)
{
	const char *line;
	pageInfo_t p = {NULL, NULL, NULL}, *pp;
	int needPageName = 1;
	int needPageId = 1;
	int needSrcId = 1;
	int needMore = 3;

	log(("getPageInfo():\n"));
	while (needMore && (line = gettag(mp))) {
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
static void
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
gettag(memBuf_t *mp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;
	int inStr = 0, comment = 0, c;

	if (memEof(mp)) {
		log(("gettag(): returning NULL\n"));
		return NULL;
	}
	while ((c = memGetc(mp)) != EOF && c != '<')
		;
	if (c == EOF) {
		log(("gettag(): returning NULL\n"));
		return NULL;
	}

	/* first char - check for comment */
	c = memGetc(mp);
	if (c == '>') {
		log(("gettag(): returning empty tag\n"));
		return "";
	} else if (c == EOF) {
		log(("gettag(): returning NULL\n"));
		return NULL;
	}
	addchar(buf, bufsize, count, c);
	if (c == '!') {
		int c2 = memGetc(mp);

		if (c2 == '>' || c2 == EOF) {
			term(buf, bufsize, count);
			log(("gettag(): returning %s\n", buf));
			return buf;
		}
		addchar(buf, bufsize, count, c2);
		if (c2 == '-') {
			int c3 = memGetc(mp);

			if (c3 == '>' || c3 == EOF) {
				term(buf, bufsize, count);
				log(("gettag(): returning %s\n", buf));
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
				log(("gettag(): returning %s\n", buf));
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
					log(("gettag(): returning %s\n", buf));
					return buf;
				}
				addchar(buf, bufsize, count, c);
				break;
			case '>':
				if (inStr)
					addchar(buf, bufsize, count, c);
				else {
					term(buf, bufsize, count);
					log(("gettag(): returning %s\n", buf));
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
	log(("gettag(): returning %s\n", count ? buf : "NULL"));
	return count ? buf : NULL;
}

/*
 * Get next non-tag text, eliminating leading and trailing whitespace
 * and leaving only a single space for all internal whitespace.
 */
static char *
getnontag(memBuf_t *mp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0, amp = 0;
	int c;

	if (memEof(mp)) {
		log(("getnontag(): returning NULL\n"));
		return NULL;
	}
	while ((c = memGetc(mp)) != EOF) {
		c &= 0x7F;	/* eBay throws in 8th bit to screw things up */
		switch (c) {
		case '<':
			memUngetc(c, mp);
			if (count) {
				if (buf[count-1] == ' ')
					--count;
				term(buf, bufsize, count);
				log(("getnontag(): returning %s\n", buf));
				return buf;
			} else
				gettag(mp);
			break;
		case ' ':
		case '\n':
		case '\r':
		case '\t':
		case '\v':
			if (count > 0 && buf[count-1] != ' ')
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
	term(buf, bufsize, count);
	log(("getnontag(): returning %s\n", count ? buf : "NULL"));
	return count ? buf : NULL;
} /* getnontag() */

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
getseconds(char *timestr)
{
	static char second[] = "sec";
	static char minute[] = "min";
	static char hour[] = "hour";
	static char day[] = "day";
	static char ended[] = "ended";
	long accum = 0;
	long num;

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

	while ((cp = gettag(mp))) {
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
	const char *cp, *start = NULL, *end = NULL;
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;

	while ((cp = gettag(mp))) {
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
 * Search for next table tag.
 */
static const char *
getTableStart(memBuf_t *mp)
{
	const char *cp;

	while ((cp = gettag(mp))) {
		if (!strncmp(cp, "table", 5) &&
		    (isspace((int)*(cp+5)) || *(cp+5) == '\0'))
			return cp;
	}
	return NULL;
}

/*
 * parseAuction(): parses bid history page (pageName: PageViewBids)
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) - sets auctionError
 */
static int
parseAuction(memBuf_t *mp, auctionInfo *aip, int quantity, const char *user, time_t *timeToFirstByte)
{
	char *line, *currently = NULL, *winner = NULL;
	int ret;

	resetAuctionError(aip);

	if (timeToFirstByte) {
		*timeToFirstByte = getTimeToFirstByte(mp);
	}

	/*
	 * Auction title
	 */
	while ((line = getnontag(mp))) {
		if (!strcmp(line, "Bid History"))
			break;
		if (!strcmp(line, "Unknown Item"))
			return auctionError(aip, ae_baditem, NULL);
	}
	if (!line)
		return auctionError(aip, ae_notitle, NULL);

	ret = parseAuctionInternal(mp, aip, quantity, user, &currently, &winner);
	free(currently);
	free(winner);
	return ret;
} /* parseAuction() */

const char PRIVATE[] = "private auction - bidders' identities protected";

static int
parseAuctionInternal(memBuf_t *mp, auctionInfo *aip, int quantity, const char *user, char **currently, char **winner)
{
	char *line;
	char *title;
	int reserve = 0;	/* 1 = reserve not met */
        long remain;		/* time until auction ends */

	/*
	 * Auction item
	 */
	while ((line = getnontag(mp))) {
		if (!strcmp(line, "Item title:")) {
			line = getnontag(mp);
			break;
		}
	}
	if (!line)
		return auctionError(aip, ae_notitle, NULL);
	title = myStrdup(line);
	while ((line = getnontag(mp))) {
		char *tmp;

		if (line[strlen(line) - 1] == ':')
			break;
		tmp = title;
		title = myStrdup2(tmp, line);
		free(tmp);
	}
	printLog(stdout, "Auction %s: %s\n", aip->auction, title);
	free(title);

	/*
	 * Quantity/Current price/Time remaining
	 */
	for (; line; line = getnontag(mp)) {
		if (!strcmp("Quantity:", line)) {
			line = getnontag(mp);
			if (!line || (aip->quantity = atoi(line)) < 1)
				return auctionError(aip, ae_noquantity, NULL);
			log(("quantity: %d", aip->quantity));
		} else if (!strcmp("Currently:", line)) {
			line = getnontag(mp);
			if (!line)
				return auctionError(aip, ae_noprice, NULL);
			*currently = myStrdup(line);
			log(("Currently: %s  (your maximum bid: %s)\n", line, aip->bidPriceStr));
			aip->price = atof(priceFixup(line, aip));
			if (aip->price < 0.01)
				return auctionError(aip, ae_convprice, line);
		} else if (!strcmp("Time left:", line)) {
			line = getnontag(mp);
			break;
		}
	}
	if (!line)
		return auctionError(aip, ae_notime, NULL);
	if ((remain = getseconds(line)) < 0)
		return auctionError(aip, ae_badtime, line);
	printLog(stdout, "Time remaining: %s (%ld seconds)\n", line, remain);
        aip->endTime = remain + time(NULL);
	/* no \n needed -- ctime returns a string with \n at the end */
	printLog(stdout, "End time: %s", ctime(&(aip->endTime)));

	if (!(line = getnontag(mp)))
		return auctionError(aip, ae_nohighbid, NULL);
	if (!strcmp("Reserve not met", line))
		reserve = 1;

	/*
	 * Determine high bidder
	 *
	 * Format of high bidder table is:
	 *
	 *	Single item auction:
	 *	    Header line:
	 *		"User ID"
	 *		"Bid Amount"
	 *		"Date of bid"
	 *	    For each bid:
	 *			<user>
	 *			"("
	 *			<feedback #>
	 *			")"
	 *			<amount>
	 *			<date>
	 *
	 *	    If the auction is private, the user names are:
	 *		"private auction - bidders' identities protected"
	 *
	 *	Dutch auction:
	 *	    Header line:
	 *		"User ID"
	 *		"Bid Amount"
	 *		"Quantity wanted"
	 *		"Quantity winning"
	 *		"Date of Bid"
	 *	    For each bid:
	 *			<user>
	 *			"("
	 *			<feedback #>
	 *			")"
	 *			<amount>
	 *			<quantity wanted>
	 *			<quantity winning>
	 *			<date>
	 *
	 *	    Dutch auctions cannot be private.
	 *
	 *	If there are no bids, the text "No bids have been placed."
	 *	will be the first entry in the table.
	 */
	/* header line */
	while ((line = getnontag(mp))) {
		if (!strcmp("User ID", line)) {
			getnontag(mp);	/* Bid Amount */
			if (aip->quantity > 1) {
				getnontag(mp);	/* Quantity wanted */
				getnontag(mp);	/* Quantity winning */
			}
			getnontag(mp);	/* Date of Bid */
			break;
		}
	}
	if (!(line = getnontag(mp)))
		return auctionError(aip, ae_nohighbid, NULL);
	if (!strcmp("No bids have been placed.", line)) {
		aip->bids = 0;
		/* can't determine starting bid on history page */
		aip->price = 0;
		printf("# of bids: 0\n"
		       "Currently: --  (your maximum bid: %s)\n"
		       "High bidder: -- (not %s)\n",
		       aip->bidPriceStr, user);
	} else if (aip->quantity == 1) {	/* single auction with bids */
		int private = 0;

		if (!strcmp(line, PRIVATE))
			private = 1;
		else
			*winner = myStrdup(line);
		aip->bids = 1;
		if (!private) {
			getnontag(mp);	/* "(" */
			getnontag(mp);	/* <feedback> */
			getnontag(mp);	/* ")" */
		}
		*currently = myStrdup(getnontag(mp));	/* bid amount */
		aip->price = atof(priceFixup(line, aip));
		if (aip->price < 0.01)
			return auctionError(aip, ae_convprice, line);
		if (private) {
			*winner = (aip->price <= aip->bidPrice &&
				   (aip->bidResult == 0 ||
				    (aip->bidResult == -1 &&
				     aip->endTime - time(NULL) < options.bidtime))) ?
					myStrdup(user) : myStrdup("[private]");
		}
		getnontag(mp); /* date */
		/* count number of bids */
		for (;;) {
			if (private) {
				line = getnontag(mp); /* UserID */
				if (!line || strcmp(line, PRIVATE))
					break;
			} else {
				getnontag(mp); /* UserID */
				line = getnontag(mp); /* "(" */
				if (!line || strcmp("(", line))
					break;
				getnontag(mp); /* <feedback> */
				getnontag(mp); /* ")" */
			}
			++aip->bids;
			getnontag(mp); /* bid amount */
			getnontag(mp); /* date */
		}
		printf("Currently: %s  (your maximum bid: %s)\n"
		       "# of bids: %d\n",
		       *currently, aip->bidPriceStr, aip->bids);
		if (strcasecmp(*winner, user)) {
			printLog(stdout, "High bidder: %s (NOT %s)\n", *winner, user);
			aip->winning = 0;
			if (!remain)
				aip->won = 0;
		} else {
			printLog(stdout, "High bidder: %s!!!\n", *winner);
			aip->winning = 1;
			if (!remain)
				aip->won = 1;
		}
	} else {	/* dutch with bids */
		int gotMatch = 0, wanted = 0, winning = 0;

		printf("Currently: %s  (your maximum bid: %s)\n", *currently, aip->bidPriceStr);

		aip->bids = 0;
		/* find your bid, count number of bids */
		do {
			if (!gotMatch) {
				free(*winner);
				*winner = myStrdup(line);
			}
			line = getnontag(mp); /* "(" */
			if (strcmp("(", line))
				break;
			++aip->bids;
			getnontag(mp); /* <feedback> */
			getnontag(mp); /* ")" */
			getnontag(mp); /* <price> */
			if (!strcasecmp(*winner, user)) {
				gotMatch = 1;
				wanted = atoi(getnontag(mp)); /* <quantity wanted> */
				winning = atoi(getnontag(mp)); /* <quantity winning> */
			} else {
				getnontag(mp); /* <quantity wanted> */
				getnontag(mp); /* <quantity winning> */
			}
			getnontag(mp); /* <date> */
			line = getnontag(mp); /* <date> */
		} while (line);
		printf("# of bids: %d\n", aip->bids);
		aip->winning = winning;
		if (!remain)
			aip->won = winning;
		if (winning > 0) {
			if (winning == wanted)
				printLog(stdout, "High bidder: %s!!!\n", user);
			else
				printLog(stdout, "High bidder: %s!!! (%d out of %d items)\n", user, winning, wanted);
		} else
			printLog(stdout, "High bidder: various dutch bidders (NOT %s)\n", user);
	}

	return 0;
} /* parseAuctionInternal() */

static const char GETINFO[] = "http://offer.ebay.com/aw-cgi/eBayISAPI.dll?ViewBids&item=";

/*
 * getInfo(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
int
getInfo(auctionInfo *aip, int quantity, const char *user)
{
	return getInfoTiming(aip, quantity, user, NULL);
}

/*
 * getInfoTiming(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
int
getInfoTiming(auctionInfo *aip, int quantity, const char *user, time_t *timeToFirstByte)
{
	memBuf_t *mp;
	int ret;

	log(("\n\n*** getInfo auction %s price %s user %s\n", aip->auction, aip->bidPriceStr, user));

	if (!aip->query)
		aip->query = myStrdup2(GETINFO, aip->auction);
	if (!(mp = httpGet(aip, aip->query, NULL)))
		return 1;

	ret = parseAuction(mp, aip, quantity, user, timeToFirstByte);
	clearMembuf(mp);
	return ret;
}

/*
 * Note: quant=1 is just to dupe eBay into allowing the pre-bid to get
 *	 through.  Actual quantity will be sent with bid.
 */
static const char PRE_BID_URL[] = "http://offer.ebay.com/ws/eBayISAPI.dll?MakeBid&item=%s&maxbid=%s&quant=%s";

/*
 * Get bid key
 *
 * returns 0 on success, 1 on failure.
 */
int
preBid(auctionInfo *aip)
{
	memBuf_t *mp;
	int quantity = getQuantity(aip->quantity, options.quantity);
	char quantityStr[12];	/* must hold an int */
	size_t urlLen;
	char *url;
	int ret = 0;

	sprintf(quantityStr, "%d", quantity);
	urlLen = sizeof(PRE_BID_URL) + strlen(aip->auction) + strlen(aip->bidPriceStr) + strlen(quantityStr) - 6;
	url = (char *)myMalloc(urlLen);
	sprintf(url, PRE_BID_URL, aip->auction, aip->bidPriceStr, quantityStr);
	log(("\n\n*** preBid(): url is %s\n", url));
	mp = httpGet(aip, url, NULL);
	free(url);
	if (!mp)
		return 1;

	if (match(mp, "<input type=\"hidden\" name=\"key\" value=\"")) {
		ret = auctionError(aip, ae_bidkey, NULL);
		bugReport("preBid", __FILE__, __LINE__, mp, "cannot find bid key");
	} else {
		char *cp, *tmpkey;

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
	}

	free(aip->bidpass);
	if (match(mp, "<input type=\"hidden\" name=\"pass\" value=\"")) {
		log(("preBid(): cannot find bid password, will use user's password instead"));
		aip->bidpass = NULL;
	} else {
		aip->bidpass = myStrdup(getUntil(mp, '\"'));
		log(("preBid(): bidpass is \"%s\"", aip->bidpass));
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
int
ebayLogin(auctionInfo *aip)
{
	memBuf_t *mp;
	size_t urlLen;
	char *url, *logUrl;
	pageInfo_t *pp;
	int ret = 0;

	mp = httpGet(aip, LOGIN_1_URL, NULL);
	if (!mp)
		return 1;

	clearMembuf(mp);

	urlLen = sizeof(LOGIN_2_URL) + strlen(options.username);
	url = malloc(urlLen + strlen(getPassword()));
	logUrl = malloc(urlLen + 5);

	sprintf(url, LOGIN_2_URL, options.username, getPassword());
	sprintf(logUrl, LOGIN_2_URL, options.username, "*****");

	mp = httpGet(aip, url, logUrl);
	free(url);
	free(logUrl);

	if ((pp = getPageInfo(mp))) {
		log(("ebayLogin(): pagename = \"%s\", pageid = \"%s\", srcid = \"%s\"", nullStr(pp->pageName), nullStr(pp->pageId), nullStr(pp->srcId)));
		if (pp->srcId && !strcmp(pp->srcId, "SignInAlertSupressor"))
			aip->loginTime = time(NULL);
		else if (pp->pageName && !strcmp(pp->pageName, "PageSignIn"))
			ret = auctionError(aip, ae_login, NULL);
		else {
			ret = auctionError(aip, ae_login, NULL);
			bugReport("ebayLogin", __FILE__, __LINE__, mp, "pagename = \"%s\", pageid = \"%s\", srcid = \"%s\"", nullStr(pp->pageName), nullStr(pp->pageId), nullStr(pp->srcId));
		}
	} else {
		log(("ebayLogin(): pageinfo is NULL\n"));
		ret = auctionError(aip, ae_login, NULL);
		bugReport("ebayLogin", __FILE__, __LINE__, mp, "pageinfo is NULL");
	}
	clearMembuf(mp);
	freePageInfo(pp);
	return ret;
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
	// The following sometimes have more characters after them, for
	// example AcceptBid_HighBidder_rebid (you were already the high
	// bidder and placed another bid).
	static const char HIGHBID[] = "AcceptBid_HighBidder";
	static const char OUTBID[] = "AcceptBid_Outbid";
	static const char RESERVENOTMET[] = "AcceptBid_ReserveNotMet";
	static const char MAKEBID[] = "PageMakeBid";
	char *pagename;

	aip->bidResult = -1;
	if ((pagename = getPageName(mp))) {
		log(("parseBid(): pagename = %s\n", pagename));
		if (!strncmp(pagename, HIGHBID, sizeof(HIGHBID) - 1))
			return aip->bidResult = 0;
		if (!strncmp(pagename, OUTBID, sizeof(OUTBID) - 1))
			return aip->bidResult = auctionError(aip, ae_outbid, NULL);
		if (!strncmp(pagename, RESERVENOTMET, sizeof(RESERVENOTMET)-1))
			return aip->bidResult = auctionError(aip, ae_reservenotmet, NULL);
		if (!strcmp(pagename, "MakeBidErrorMinBid"))
			return aip->bidResult = auctionError(aip, ae_bidprice, NULL);
		if (!strcmp(pagename, "MakeBidErrorPassword") ||
		    !strncmp(pagename, MAKEBID, sizeof(MAKEBID) - 1))
			return aip->bidResult = auctionError(aip, ae_badpass, NULL);
		if (!strcmp(pagename, "MakeBidError"))
			return aip->bidResult = auctionError(aip, ae_ended, NULL);
		bugReport("parseBid", __FILE__, __LINE__, mp, "pagename is \"%s\"", pagename);
	} else {
		log(("parseBid(): pagename is NULL\n"));
		bugReport("parseBid", __FILE__, __LINE__, mp, "pagename is NULL");
	}
	printLog(stdout, "Cannot determine result of bid\n");
	return 0;	/* prevent another bid */
} /* parseBid() */

static const char BID_URL[] = "http://offer.ebay.com/ws/eBayISAPI.dll?AcceptBid&item=%s&key=%s&maxbid=%s&quant=%s&user=%s&pass=%s&mode=1";

/*
 * Place bid.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
int
bid(auctionInfo *aip)
{
	memBuf_t *mp;
	size_t urlLen, passwordLen;
	char *url, *logUrl, *tmpUsername, *tmpPassword, *password;
	int ret;
	int quantity = getQuantity(aip->quantity, options.quantity);
	char quantityStr[12];	/* must hold an int */

	sprintf(quantityStr, "%d", quantity);

	/* create url */
	password = aip->bidpass ? aip->bidpass : getPassword();
	passwordLen = strlen(password);
	urlLen = sizeof(BID_URL) + strlen(aip->auction) + strlen(aip->bidkey) + strlen(aip->bidPriceStr) + strlen(quantityStr) + strlen(options.username) + passwordLen - 12;
	url = (char *)myMalloc(urlLen);
	sprintf(url, BID_URL, aip->auction, aip->bidkey, aip->bidPriceStr, quantityStr, options.username, password);
	if (!aip->bidpass)
		freePassword(password);

	logUrl = (char *)myMalloc(urlLen);
	tmpUsername = stars(strlen(options.username));
	tmpPassword = stars(passwordLen);
	sprintf(logUrl, BID_URL, aip->auction, aip->bidkey, aip->bidPriceStr, quantityStr, tmpUsername, tmpPassword);
	free(tmpUsername);
	free(tmpPassword);

	if (!options.bid) {
		printLog(stdout, "Bidding disabled\n");
		log(("\n\nbid(): query url:\n%s\n", logUrl));
		ret = aip->bidResult = 0;
	} else if (!(mp = httpGet(aip, url, logUrl))) {
		ret = 1;
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
int
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
		int ret = getInfoTiming(aip, options.quantity, options.username, &timeToFirstByte);
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
			} else if (remain == LONG_MIN) { /* first time */
				int j;

				for (j = 0; ret && j < 3 && aip->auctionError == ae_notitle; ++j) {
					ret = getInfo(aip, options.quantity,
						      options.username);
                                }
				if (!ret)
					remain = newRemain(aip);
				else
					return 1;
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
		if ((remain <= 300) && ((time(NULL) - aip->loginTime) > 300)) {
			cleanupCurlStuff();
			initCurlStuff();
			ebayLogin(aip);
			remain = newRemain(aip);
		}

		/*
		 * if we're less than two minutes away, get bid key
		 */
		if (remain <= 150 && !aip->bidkey) {
			int i;

			printf("\n");
			for (i = 0; i < 5; ++i) {
				if (!preBid(aip))
					break;
			}
			if (i == 5) {
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

#if DEBUG
/* secret option - test parser */
void
testParser(int flag)
{
	memBuf_t *mp = readFile(stdin);

	switch (flag) {
	case 1:
	    {
		char *line;

		/* dump non-tag data */
		while ((line = getnontag(mp)))
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
		int ret = parseAuction(mp, aip, 1, options.username, NULL);

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
		/* find the watching table */
		const char *table;
		char **row;

		while ((table = getTableStart(mp))) {
			int rowNum = 0;

			if (!strstr(table, "tableName=\"Watching\""))
				continue;
			printf("table: %s\n", table);
			while ((row = getTableRow(mp))) {
				int columnNum = 0;

				printf("\trow %d:\n", rowNum++);
				for (; row[columnNum]; ++columnNum) {
					printf("\t\tcolumn %d: %s\n", columnNum, row[columnNum]);
					free(row[columnNum]);
				}
			}
		}
		break;
	    }
	}
}
#endif
