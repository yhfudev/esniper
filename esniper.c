/*
 * Copyright (c) 2002, Scott Nicol <esniper@sourceforge.net>
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

static const char version[]="esniper version 1.5";
static const char blurb[]="Please visit http://esniper.sourceforge.net/ for updates and bug reports";

#if defined(unix) || defined (__unix) || defined (__MACH__)
#	include <unistd.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <sys/time.h>
#elif defined(WIN32) /* TODO */
#	include <winsock.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

static void *myMalloc(size_t);
static char *myStrdup(const char *);
static void printLog(FILE *, const char *, ...);

/* various buffer sizes.  Assume these sizes are big enough (I hope!) */
#define QUERY_LEN	1024
#define TIME_BUF_SIZE	1024

/* bid time, in seconds before end of auction */
static const int MIN_BIDTIME = 5;
static const int DEFAULT_BIDTIME = 10;

static const char HOSTNAME[] = "cgi.ebay.com";

static FILE *logfile = NULL;
static int debug = 0;

/*
 * All information associated with an item
 */
typedef struct {
	long remain;	/* seconds remaining */
	char *host;	/* bidding history host */
	char *query;	/* bidding history query */
	char *key;	/* bidding key */
	int quantity;	/* number of items available */
	int bids;	/* number of bids made */
	double price;	/* current price */
	double bidResult;/* result code from bid (-1 = no bid yet) */
} itemInfo;

static itemInfo *
newItemInfo(char *host, char *query, char *key)
{
	itemInfo *iip = (itemInfo *)myMalloc(sizeof(itemInfo));

	iip->remain = 0;
	iip->host = host;
	iip->query = query;
	iip->key = key;
	iip->quantity = 0;
	iip->bids = 0;
	iip->price = 0;
	iip->bidResult = -1;
	return iip;
}

/*
 * Replacement malloc/realloc/strdup, with error checking
 */

static void *
myMalloc(size_t size)
{
	void *ret = malloc(size);

	if (!ret) {
		printLog(stderr, "Cannot allocate memory: %s\n", strerror(errno));
		exit(1);
	}
	return ret;
}

static void *
myRealloc(void *buf, size_t size)
{
	void *ret = buf ? realloc(buf, size) : malloc(size);

	if (!ret) {
		printLog(stderr, "Cannot reallocate memory: %s\n", strerror(errno));
		exit(1);
	}
	return ret;
}

static char *
myStrdup(const char *s)
{
	char *ret;

	if (!s)
		return NULL;
	ret = myMalloc(strlen(s) + 1);
	strcpy(ret, s);
	return ret;
}

/*
 * Debugging functions and macros.
 */
static void
logOpen(const char *item)
{
	char logfilename[128];

	sprintf(logfilename, "esniper.%s.log", item);
	if (!(logfile = fopen(logfilename, "a"))) {
		fprintf(stderr, "Unable to open log file %s: %s\n",
			logfilename, strerror(errno));
		exit(1);
	}
}

/*
 * va_list version of log
 */
static void
vlog(const char *fmt, va_list arglist)
{
	struct timeval tv;
	time_t t;
	char timebuf[TIME_BUF_SIZE];

	if (!logfile) {
		fprintf(stderr, "Log file not open\n");
		exit(1);
	}

	gettimeofday(&tv, NULL);
	t = (time_t)(tv.tv_sec);
	strftime(timebuf, TIME_BUF_SIZE, "\n\n*** %Y-%m-%d %H:%M:%S", localtime(&t));
	fprintf(logfile, "%s.%06ld ", timebuf, tv.tv_usec);
	vfprintf(logfile, fmt, arglist);
	fflush(logfile);
}

/*
 * Debugging log function.  Use like printf.  Or, better yet, use the log()
 * macro (but be sure to enclose the argument list in two parens)
 */
