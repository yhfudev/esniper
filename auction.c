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

#include "auction.h"
#include "buffer.h"
#if defined(WIN32) /* TODO */
#       include <winsock.h>
#else
#       include <unistd.h>
#       include <netinet/in.h>
#       include <sys/socket.h>
#endif
#if defined(_XOPEN_SOURCE_EXTENDED)
#	include <arpa/inet.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>	/* AIX 4.2 strcasecmp() */
#include <time.h>

static int logNonTag(const char *msg, FILE *fp, int ret);
static FILE *verboseConnect(proxy_t *proxy, const char *host, unsigned int retryTime, int retryCount);
static int match(FILE *fp, const char *str);
static char *gettag(FILE *fp);
static char *getnontag(FILE *fp);
static char *getuntilchar(FILE *fp, int until);
static void runout(FILE *fp);
static long getseconds(char *timestr);
static int parseAuction(FILE *fp, auctionInfo *aip, int quantity, const char *user);
static int bidSocket(FILE *fp, auctionInfo *aip, int quantity,
                     const char *user, const char *password);

/*
 * Log the next nontag.
 */
static int
logNonTag(const char *msg, FILE *fp, int ret)
{
	if (options.debug) {
		dlog("%s ", msg);
		dlog("%s\n", getnontag(fp));
	}
	return ret;
}

/*
 * Open a connection to the host.  Return valid FILE * if successful, NULL
 * otherwise
 */
