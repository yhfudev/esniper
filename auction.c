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
#include <string.h>
#include <sys/types.h>
#include <time.h>
#if defined(WIN32)
#	include <stdlib.h>
#	define strcasecmp(s1, s2) stricmp((s1), (s2))
#	define sleep(t) _sleep((t) * 1000)
#else
#	include <unistd.h>
#endif

static int match(FILE *fp, const char *str);
static const char *gettag(FILE *fp);
static char *getnontag(FILE *fp);
static long getseconds(char *timestr);
static int getInfoTiming(auctionInfo *aip, int quantity, const char *user, time_t *timeToFirstByte);

static int getQuantity(int want, int available);
static int parseAuction(FILE *fp, auctionInfo *aip, int quantity, const char *user, time_t *timeToFirstByte);
static int parseAuctionInternal(FILE *fp, auctionInfo *aip, int quantity, const char *user, char **currently, char **winner);
static int parseBid(FILE *fp, auctionInfo *aip);

/*
 * attempt to match some input, ignoring \r and \n
 * returns 0 on success, -1 on failure
 */
static int
match(FILE *fp, const char *str)
{
	const char *cursor;
	int c;

	log(("\n\nmatch(\"%s\")\n\n", str));

	cursor = str;
	while ((c = getc(fp)) != EOF) {
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

/*
 * Get next tag text, eliminating leading and trailing whitespace
 * and leaving only a single space for all internal whitespace.
 */
static const char *
gettag(FILE *fp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;
	int inStr = 0, comment = 0, c;

	if (feof(fp)) {
		log(("gettag(): returning NULL\n"));
		return NULL;
	}
	while ((c = getc(fp)) != EOF && c != '<')
		;
	if (c == EOF) {
		log(("gettag(): returning NULL\n"));
		return NULL;
	}

	/* first char - check for comment */
	c = getc(fp);
	if (c == '>') {
		log(("gettag(): returning empty tag\n"));
		return "";
	} else if (c == EOF) {
		log(("gettag(): returning NULL\n"));
		return NULL;
	}
	addchar(buf, bufsize, count, c);
	if (c == '!') {
		int c2 = getc(fp);

		if (c2 == '>' || c2 == EOF) {
			term(buf, bufsize, count);
			log(("gettag(): returning %s\n", buf));
			return buf;
		}
		addchar(buf, bufsize, count, c2);
		if (c2 == '-') {
			int c3 = getc(fp);

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
		while ((c = getc(fp)) != EOF) {
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
		while ((c = getc(fp)) != EOF) {
			switch (c) {
			case '\\':
				addchar(buf, bufsize, count, c);
				c = getc(fp);
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
getnontag(FILE *fp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0, amp = 0;
	int c;

	if (feof(fp)) {
		log(("getnontag(): returning NULL\n"));
		return NULL;
	}
	while ((c = getc(fp)) != EOF) {
		c &= 0x7F;	/* eBay throws in 8th bit to screw things up */
		switch (c) {
		case '<':
			ungetc(c, fp);
			if (count) {
				if (buf[count-1] == ' ')
					--count;
				term(buf, bufsize, count);
				log(("getnontag(): returning %s\n", buf));
				return buf;
			} else
				gettag(fp);
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
 * parseAuction(): parses bid history page (pageName: PageViewBids)
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) - sets auctionError
 */
static int
parseAuction(FILE *fp, auctionInfo *aip, int quantity, const char *user, time_t *timeToFirstByte)
{
	char *line, *currently = NULL, *winner = NULL;
	int ret;

	resetAuctionError(aip);

	/*
	 * Auction title
	 */
	while ((line = getnontag(fp))) {
		if (timeToFirstByte) {
			*timeToFirstByte = time(NULL);
			timeToFirstByte = NULL;
		}
		if (!strcmp(line, "Bid History"))
			break;
		if (!strcmp(line, "Unknown Item"))
			return auctionError(aip, ae_baditem, NULL);
	}
	if (!line)
		return auctionError(aip, ae_notitle, NULL);

	ret = parseAuctionInternal(fp, aip, quantity, user, &currently, &winner);
	free(currently);
	free(winner);
	return ret;
} /* parseAuction() */

const char PRIVATE[] = "private auction - bidders' identities protected";

static int
parseAuctionInternal(FILE *fp, auctionInfo *aip, int quantity, const char *user, char **currently, char **winner)
{
	char *line;
	char *title;
	int reserve = 0;	/* 1 = reserve not met */

	/*
	 * Auction item
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp(line, "Item title:")) {
			line = getnontag(fp);
			break;
		}
	}
	if (!line)
		return auctionError(aip, ae_notitle, NULL);
	title = myStrdup(line);
	while ((line = getnontag(fp))) {
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
	for (; line; line = getnontag(fp)) {
		if (!strcmp("Quantity:", line)) {
			line = getnontag(fp);
			if (!line || (aip->quantity = atoi(line)) < 1)
				return auctionError(aip, ae_noquantity, NULL);
			log(("quantity: %d", aip->quantity));
		} else if (!strcmp("Currently:", line)) {
			line = getnontag(fp);
			if (!line)
				return auctionError(aip, ae_noprice, NULL);
			*currently = myStrdup(line);
			log(("Currently: %s  (your maximum bid: %s)\n", line, aip->bidPriceStr));
			aip->price = atof(priceFixup(line, aip));
			if (aip->price < 0.01)
				return auctionError(aip, ae_convprice, line);
		} else if (!strcmp("Time left:", line)) {
			line = getnontag(fp);
			break;
		}
	}
	if (!line)
		return auctionError(aip, ae_notime, NULL);
	if ((aip->remain = getseconds(line)) < 0)
		return auctionError(aip, ae_badtime, line);
	printLog(stdout, "Time remaining: %s (%ld seconds)\n",
		line, aip->remain);

	if (!(line = getnontag(fp)))
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
	while ((line = getnontag(fp))) {
		if (!strcmp("User ID", line)) {
			getnontag(fp);	/* Bid Amount */
			if (aip->quantity > 1) {
				getnontag(fp);	/* Quantity wanted */
				getnontag(fp);	/* Quantity winning */
			}
			getnontag(fp);	/* Date of Bid */
			break;
		}
	}
	if (!(line = getnontag(fp)))
		return auctionError(aip, ae_nohighbid, NULL);
	if (!strcmp("No bids have been placed.", line)) {
		aip->bids = 0;
		/* can't determine starting bid on history page */
		aip->price = 0;
		puts("# of bids: 0");
		puts("Currently: --");
		printf("High bidder: -- (not %s)\n", user);
	} else if (aip->quantity == 1) {	/* single auction with bids */
		int private = 0;

		if (!strcmp(line, PRIVATE))
			private = 1;
		else
			*winner = myStrdup(line);
		aip->bids = 1;
		if (!private) {
			getnontag(fp);	/* "(" */
			getnontag(fp);	/* <feedback> */
			getnontag(fp);	/* ")" */
		}
		*currently = myStrdup(getnontag(fp));	/* bid amount */
		aip->price = atof(priceFixup(line, aip));
		if (aip->price < 0.01)
			return auctionError(aip, ae_convprice, line);
		if (private) {
			*winner = (aip->price <= aip->bidPrice &&
				   (aip->bidResult == 0 ||
				    (aip->bidResult == -1 &&
				     aip->remain < options.bidtime))) ?
					myStrdup(user) : myStrdup("[private]");
		}
		getnontag(fp); /* date */
		/* count number of bids */
		for (;;) {
			if (private) {
				line = getnontag(fp); /* UserID */
				if (!line || strcmp(line, PRIVATE))
					break;
			} else {
				getnontag(fp); /* UserID */
				line = getnontag(fp); /* "(" */
				if (!line || strcmp("(", line))
					break;
				getnontag(fp); /* <feedback> */
				getnontag(fp); /* ")" */
			}
			++aip->bids;
			getnontag(fp); /* bid amount */
			getnontag(fp); /* date */
		}
		printf("Currently: %s\n", *currently);
		printf("# of bids: %d\n", aip->bids);
		if (strcasecmp(*winner, user)) {
			printLog(stdout, "High bidder: %s (NOT %s)\n", *winner, user);
			if (!aip->remain)
				aip->won = 0;
		} else {
			printLog(stdout, "High bidder: %s!!!\n", *winner);
			if (!aip->remain)
				aip->won = 1;
		}
	} else {	/* dutch with bids */
		int gotMatch = 0, wanted = 0, winning = 0;

		printf("Currently: %s\n", *currently);

		aip->bids = 0;
		/* find your bid, count number of bids */
		do {
			if (!gotMatch) {
				free(*winner);
				*winner = myStrdup(line);
			}
			line = getnontag(fp); /* "(" */
			if (strcmp("(", line))
				break;
			++aip->bids;
			getnontag(fp); /* <feedback> */
			getnontag(fp); /* ")" */
			getnontag(fp); /* <price> */
			if (!strcasecmp(*winner, user)) {
				gotMatch = 1;
				wanted = atoi(getnontag(fp)); /* <quantity wanted> */
				winning = atoi(getnontag(fp)); /* <quantity winning> */
			} else {
				getnontag(fp); /* <quantity wanted> */
				getnontag(fp); /* <quantity winning> */
			}
			getnontag(fp); /* <date> */
			line = getnontag(fp); /* <date> */
		} while (line);
		printf("# of bids: %d\n", aip->bids);
		if (!aip->remain)
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

static const char GETINFO[] = "aw-cgi/eBayISAPI.dll?ViewBids&item=";

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
	FILE *fp;
	int ret;

	log(("\n\n*** getInfo auction %s price %s user %s\n", aip->auction, aip->bidPriceStr, user));

	if (!aip->host)
		aip->host = myStrdup(options.historyHost);
	if (!aip->query)
		aip->query = myStrdup2(GETINFO, aip->auction);
	if (!(fp = httpGet(aip, aip->host, aip->query, NULL, 1)))
		return 1;

	ret = parseAuction(fp, aip, quantity, user, timeToFirstByte);
	closeSocket(fp);
	return ret;
}

/*
 * Note: quant=1 is just to dupe eBay into allowing the pre-bid to get
 *	 through.  Actual quantity will be sent with bid.
 */
static const char PRE_BID_URL[] = "ws/eBayISAPI.dll";
static const char PRE_BID_DATA[] = "MfcISAPICommand=MakeBid&item=%s&maxbid=%s&quant=%s";

/*
 * Get key for bid
 *
 * returns 0 on success, 1 on failure.
 */
int
preBid(auctionInfo *aip)
{
	FILE *fp;
	size_t dataLen;
	int quantity = getQuantity(aip->quantity, options.quantity);
	char quantityStr[12];	/* must hold an int */
	char *data;
	int ret = 0;

	sprintf(quantityStr, "%d", quantity);
	dataLen = sizeof(PRE_BID_DATA) + strlen(aip->auction) + strlen(aip->bidPriceStr) + strlen(quantityStr) - 6;
	data = (char *)myMalloc(dataLen);
	sprintf(data, PRE_BID_DATA, aip->auction, aip->bidPriceStr, quantityStr);
	log(("\n\n*** preBid(): data is %s\n", data));
	fp = httpPost(aip, options.prebidHost, PRE_BID_URL, "", data, NULL, 0);
	free(data);
	if (!fp)
		return 1;

	if (match(fp, "<input type=\"hidden\" name=\"key\" value=\""))
		ret = auctionError(aip, ae_bidkey, NULL);
	else {
		char *cp, *tmpkey;

		tmpkey = getUntil(fp, '\"');
		log(("  reported key is: %s\n", tmpkey));

		/* translate key for URL */
		aip->key = (char *)myMalloc(strlen(tmpkey)*3 + 1);
		for (cp = aip->key; *tmpkey; ++tmpkey) {
			if (*tmpkey == '$') {
				*cp++ = '%';
				*cp++ = '2';
				*cp++ = '4';
			} else
				*cp++ = *tmpkey;
		}
		*cp = '\0';

		log(("\n\ntranslated key is: %s\n\n", aip->key));
	}
	closeSocket(fp);
	return ret;
}

static const char PAGENAME[] = "var pageName = \"";

/*
 * Parse bid result.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
static int
parseBid(FILE *fp, auctionInfo *aip)
{
	const char *line;

	aip->bidResult = -1;
	while ((line = gettag(fp))) {
		char *var, *pagename, *quote;

		if (strncmp(line, "!--", 3) || !(var = strstr(line, PAGENAME)))
			continue;

		pagename = var + sizeof(PAGENAME) - 1;
		quote = strchr(pagename, '"');

		if (!*quote) {
			log(("Cannot find trailing quote in pagename: %s\n", pagename));
			break;
		}

		*quote = '\0';
		log(("parseBid(): pagename = %s\n", pagename));
		if (!strcmp(pagename, "AcceptBid_HighBidder"))
			return aip->bidResult = 0;
		else if (!strcmp(pagename, "AcceptBid_Outbid"))
			return aip->bidResult = auctionError(aip, ae_outbid, NULL);
		else if (!strcmp(pagename, "MakeBidErrorMinBid"))
			return aip->bidResult = auctionError(aip, ae_bidprice, NULL);
		else if (!strcmp(pagename, "AcceptBid_ReserveNotMet"))
			return aip->bidResult = auctionError(aip, ae_reservenotmet, NULL);
		else if (!strcmp(pagename, "MakeBidErrorPassword") ||
			   !strcmp(pagename, "PageMakeBid") ||
			   !strcmp(pagename, "PageMakeBid_signin"))
			return aip->bidResult = auctionError(aip, ae_badpass, NULL);
		else if (!strcmp(pagename, "MakeBidError"))
			return aip->bidResult = auctionError(aip, ae_ended, NULL);
		break;
	}
	printLog(stdout, "Cannot determine result of bid\n");
	return 0;	/* prevent another bid */
} /* parseBid() */

static const char BID_URL[] = "ws/eBayISAPI.dll";
static const char BID_DATA[] = "MfcISAPICommand=AcceptBid&item=%s&key=%s&maxbid=%s&quant=%s&user=%s&pass=%s&mode=1";

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
	FILE *fp;
	size_t dataLen, passwordLen;
	char *data, *logData, *tmpUsername, *tmpPassword, *password;
	int ret;
	int quantity = getQuantity(aip->quantity, options.quantity);
	char quantityStr[12];	/* must hold an int */

	sprintf(quantityStr, "%d", quantity);

	/* create data */
	password = getPassword();
	passwordLen = strlen(password);
	dataLen = sizeof(BID_DATA) + strlen(aip->auction) + strlen(aip->key) + strlen(aip->bidPriceStr) + strlen(quantityStr) + strlen(options.username) + passwordLen - 12;
	data = (char *)myMalloc(dataLen);
	sprintf(data, BID_DATA, aip->auction, aip->key, aip->bidPriceStr, quantityStr, options.username, password);
	freePassword(password);

	logData = (char *)myMalloc(dataLen);
	tmpUsername = stars(strlen(options.username));
	tmpPassword = stars(passwordLen);
	sprintf(logData, BID_DATA, aip->auction, aip->key, aip->bidPriceStr, quantityStr, tmpUsername, tmpPassword);
	free(tmpUsername);
	free(tmpPassword);

	if (!options.bid) {
		printLog(stdout, "Bidding disabled\n");
		log(("\n\nbid(): query data:\n%s\n", logData));
		ret = aip->bidResult = 0;
	} else if (!(fp = httpPost(aip, options.bidHost, BID_URL, "", data, logData, 0)))
		ret = 1;
	else {
		ret = parseBid(fp, aip);
		closeSocket(fp);
	}
	free(data);
	free(logData);
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
		time_t start = time(NULL);
		time_t timeToFirstByte = 0;
		int ret = getInfoTiming(aip, options.quantity, options.username, &timeToFirstByte);
		time_t end = time(NULL);
		time_t latency = (end - start) + (timeToFirstByte - start);

		if (ret) {
			printAuctionError(aip, stderr);

			/*
			 * Fatal error?  We allow up to 50 errors, then quit.
			 * eBay "unavailable" doesn't count towards the total.
			 */
			if (aip->auctionError == ae_unavailable) {
				if (remain >= 0)
					remain -= sleepTime + latency;
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

				for (j = 0; ret && j < 3 && aip->auctionError == ae_notitle; ++j)
					ret = getInfo(aip, options.quantity,
						      options.username);
				if (!ret)
					remain = aip->remain - options.bidtime - latency;
				else
					return 1;
			} else {
				/* non-fatal error */
				log(("ERROR %d!!!\n", ++errorCount));
				if (errorCount > 50)
					return auctionError(aip, ae_toomany, NULL);
				printLog(stdout, "Cannot find auction - internet or eBay problem?\nWill try again after sleep.\n");
				remain -= sleepTime + latency;
			}
		} else if (!isValidBidPrice(aip))
			return auctionError(aip, ae_bidprice, NULL);
		else
			remain = aip->remain - options.bidtime - latency;

		/*
		 * if we're less than two minutes away,
		 * get key for bid
		 */
		if (remain <= 150 && !aip->key) {
			int i;
			time_t keyLatency;

			printf("\n");
			for (i = 0; i < 5; ++i) {
				if (!preBid(aip))
					break;
			}
			if (i == 5) {
				printLog(stderr, "Cannot get bid key\n");
				return 1;
			}
			keyLatency = time(NULL) - end;
			remain -= keyLatency;
		}

		/* it's time!!! */
		if (remain < 0)
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

		if ((long)sleepTime == remain)
			break;
	}

	return 0;
} /* watch() */

#if DEBUG
/* secret option - test parser */
void
testParser(int flag)
{
	switch (flag) {
	case 1:
	    {
		/* dump non-tag data */
		char *line;

		while ((line = getnontag(stdin)))
			printf("\"%s\"\n", line);
		break;
	    }
	case 2:
	    {
		/* run through bid history parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		int ret = parseAuction(stdin, aip, 1, options.username);

		printf("ret = %d\n", ret);
		printAuctionError(aip, stdout);
		break;
	    }
	case 3:
	    {
		/* run through bid result parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		int ret = parseBid(stdin, aip);

		printf("ret = %d\n", ret);
		printAuctionError(aip, stdout);
		break;
	    }
	}
}
#endif