#define log(x) if(!debug);else dlog x
static void
dlog(const char *fmt, ...)
{
	va_list arglist;


	va_start(arglist, fmt);
	vlog(fmt, arglist);
	va_end(arglist);
}

/*
 * send message to log file and stderr
 */
static void
printLog(FILE *fp, const char *fmt, ...)
{
	va_list arglist;

	if (debug) {
		va_start(arglist, fmt);
		vlog(fmt, arglist);
		va_end(arglist);
	}
	va_start(arglist, fmt);
	vfprintf(fp, fmt, arglist);
	va_end(arglist);
	fflush(fp);
}

/*
 * log a single character
 */
static void
logChar(int c)
{
	if (!logfile) {
		fprintf(stderr, "Log file not open!\n");
		exit(1);
	}

	if (c == EOF)
		fflush(logfile);
	else
		putc(c, logfile);
}

/*
 * Cygwin doesn't provide basename?
 */
static char *
basename(char *s)
{
	char *slash;
#if defined(__CYGWIN__) || defined(WIN32)
	char *backslash;

	if (!s) return s;
	slash = strrchr(s, '/');
	backslash = strrchr(s, '\\');
	return slash > backslash ? slash + 1 : (backslash ? backslash + 1 : s);
#else
	if (!s) return s;
	slash = strrchr(s, '/');
	return slash ? slash + 1 : s;
#endif
}

/*
 * Open a connection to the host.  Return valid FILE * if successful, NULL
 * otherwise
 */
static FILE *
verboseConnect(const char *host, int retryTime, int retryCount)
{
	int saveErrno, sockfd, rc, count;
	struct sockaddr_in servAddr;
	struct hostent *entry;
	static struct sigaction alarmAction;
	static int firstTime = 1;

	if (firstTime)
		sigaction(SIGALRM, NULL, &alarmAction);
	for (count = 0; count < 10; count++) {
		if (!(entry = gethostbyname(host))) {
			log(("gethostbyname errno %d\n", h_errno));
			sleep(1);
		} else
			break;
	}
	if (!entry) {
		printLog(stderr, "Cannot convert \"%s\" to IP address\n", host);
		return NULL;
	}
	if (entry->h_addrtype != AF_INET) {
		printLog(stderr, "%s is not an internet host?\n", host);
		return NULL;
	}

	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	memcpy(&servAddr.sin_addr.s_addr, entry->h_addr, 4);
	servAddr.sin_port = htons(80);

	log(("connect"));
	while (retryCount-- > 0) {
		if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			printLog(stderr, "Socket error %d: %s\n", errno,
				strerror(errno));
			return NULL;
		}
		alarmAction.sa_flags &= ~SA_RESTART;
		sigaction(SIGALRM, &alarmAction, NULL);
		alarm(retryTime);
		rc = connect(sockfd, (struct sockaddr *)&servAddr, sizeof(struct sockaddr_in));
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
		if (debug)
			logChar(c);
		if ((char)c == *cursor) {
			if (*++cursor == '\0') {
				if (debug)
					logChar(EOF);
				return 0;
			}
		} else if (c != '\n' && c != '\r')
			cursor = str;
	}
	if (debug)
		logChar(EOF);
	return -1;
}


/*
 * Resize a buffer.  Used by addchar and term macros.
 */
static char *
resize(char *buf, size_t *size)
{
	*size += 1024;
	return (char *)myRealloc(buf, *size);
}

#define addchar(buf, bufsize, count, c) \
	do {\
		if (count >= bufsize)\
			buf = resize(buf, &bufsize);\
		buf[count++] = c;\
	} while (0)