static FILE *
verboseConnect(proxy_t *proxy, const char *host, unsigned int retryTime, int retryCount)
{
	int saveErrno, sockfd = -1, rc = -1, count;
	struct sockaddr_in servAddr;
	struct hostent *entry = NULL;
	static struct sigaction alarmAction;
	static int firstTime = 1;
	const char *connectHost;
	int connectPort;

	/* use proxy? */
	if (proxy->host) {
		connectHost = proxy->host;
		connectPort = proxy->port;
	} else {
		connectHost = host;
		connectPort = 80;
	}

	if (firstTime)
		sigaction(SIGALRM, NULL, &alarmAction);
	for (count = 0; count < 10; count++) {
		if (!(entry = gethostbyname(connectHost))) {
			log(("gethostbyname errno %d\n", h_errno));
			sleep(1);
		} else
			break;
	}
	if (!entry) {
		printLog(stderr, "Cannot convert \"%s\" to IP address\n", connectHost);
		return NULL;
	}
	if (entry->h_addrtype != AF_INET) {
		printLog(stderr, "%s is not an internet host?\n", connectHost);
		return NULL;
	}

	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	memcpy(&servAddr.sin_addr.s_addr, entry->h_addr, (size_t)4);
	servAddr.sin_port = htons((unsigned short)connectPort);

	log(("connecting to %s:%d", connectHost, connectPort));
	while (retryCount-- > 0) {
		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			printLog(stderr, "Socket error %d: %s\n", errno,
				strerror(errno));
			return NULL;
		}
		alarmAction.sa_flags &= ~SA_RESTART;
		sigaction(SIGALRM, &alarmAction, NULL);
		alarm(retryTime);
		rc = connect(sockfd, (struct sockaddr *)&servAddr, (size_t)sizeof(struct sockaddr_in));
		saveErrno = errno;
		alarm(0);
		alarmAction.sa_flags |= SA_RESTART;
		sigaction(SIGALRM, &alarmAction, NULL);
		if (!rc)
			break;
		log(("connect errno %d", saveErrno));
		close(sockfd);
		sockfd = -1;
	}
	if (!rc) {
		log((" OK "));
	}
	return (sockfd >= 0) ? fdopen(sockfd, "a+") : 0;
}

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
static char *
gettag(FILE *fp)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;
	int inStr = 0, comment = 0, c;

	while ((c = getc(fp)) != EOF && c != '<')
		;
	if (c == EOF) {
		log(("gettag(): returning NULL\n"));
		return NULL;
	}

	/* first char - check for comment */
	c = getc(fp);
	if (c == '>' || c == EOF) {
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

	while ((c = getc(fp)) != EOF) {
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
}

static char *
getuntilchar(FILE *fp, int until)
{
	static char buf[1024]; /* returned string cannot be longer than this */
	int count;
	int c;

	log(("\n\ngetuntilchar('%c')\n\n", until));

	count = 0;
	while ((c = getc(fp)) != EOF) {
		if (options.debug)
			logChar(c);
		if (count >= 1024) {
			/* error! too long */
			if (options.debug)
				logChar(EOF);
			return NULL;
		}
		if ((char)c == until) {
			buf[count] = '\0';
			if (options.debug)
				logChar(EOF);
			return buf;
		}
		buf[count++] = (char)c;
	}
	if (options.debug)
		logChar(EOF);
	return NULL;
}

static void
runout(FILE *fp)
{
	int c, count;

	if (options.debug) {
		dlog("\n\nrunout()\n\n");
		for (count = 0, c = getc(fp); c != EOF; ++count, c = getc(fp))
			logChar(c);
		logChar(EOF);
		dlog("%d bytes", count);
	} else {
		for (c = getc(fp); c != EOF; c = getc(fp))
			;
	}
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
 * parseAuction(): parses bid history page
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) - sets auctionError
 */
static int
parseAuction(FILE *fp, auctionInfo *aip, int quantity, const char *user)
{
	char *line, *s1;

	resetAuctionError(aip);

	/*
	 * Auction title
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp(line, "eBay Bid History for") ||
		    !strcmp(line, "eBayBid History for"))
			break;
		if (!strcmp(line, "Unknown Item"))
			return auctionError(aip, ae_baditem, NULL);
	}
	if (!line || !(line=getnontag(fp)) || !(s1=strstr(line, " (Item #")))
		return auctionError(aip, ae_notitle, NULL);
	*s1 = '\0';
	printLog(stdout, "Auction %s: %s\n", aip->auction, line);


	/*
	 * current price
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("Currently", line))
			break;
	}
	if (!line || !(line = getnontag(fp)))
		return auctionError(aip, ae_noprice, NULL);
	printLog(stdout, "Currently: %s  (your maximum bid: %s)\n", line,
		 aip->bidPriceStr);
	aip->price = atof(line + strcspn(line, "0123456789"));
	if (aip->price < 0.01)
		return auctionError(aip, ae_convprice, line);


	/*
	 * Quantity
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("Quantity", line))
			break;
	}
	if (!line || !(line = getnontag(fp)) ||
	    (aip->quantity = atoi(line)) < 1)
		return auctionError(aip, ae_noquantity, NULL);


	/*
	 * Number of bids
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("# of bids", line))
			break;
	}
	if (!line || !(line = getnontag(fp)) || (aip->bids = atoi(line)) < 0)
		return auctionError(aip, ae_nonumbid, NULL);
	printLog(stdout, "Bids: %d\n", aip->bids);


	/*
	 * Time remaining
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("Time left", line))
			break;
	}
	if (!line || !(line = getnontag(fp)))
		return auctionError(aip, ae_notime, NULL);
	if ((aip->remain = getseconds(line)) < 0)
		return auctionError(aip, ae_badtime, line);
	printLog(stdout, "Time remaining: %s (%ld seconds)\n",
		line, aip->remain);


	/*
	 * High bidder
	 */
	if (aip->bids == 0) {
		puts("High bidder: --");
	} else if (aip->quantity == 1) {
		/* single auction with bids */
		const char *winner = NULL;

		while((line = getnontag(fp))) {
			if (!strcmp("Date of Bid", line))
				break;
		}
		if (!line || !(line = getnontag(fp)))
			return auctionError(aip, ae_nohighbid, NULL);
		if (strstr(line, "private auction")) {
			if (aip->bidResult == 0 && aip->price <= aip->bidPrice)
				winner = user;
			else
				winner = "[private]";
		} else
			winner = line;
		if (strcmp(winner, user)) {
			printLog(stdout, "High bidder: %s (NOT %s)\n", winner, user);
			if (!aip->remain)
				aip->won = 0;
		} else {
			printLog(stdout, "High bidder: %s!!!\n", winner);
			if (!aip->remain)
				aip->won = 1;
		}
	} else {
		/* dutch with bids */
		int bids = aip->bids;
		int numItems = aip->quantity;
		int match = 0;

		while((line = getnontag(fp))) {
			if (!strcmp("Date of Bid", line))
				break;
		}
		if (!line)
			return auctionError(aip, ae_nohighbid, NULL);
		while (bids && numItems > 0) {
			int bidQuant;

			if (!(line = getnontag(fp)))	/* user */
				return auctionError(aip, ae_nohighbid, NULL);
			match = !strcmp(user, line);
			if (!(line = getnontag(fp)) ||	/* reputation */
			    !(line = getnontag(fp)) ||	/* bid */
			    !(line = getnontag(fp)) ||	/* numItems */
			    !(bidQuant = atoi(line)))
				return auctionError(aip, ae_nohighbid, NULL);
			numItems -= bidQuant;
			--bids;
			if (match) {
				if (numItems >= 0) {
					if (!aip->remain)
						aip->won = bidQuant;
					printLog(stdout, "High bidder: %s!!!\n", user);
				} else {
					if (!aip->remain)
						aip->won = bidQuant + numItems;
					printLog(stdout, "High bidder: %s!!! (%d out of %d items)\n", user, bidQuant + numItems, bidQuant);
				}
				break;
			}
			if (!(line = getnontag(fp)))	/* date */
				return auctionError(aip, ae_nohighbid, NULL);
		}
		if (!match) {
			printLog(stdout, "High bidder: various dutch bidders (NOT %s)\n", user);
			if (!aip->remain)
				aip->won = 0;
		}
	}

	return 0;
} /* parseAuction() */

static const char QUERY_FMT[] =
	"GET http://%s/%s HTTP/1.0\r\n"
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\r\n"
	"Host: %s\r\n"
	"Accept: text/*\r\n"
	"Accept-Language: en\r\n"
	"Accept-Charset: iso-8859-1,*,utf-8\r\n"
	"Pragma: no-cache\r\n"
	"Cache-Control: no-cache\r\n"
	"Proxy-Connection: Keep-Alive\r\n"
	"\r\n";
static const char QUERY_CMD[] = "aw-cgi/eBayISAPI.dll?ViewBids&item=%s";
static const char UNAVAILABLE[] = "unavailable/";

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
	FILE *fp;
	char *line, *s1, *s2;
	int ret;

	log(("\n\n*** getInfo auction %s price %s user %s\n", aip->auction, aip->bidPriceStr, user));

	if (!aip->host)
		aip->host = myStrdup(HOSTNAME);
	if (!(fp = verboseConnect(&options.proxy, aip->host, 10, 5)))
		return auctionError(aip, ae_connect, NULL);

	if (!aip->query) {
		aip->query = (char *)myMalloc(sizeof(QUERY_CMD)+strlen(aip->auction));
		sprintf(aip->query, QUERY_CMD, aip->auction);
	}

	printLog(fp, QUERY_FMT, aip->host, aip->query, aip->host);
	fflush(fp);

	/*
	 * Redirect?
	 *
	 * Line will look like "HTTP/x.x 302 Object Moved" where x.x is HTTP
	 * version number.
	 */
	line = getuntilchar(fp, '\n');
	s1 = strtok(line, " \t");
	s2 = strtok(NULL, " \t");
	if (s1 && s2 && !strncmp("HTTP/", s1, (size_t)5) &&
	    (!strcmp("301", s2) || !strcmp("302", s2))) {
		/* redirect */
		char *newHost = NULL;
		char *newQuery = NULL;
		size_t newQueryLen = 0;

		log(("Redirect..."));
		if (match(fp, "Location: http://"))
			ret = auctionError(aip, ae_badredirect, NULL);
		else {
			newHost = getuntilchar(fp, '/');
			if (strcasecmp(aip->host, newHost)) {
				log(("redirect hostname is %s\n", newHost));
				newHost = myStrdup(newHost);
			} else
				newHost = NULL;
			newQuery = getuntilchar(fp, '\n');
			newQueryLen = strlen(newQuery);
			if (newQuery[newQueryLen - 1] == '\r')
				newQuery[--newQueryLen] = '\0';
			log(("redirected to %s", newQuery));
			if (!strncmp(newQuery, UNAVAILABLE, sizeof(UNAVAILABLE) - 1)) {
				log(("Unavailable!"));
				free(newHost);
				ret = auctionError(aip, ae_unavailable, NULL);
			} else {
				if (newHost) {
					free(aip->host);
					aip->host = newHost;
				}
				if (strcmp(aip->query, newQuery)) {
					free(aip->query);
					aip->query = myStrdup(newQuery);
				}

				runout(fp);
				fclose(fp);
				return getInfo(aip, quantity, user);
			}
		}
	} else
		ret = parseAuction(fp, aip, quantity, user);

	/* done! */
	runout(fp);
	fclose(fp);
	return ret;
}