#define term(buf, bufsize, count) \
	do {\
		if (count >= bufsize)\
			buf = resize(buf, &bufsize);\
		buf[count] = '\0';\
	} while (0)

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
getuntilchar(FILE *fp, char until)
{
	static char buf[1024];
	int count;
	int c;

	log(("\n\ngetuntilchar('%c')\n\n", until));

	count = 0;
	while ((c = getc(fp)) != EOF) {
		if (debug)
			logChar(c);
		if (count >= 1024) {
			if (debug)
				logChar(EOF);
			return NULL;
		}
		if ((char)c == until) {
			buf[count] = '\0';
			if (debug)
				logChar(EOF);
			return buf;
		}
		buf[count++] = (char)c;
	}
	if (debug)
		logChar(EOF);
	return NULL;
}

static void
runout(FILE *fp)
{
	int c, count;

	if (debug) {
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
		else {
			printLog(stdout, "Error: unknown time interval \"%s\"\n", timestr);
			return -1;
		}
		while (*timestr && !isdigit((int)*timestr))
			++timestr;
	}

	return accum;
}

/*
 * parseItem(): parses bid history page
 *
 * returns:
 *	0 OK
 *	1 non-fatal error (badly formatted page, etc)
 *	2 fatal error (price too high, quantity requested not available, etc)
 */
static int
parseItem(FILE *fp, char *item, char *quantity, char *amount, char *user, itemInfo *iip)
{
	char *line, *s1;

	/*
	 * Auction title
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp(line, "eBay Bid History for"))
			break;
	}
	if (!line || !(line=getnontag(fp)) || !(s1=strstr(line, " (Item #"))) {
		printLog(stderr, "Item title not found\n");
		return 1;
	}
	*s1 = '\0';
	printLog(stdout, "Item %s: %s\n", item, line);


	/*
	 * current price
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("Currently", line))
			break;
	}
	if (!line || !(line = getnontag(fp))) {
		printLog(stderr, "Current price not found\n");
		return 1;
	}
	printLog(stdout, "Currently: %s\n", line);
	iip->price = atof(line + strcspn(line, "0123456789"));
	if (iip->price < 0.01) {
		printLog(stderr, "Cannot convert amount %s\n", line);
		return 1;
	} else if (iip->price > atof(amount)) {
		printLog(stderr, "Current price above your limit.\n");
		return 2;
	}


	/*
	 * Quantity
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("Quantity", line))
			break;
	}
	if (!line || !(line = getnontag(fp)) ||
	    (iip->quantity = atoi(line)) < 1) {
		printLog(stderr, "Quantity not found\n");
		return 1;
	} else if (iip->quantity < atoi(quantity)) {
		printLog(stderr, "Quantity requested not available.\n");
		return 2;
	}


	/*
	 * Number of bids
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("# of bids", line))
			break;
	}
	if (!line || !(line = getnontag(fp)) || (iip->bids = atoi(line)) < 0) {
		printLog(stderr, "Number of bids not found\n");
		return 1;
	}
	printLog(stdout, "Bids: %d\n", iip->bids);


	/*
	 * Time remaining
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("Time left", line))
			break;
	}
	if (!line || !(line = getnontag(fp))) {
		printLog(stderr, "Time remaining not found\n");
		return 1;
	}
	if ((iip->remain = getseconds(line)) < 0)
		return 1;
	printLog(stdout, "Time remaining: %s (%ld seconds)\n",
		line, iip->remain);


	/*
	 * High bidder
	 */
	if (iip->bids == 0) {
		puts("High bidder: --");
	} else if (iip->quantity == 1) {
		/* single item with bids */
		while((line = getnontag(fp))) {
			if (!strcmp("Date of Bid", line))
				break;
		}
		if (!line || !(line = getnontag(fp))) {
			printLog(stderr, "High bidder not found\n");
			return 1;
		}
		if (strstr(line, "private auction")) {
			if (iip->bidResult == 0 && iip->price <= atof(amount))
				line = user;
			else
				line = "[private]";
		}
		if (strcmp(line, user))
			printLog(stdout, "High bidder: %s (NOT %s)\n", line, user);
		else
			printLog(stdout, "High bidder: %s!!!\n", line);
	} else {
		/* dutch with bids */
		int bids = iip->bids;
		int quantity = iip->quantity;
		int match = 0;

		while((line = getnontag(fp))) {
			if (!strcmp("Date of Bid", line))
				break;
		}
		if (!line) {
			printLog(stderr, "High bidder not found\n");
			return 1;
		}
		while (bids && quantity > 0) {
			int bidQuant;

			if (!(line = getnontag(fp))) {	/* user */
				printLog(stderr, "High bidder not found\n");
				return 1;
			}
			match = !strcmp(user, line);
			if (!(line = getnontag(fp)) ||	/* reputation */
			    !(line = getnontag(fp)) ||	/* bid */
			    !(line = getnontag(fp)) ||	/* quantity */
			    !(bidQuant = atoi(line))) {
				printLog(stderr, "High bidder not found\n");
				return 1;
			}
			quantity -= bidQuant;
			--bids;
			if (match) {
				if (quantity >= 0)
					printLog(stdout, "High bidder: %s!!!\n", user);
				else
					printLog(stdout, "High bidder: %s!!! (%d out of %d items)\n", user, bidQuant + quantity, bidQuant);
				break;
			}
			if (!(line = getnontag(fp))) {	/* date */
				printLog(stderr, "High bidder not found\n");
				return 1;
			}
		}
		if (!match)
			printLog(stdout, "High bidder: various dutch bidders (NOT %s)\n", user);
	}


	return 0;
} /* parseItem() */

static const char QUERY_FMT[] =
	"GET /%s HTTP/1.0\r\n"
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\r\n"
	"Host: %s:80\r\n"
	"Accept: text/*\r\n"
	"Accept-Language: en\r\n"
	"Accept-Charset: iso-8859-1,*,utf-8\r\n"
	"\r\n";
static const char QUERY_CMD[] = "aw-cgi/eBayISAPI.dll?ViewBids&item=%s";

/*
 * getItemInfo(): Get info on item from bid history page.
 *
 * returns:
 *	0 OK
 *	1 non-fatal error (badly formatted page, etc)
 *	2 fatal error (price too high, quantity requested not available, etc)
 */
static int
getItemInfo(char *item, char *quantity, char *amount, char *user, itemInfo *iip)
{
	FILE *fp;
	char *line, *s1, *s2;
	int ret;

	log(("\n\n*** getItemInfo item %s amount %s user %s\n", item, amount, user));

	if (!iip->host)
		iip->host = myStrdup(HOSTNAME);
	if (!(fp = verboseConnect(iip->host, 10, 5))) {
		printLog(stderr, "Connect Failed.\n");
		return 1;
	}

	if (!iip->query) {
		iip->query = (char *)myMalloc(sizeof(QUERY_CMD)+strlen(item));
		sprintf(iip->query, QUERY_CMD, item);
	}

	printLog(fp, QUERY_FMT, iip->query, iip->host);
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
	if (s1 && s2 && !strncmp("HTTP/", s1, 5) &&
	    (!strcmp("301", s2) || !strcmp("302", s2))) {
		char *newHost;
		char *newQuery;
		size_t newQueryLen;

		log(("Redirect..."));
		if (match(fp, "Location: http://")) {
			printLog(stderr, "new item location not found\n");
			return 1;
		}
		if (strcasecmp(iip->host, (newHost = getuntilchar(fp, '/')))) {
			log(("redirect hostname is %s\n", newHost));
			free(iip->host);
			iip->host= myStrdup(newHost);
		}
		newQuery = getuntilchar(fp, '\n');
		newQueryLen = strlen(newQuery);
		if (newQuery[newQueryLen - 1] == '\r')
			newQuery[--newQueryLen] = '\0';
		if (strcmp(iip->query, newQuery)) {
			free(iip->query);
			iip->query = myStrdup(newQuery);
		}

		runout(fp);
		fclose(fp);

		return getItemInfo(item, quantity, amount, user, iip);
	}

	ret = parseItem(fp, item, quantity, amount, user, iip);

	/* done! */
	runout(fp);
	fclose(fp);

	return ret;
}