static const char PRE_BID_FMT[] =
	"POST http://%s/aw-cgi/eBayISAPI.dll HTTP/1.0\r\n"
	"Referer: http://%s/%s\r\n"
	/*"Connection: Keep-Alive\r\n"*/
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\r\n"
	"Host: %s\r\n"
	"Accept: text/*\r\n"
	"Accept-Language: en\r\n"
	"Accept-Charset: iso-8859-1,*,utf-8\r\n"
	"Pragma: no-cache\r\n"
	"Cache-Control: no-cache\r\n"
	"Proxy-Connection: Keep-Alive\r\n"
	"Content-type: application/x-www-form-urlencoded\r\n"
	"Content-length: %d\r\n";
static const char PRE_BID_CMD[] =
	"MfcISAPICommand=MakeBid&item=%s&maxbid=%s\r\n\r\n";

/*
 * Get key for bid
 *
 * returns 0 on success, 1 on failure.
 */
int
preBid(auctionInfo *aip)
{
	FILE *fp;
	char *tmpkey;
	char *cp;
	size_t cmdlen = sizeof(PRE_BID_CMD) + strlen(aip->auction) + strlen(aip->bidPriceStr) -9;
	int ret = 0;

	log(("\n\n*** preBidAuction auction %s price %s\n", aip->auction, aip->bidPriceStr));

	if (!(fp = verboseConnect(&options.proxy, HOSTNAME, 6, 5)))
		return auctionError(aip, ae_connect, NULL);


	log(("\n\nquery string:\n"));
	printLog(fp, PRE_BID_FMT, HOSTNAME, aip->host, aip->query, HOSTNAME, cmdlen);
	printLog(fp, PRE_BID_CMD, aip->auction, aip->bidPriceStr);
	fflush(fp);

	log(("sent pre-bid\n"));

	if (match(fp, "<input type=\"hidden\" name=\"key\" value=\""))
		ret = auctionError(aip, ae_bidkey, NULL);
	else {
		tmpkey = getuntilchar(fp, '\"');
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

	runout(fp);
	fclose(fp);

	log(("socket closed\n"));

	return ret;
}

static const char BID_FMT[] =
	"POST http://%s/aw-cgi/eBayISAPI.dll HTTP/1.0\r\n"
	"Referer: http://%s/aw-cgi/eBayISAPI.dll\r\n"
	/*"Connection: Keep-Alive\r\n"*/
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\r\n"
	"Host: %s\r\n"
	"Accept: text/*\r\n"
	"Accept-Language: en\r\n"
	"Accept-Charset: iso-8859-1,*,utf-8\r\n"
	"Pragma: no-cache\r\n"
	"Cache-Control: no-cache\r\n"
	"Proxy-Connection: Keep-Alive\r\n"
	"Content-type: application/x-www-form-urlencoded\r\n"
	"Content-length: %d\r\n";
static const char BID_CMD[] =
	"MfcISAPICommand=AcceptBid&item=%s&key=%s&maxbid=%s&quant=%s&userid=%s&pass=%s\r\n\r\n";

static const char CONGRATS[] = "Congratulations...";

/*
 * Place bid.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
static int
bidSocket(FILE *fp, auctionInfo *aip, int quantity, const char *user, const char *password)
{
	size_t cmdlen;
	int i;
	char *line;
	char quantityStr[12];	/* must hold an int */

	if (aip->quantity < quantity)
		quantity = aip->quantity;
	sprintf(quantityStr, "%d", quantity);

	decryptPassword();
	log(("\n\nquery string:\n"));
	cmdlen = sizeof(BID_CMD) + strlen(aip->auction) + strlen(aip->key) +
		strlen(aip->bidPriceStr) + strlen(quantityStr) + strlen(user) +
		strlen(password) - 17;
	printLog(fp, BID_FMT, HOSTNAME, aip->host, HOSTNAME, cmdlen);

	/* don't log password */
	fprintf(fp, BID_CMD, aip->auction, aip->key, aip->bidPriceStr, quantityStr, user, password);
	log((BID_CMD, aip->auction, aip->key, aip->bidPriceStr, quantityStr, user, "(password hidden)"));
	encryptPassword();

	fflush(fp);
	aip->bidResult = -1;

	for (line = getnontag(fp), i = 0; aip->bidResult < 0 && line && i < 10;
	     ++i, line = getnontag(fp)) {
		if (!strncmp(line, CONGRATS, sizeof(CONGRATS) - 1)) /* high bidder */
			return logNonTag(line, fp, aip->bidResult = 0);
		else if (!strcmp(line, "User ID"))	/* bad user/pass */
			return logNonTag(line, fp, aip->bidResult = auctionError(aip, ae_badpass, NULL));
		else if (!strcmp(line, "We're sorry..."))	/* outbid */
			return logNonTag(line, fp, aip->bidResult = auctionError(aip, ae_outbid, NULL));
		else if (!strcmp(line, "You are the current high bidder..."))
							/* reserve not met */
			return logNonTag(line, fp, aip->bidResult = auctionError(aip, ae_reservenotmet, NULL));
		else if (!strcmp(line, "Cannot proceed")) /* auction closed */
			return logNonTag(line, fp, aip->bidResult = auctionError(aip, ae_ended, NULL));
	}
	if (aip->bidResult == -1) {
		printLog(stdout, "Cannot determine result of bid\n");
		return 0;	/* prevent another bid */
	}

	return aip->bidResult;
} /* bidSocket() */