static const char PRE_BID_FMT[] =
	"POST /aw-cgi/eBayISAPI.dll HTTP/1.0\r\n"
	"Referer: http://%s/%s\r\n"
	/*"Connection: Keep-Alive\r\n"*/
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\r\n"
	"Host: %s\r\n"
	"Accept: text/*\r\n"
	"Accept-Language: en\r\n"
	"Accept-Charset: iso-8859-1,*,utf-8\r\n"
	"Content-type: application/x-www-form-urlencoded\r\n"
	"Content-length: %d\r\n";
static const char PRE_BID_CMD[] =
	"MfcISAPICommand=MakeBid&item=%s&maxbid=%s\r\n\r\n";

/*
 * Get key for bid
 *
 * returns 0 on success, 1 on failure.
 */
static int
preBidItem(char *item, char *amount, itemInfo *iip)
{
	FILE *fp;
	char *tmpkey;
	char *cp;
	size_t cmdlen = sizeof(PRE_BID_CMD) + strlen(item) + strlen(amount) -9;
	int ret = 0;

	log(("\n\n*** preBidItem item %s amount %s\n", item, amount));

	if (!(fp = verboseConnect(HOSTNAME, 6, 5))) {
		printLog(stderr, "Connect Failed.\n");
		return 1;
	}


	log(("\n\nquery string:\n"));
	printLog(fp, PRE_BID_FMT, iip->host, iip->query, HOSTNAME, cmdlen);
	printLog(fp, PRE_BID_CMD, item, amount);
	fflush(fp);

	log(("sent pre-bid\n"));

	if (match(fp, "<input type=\"hidden\" name=\"key\" value=\""))
		ret = 1;
	else {
		tmpkey = getuntilchar(fp, '\"');
		log(("  reported key is: %s\n", tmpkey));

		/* translate key for URL */
		iip->key = (char *)myMalloc(strlen(tmpkey)*3 + 1);
		for (cp = iip->key; *tmpkey; ++tmpkey) {
			if (*tmpkey == '$') {
				*cp++ = '%';
				*cp++ = '2';
				*cp++ = '4';
			} else
				*cp++ = *tmpkey;
		}
		*cp = '\0';

		log(("\n\ntranslated key is: %s\n\n", iip->key));
	}

	runout(fp);
	fclose(fp);

	log(("socket closed\n"));

	return ret;
}

static const char BID_FMT[] =
	"POST /aw-cgi/eBayISAPI.dll HTTP/1.0\r\n"
	"Referer: http://%s/aw-cgi/eBayISAPI.dll\r\n"
	/*"Connection: Keep-Alive\r\n"*/
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\r\n"
	"Host: %s\r\n"
	"Accept: text/*\r\n"
	"Accept-Language: en\r\n"
	"Accept-Charset: iso-8859-1,*,utf-8\r\n"
	"Content-type: application/x-www-form-urlencoded\r\n"
	"Content-length: %d\r\n";
static const char BID_CMD[] =
	"MfcISAPICommand=AcceptBid&item=%s&key=%s&maxbid=%s&quant=%s&userid=%s&pass=%s\r\n\r\n";

/*
 * Place bid.
 *
 * Returns:
 * 0: no error
 * 1: known error
 * 2: unknown error (retry?)
 */
int
bidItem(int bid, const char *item, const char *amount, const char *quantity, const char *user, const char *password, itemInfo *iip)
{
	FILE *fp;
	size_t cmdlen;
	int i;
	char *line;

	log(("\n\n*** bidItem item %s amount %s quantity %s user %s\n", item, amount, quantity, user));

	if (!bid) {
		printLog(stdout, "Bidding disabled\n");
		return iip->bidResult = 0;
	}
	if (!(fp = verboseConnect(HOSTNAME, 6, 5))) {
		printLog(stderr, "Connect Failed.\n");
		return iip->bidResult = 2;
	}

	log(("\n\nquery string:\n"));
	cmdlen = sizeof(BID_CMD) + strlen(item) + strlen(iip->key) +
		strlen(amount) + strlen(quantity) + strlen(user) +
		strlen(password) - 17;
	printLog(fp, BID_FMT, iip->host, HOSTNAME, cmdlen);
	printLog(fp, BID_CMD, item, iip->key, amount, quantity, user,password);
	fflush(fp);
	iip->bidResult = -1;

	for (line = getnontag(fp), i = 0; iip->bidResult < 0 && line && i < 10;
	     ++i, line = getnontag(fp)) {
		if (!strcmp(line, "Congratulations...")) { /* high bidder */
			printLog(stdout, "%s ", line);
			printLog(stdout, "%s\n", getnontag(fp));
			iip->bidResult = 0;
		} else if (!strcmp(line, "User ID")) { /* bad user/pass */
			printLog(stdout, "%s ", line);
			printLog(stdout, "%s\n", getnontag(fp));
			iip->bidResult = 1;
		} else if (!strcmp(line, "We're sorry...")) { /* outbid */
			printLog(stdout, "%s ", line);
			printLog(stdout, "%s\n", getnontag(fp));
			iip->bidResult = 1;
		} else if (!strcmp(line, "You are the current high bidder...")) {
							/* reserve not met */
			printLog(stdout, "%s ", line);
			printLog(stdout, "%s\n", getnontag(fp));
			iip->bidResult = 1;
		} else if (!strcmp(line, "Cannot proceed")) {
							/* auction closed */
			printLog(stdout, "%s\n", getnontag(fp));
			iip->bidResult = 1;
		}
	}
	if (iip->bidResult == -1) {
		printLog(stdout, "Cannot determine result of bid\n");
		return 0;	// prevent another bid
	}

	runout(fp);
	fclose(fp);

	return iip->bidResult;
}

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

static void
sigAlarm(int sig)
{
	signal(SIGALRM, sigAlarm);
	log((" SIGALRM"));
}

static void
sigTerm(int sig)
{
	signal(SIGTERM, SIG_DFL);
	log(("SIGTERM...\n"));
	raise(sig);
}

/*
 * Current date/time
 */
static char *
timestamp()
{
	static char buf[80];	/* much larger than needed */
	time_t t = time(0);
	struct tm *tmp = localtime(&t);

	strftime(buf, 80, "%a %b %e %T", tmp);
	return buf;
}

/*
 * watchItem(): watch item until it is time to bid
 *
 * returns:
 *	0 OK
 *	1 Error
 */