/*
 * Place bid.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
int
bid(option_t options, auctionInfo *aip)
{
	FILE *fp;
	int ret;

	log(("\n\n*** bidAuction auction %s price %s quantity %d user %s\n", aip->auction, aip->bidPriceStr, options.quantity, options.user));

	if (!options.bid) {
		printLog(stdout, "Bidding disabled\n");
		return aip->bidResult = 0;
	}
	if (!(fp = verboseConnect(&options.proxy, HOSTNAME, 6, 5)))
		return aip->bidResult = auctionError(aip, ae_connect, NULL);
	ret = bidSocket(fp, aip, options.quantity, options.user, options.password);
	runout(fp);
	fclose(fp);
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
watch(auctionInfo *aip, option_t options)
{
	int errorCount = 0;
	long remain = LONG_MIN;
	unsigned int sleepTime = 0;

	log(("*** WATCHING auction %s price-each %s quantity %d user %s bidtime %ld\n", aip->auction, aip->bidPriceStr, options.quantity, options.user, options.bidtime));

	for (;;) {
		time_t start = time(NULL);
		int ret = getInfo(aip, options.quantity, options.user);
		time_t latency = time(NULL) - start;

		if (ret) {
			printAuctionError(aip, stderr);

			/*
			 * Fatal error?  We allow up to 50 errors, then quit.
			 * eBay "unavailable" doesn't count towards the total.
			 */
			if (aip->auctionError == ae_unavailable) {
				if (remain >= 0)
					remain -= sleepTime + (latency * 2);
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
						      options.user);
				if (!ret)
					remain = aip->remain - options.bidtime - (latency * 2);
				else
					return 1;
			} else {
				/* non-fatal error */
				log(("ERROR %d!!!\n", ++errorCount));
				if (errorCount > 50)
					return auctionError(aip, ae_toomany, NULL);
				printLog(stdout, "Cannot find auction - internet or eBay problem?\nWill try again after sleep.\n");
				remain -= sleepTime + (latency * 2);
			}
		} else if (!isValidBidPrice(aip))
			return auctionError(aip, ae_bidprice, NULL);
		else
			remain = aip->remain - options.bidtime - (latency * 2);

		/* it's time!!! */
		if (remain < 0)
			break;

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
			keyLatency = time(NULL) - start - latency;
			remain -= keyLatency;
		}

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

		if (sleepTime == remain)
			break;
	}

	return 0;
} /* watch() */

/* secret option - test parser */
void
testParser(int argc, char *argv[])
{
	for (; argc; --argc, ++argv) {
		FILE *fp = fopen(*argv, "r");
		char *line;
		FILE *fpw;
		char *wname;

		if (!fp) {
			fprintf(stderr, "Cannot open %s: %s\n", *argv,
				strerror(errno));
			continue;
		}
		wname = basename(*argv);
		wname = (char *)myMalloc(strlen(wname) + 4 + 1);
		sprintf(wname, "%s.out", basename(*argv));

		if (!(fpw = fopen(wname, "w"))) {
			fprintf(stderr, "Cannot open %s: %s\n", wname,
				strerror(errno));
			fclose(fp);
			continue;
		}
		while ((line = getnontag(fp)))
			fprintf(fpw, "\"%s\"\n", line);
		fclose(fp);
		fclose(fpw);
	}
	exit(0);
}