static int
watchItem(char *item, char *quantity, char *amount, char *user, long bidtime, itemInfo *iip)
{
	int errorCount = 0;
	long remain = -1, sleepTime = -1;

	log(("*** WATCHING item %s amount-each %s quantity %s user %s bidtime %ld\n", item, amount, quantity, user, bidtime));

	for (;;) {
		time_t start = time(NULL);
		int ret = getItemInfo(item, quantity, amount, user, iip);
		time_t latency = time(NULL) - start;

		if (ret > 1 || (ret == 1 && remain == -1)) /* Fatal error! */
			return 1;

		if (ret == 1) {	/* non-fatal error */
			log((" ERROR %d!!!\n", ++errorCount));
			if (errorCount > 50) {
				printLog(stderr, "Cannot get item info\n");
				return 1;
			}
			printLog(stdout, "Cannot find item - internet or eBay problem?\nWill try again after sleep.\n");
			remain -= sleepTime + (latency * 2);
		} else
			remain = iip->remain - bidtime - (latency * 2);

		/* it's time!!! */
		if (remain < 0)
			break;

		/*
		 * if we're less than two minutes away,
		 * get key for bid
		 */
		if (remain <= 150 && !iip->key) {
			int i;
			time_t keyLatency;

			printf("\n");
			for (i = 0; i < 5; ++i) {
				if (!preBidItem(item, amount, iip))
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
} /* watchItem() */

int
main(int argc, char *argv[])
{
	long bidtime = 0;
	char *item, *amount, *quantity, *user, *password;
	int retryCount, now = 0, usage = 0;
	int bid = 1;	/* really make a bid? */
	int parse = 0;	/* secret option - for testing page parsing */
	int ret = 0;
	const char *progname = basename(argv[0]);
	itemInfo *iip = newItemInfo(NULL, NULL, NULL);
	int c;

	while ((c = getopt(argc, argv, "dnpv")) != EOF) {
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'n':
			bid = 0;
			break;
		case 'p':	/* secret option */
			parse = 1;
			break;
		case 'v':
			fprintf(stderr, "%s\n%s\n", version, blurb);
			exit(0);
		case '?':
			usage = 1;
		}
	}
	argc -= optind;
	argv += optind;

	if (parse) {
		testParser(argc, argv);
		exit(0);
	}

	if (usage || argc < 5 || argc > 6) {
		fprintf(stderr,
			"usage: %s [-dn] item price quantity username password [secs|now]\n"
			"\n"
			"where:\n"
			"-d: write debug output to file\n"
			"-n: do not place bid\n"
			"-v: print version and exit\n"
			"\n"
			"Bid is placed %d seconds before the end of auction unless some other time or\n"
			"\"now\" is specified.\n"
			"\n"
			"%s\n",
			progname, DEFAULT_BIDTIME, blurb);
		exit(1);
	}

	/* init variables */
	item = argv[0];
	amount = argv[1];
	quantity = argv[2];
	user = argv[3];
	password = argv[4];
	if (argc == 6) {
		now = !strcmp("now", argv[5]);
		if (!now) {
			if ((bidtime = atol(argv[5])) < MIN_BIDTIME) {
				printf("NOTE: minimum bid time %d seconds\n",
					MIN_BIDTIME);
				bidtime = MIN_BIDTIME;
			}
		}
	} else
		bidtime = DEFAULT_BIDTIME;

	if (debug)
		logOpen(item);

	log(("item %s amount %s quantity %s user %s bidtime %ld\n",
		item, amount, quantity, user, bidtime));

	signal(SIGALRM, sigAlarm);
	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, sigTerm);

	if (now) {
		if (getItemInfo(item, quantity, amount, user, iip))
			exit(1);
		if (iip->remain && preBidItem(item, amount, iip))
			exit(1);
	} else {
		if (watchItem(item, quantity, amount, user, bidtime, iip))
			exit(1);
	}

	/* ran out of time! */
	if (!iip->remain) {
		if (bid) {
			printLog(stderr, "Sorry, auction is over\n");
			exit(1);
		}
		exit(0);
	}

	if (bid)
		printLog(stdout, "\nBidding...\n");

	if (!iip->key && bid) {
		printLog(stderr, "Problem with bid.  No bid placed.\n");
		exit(1);
	}

	log(("*** BIDDING!!! item %s amount %s quantity %s user %s\n",
		item, amount, quantity, user));

	for (retryCount = 0; retryCount < 3; retryCount++) {
		ret = bidItem(bid, item, amount, quantity, user, password,iip);

		if (ret == 0 || ret == 1)
			break;
		printLog(stderr, "retrying...\n");
	}

	if (!(ret == 0 || ret == 1))
		exit(ret);

	/* view item after bid */
	if (bidtime > 0 && bidtime < 60) {
		printLog(stdout, "Waiting %d seconds for auction to complete...\n", bidtime);
		sleep(bidtime + 1);	/* make sure it really is over */
	}
	printLog(stdout, "\nPost-bid info:\n");
	getItemInfo(item, quantity, amount, user, iip);

	exit(ret);
}
