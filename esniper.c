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

static const char version[]="esniper version 1.0";
static const char blurb[]="Please visit http://esniper.sourceforge.net/ for updates and bug reports";

#if defined(unix) || defined (__unix)
#	include <unistd.h>
#	include <sys/socket.h>
#	ifdef __CYGWIN__
#		include <netinet/in.h>
#	elif defined(__linux)
#		include <netinet/in.h>
#		include <sys/time.h>
#	elif defined(__hpux)
		/* nothing special yet */
#	elif defined(sun)
		/* nothing special yet */
#	endif
#elif defined(WIN32)
	/* TODO */
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

/*
 * Returned from getItemInfo()
 */
typedef struct {
	size_t remain;
	char *host;
	char *query;
	int quantity;
	int bids;
	double price;
} itemInfo;

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
 * Debugging functions and macros.
 */
static void
logOpen(const char *item)
{
	char logfilename[128];

	sprintf(logfilename, "esniper.log.%s", item);
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
	struct timeval	tv;
	char timebuf[TIME_BUF_SIZE];

	if (!logfile) {
		fprintf(stderr, "Log file not open\n");
		exit(1);
	}

	gettimeofday(&tv, NULL);
	strftime(timebuf, TIME_BUF_SIZE, "\n\n*** %Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
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
}

static void
logChar(char ch)
{
	if (!logfile) {
		fprintf(stderr, "Log file not open!\n");
		exit(1);
	}

	putc(ch, logfile);
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
			if (*++cursor == '\0')
				return 0;
		} else if (c != '\n' && c != '\r')
			cursor = str;
	}
	return -1;
}


/*
 * Resize a buffer.  Used by addchar and term macros.
 */
static char *
resize(char *buf, size_t *size)
{
	*size += 1024;
	return (char *)((buf == NULL) ? malloc(*size) : realloc(buf, *size));
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
	size_t count = 0;
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
		if (count >= 1024)
			return NULL;
		if ((char)c == until) {
			buf[count] = '\0';
			return buf;
		}
		buf[count++] = (char)c;
	}
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
		dlog("%d bytes", count);
	} else {
		for (c = getc(fp); c != EOF; c = getc(fp))
			;
	}
}

static long
getseconds(char *timestr)
{
	static char *second = "sec";
	static char *minute = "min";
	static char *hour = "hour";
	static char *day = "day";
	long accum = 0;
	long num;

	while (*timestr) {
		num = strtol(timestr, &timestr, 10);
		while (isspace((int)*timestr))
			++timestr;
		if (!strncmp(timestr, second, strlen(second)))
			return(accum + num);
		else if (!strncmp(timestr, minute, strlen(minute)))
			accum += num * 60;
		else if (!strncmp(timestr, hour, strlen(hour)))
			accum += num * 3600;
		else if (!strncmp(timestr, day, strlen(day)))
			accum += num * 86400;
		else {
			printf("Error: unknown time interval \"%s\"\n",timestr);
			return accum;
		}
		while (*timestr && !isdigit((int)*timestr))
			++timestr;
	}

	return accum;
}

static itemInfo *
parseItem(FILE *fp, char *item, char *amount, char *user, char *host, char *query)
{
	itemInfo *ret = (itemInfo *)malloc(sizeof(itemInfo));
	char *line, *s1;

	ret->host = host;
	ret->query = query;

	/*
	 * Auction title
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp(line, "eBay Bid History for"))
			break;
	}
	if (!line || !(line=getnontag(fp)) || !(s1=strstr(line, " (Item #"))) {
		printLog(stderr, "Item title not found\n");
		return NULL;
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
		return NULL;
	}
	printLog(stdout, "Currently: %s\n", line);
	ret->price = atof(line + strcspn(line, "0123456789"));
	if (ret->price < 0.01) {
		printLog(stderr, "Cannot convert amount %s\n", line);
		return NULL;
	} else if (ret->price > atof(amount)) {
		printLog(stderr, "Current price above your limit.\n");
		return NULL;
	}


	/*
	 * Quantity
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("Quantity", line))
			break;
	}
	if (!line || !(line = getnontag(fp)) ||
	    (ret->quantity = atoi(line)) < 1) {
		printLog(stderr, "Quantity not found\n");
		return NULL;
	}


	/*
	 * Number of bids
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("# of bids", line))
			break;
	}
	if (!line || !(line = getnontag(fp)) || (ret->bids = atoi(line)) < 0) {
		printLog(stderr, "Number of bids not found\n");
		return NULL;
	}
	printLog(stdout, "Bids: %d\n", ret->bids);


	/*
	 * Time remaining
	 */
	while ((line = getnontag(fp))) {
		if (!strcmp("Time left", line))
			break;
	}
	if (!line || !(line = getnontag(fp))) {
		printLog(stderr, "Time remaining not found\n");
		return NULL;
	}
	ret->remain = getseconds(line);
	printLog(stdout, "Time remaining: %s (%ld seconds)\n",
		line, ret->remain);


	/*
	 * High bidder
	 */
	if (ret->bids == 0) {
		puts("High bidder: --");
	} else if (ret->quantity == 1) {
		/* single item with bids */
		while((line = getnontag(fp))) {
			if (!strcmp("Date of Bid", line))
				break;
		}
		if (!line || !(line = getnontag(fp))) {
			printLog(stderr, "High bidder not found\n");
			return NULL;
		}
		if (strcmp(line, user))
			printLog(stdout, "High bidder: %s (NOT %s)\n", line, user);
		else
			printLog(stdout, "High bidder: %s!!!\n", line);
	} else {
		/* dutch with bids */
		int bids = ret->bids;
		int quantity = ret->quantity;
		int match = 0;

		while((line = getnontag(fp))) {
			if (!strcmp("Date of Bid", line))
				break;
		}
		if (!line) {
			printLog(stderr, "High bidder not found\n");
			return NULL;
		}
		while (bids && quantity > 0) {
			int bidQuant;

			if (!(line = getnontag(fp))) {	/* user */
				printLog(stderr, "High bidder not found\n");
				return NULL;
			}
			match = !strcmp(user, line);
			if (!(line = getnontag(fp)) ||	/* reputation */
			    !(line = getnontag(fp)) ||	/* bid */
			    !(line = getnontag(fp)) ||	/* quantity */
			    !(bidQuant = atoi(line))) {
				printLog(stderr, "High bidder not found\n");
				return NULL;
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
				return NULL;
			}
		}
		if (!match)
			printLog(stdout, "High bidder: various dutch bidders (NOT %s)\n", user);
	}


	return ret;
}

static const char QUERY_FMT[] = "aw-cgi/eBayISAPI.dll?ViewBids&item=%s";

/*
 * Get item information, return time remaining in auction if successful, -1
 * otherwise
 */
static itemInfo *
getItemInfo(char *item, char *amount, char *user, char *host, char *query)
{
	FILE *fp;
	itemInfo *ret;
	char *line, *s1, *s2;

	log(("\n\n*** getItemInfo item %s amount %s user %s\n", item, amount, user));

	if (!host)
		host = strdup(HOSTNAME);
	if (!(fp = verboseConnect(host, 10, 5))) {
		printLog(stderr, "Connect Failed.\n");
		return NULL;
	}

	if (!query) {
		query = (char *)malloc(sizeof(QUERY_FMT) + strlen(item));
		sprintf(query, QUERY_FMT, item);
	}

	log(("\n\nquery string:\n\nGET %s\n", query));
	fprintf(fp, "GET /%s\n", query);
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
	if (s1 && s2 && !strncmp("HTTP/", s1, 5) && !strcmp("302", s2)) {
		char *newHost;
		char *newQuery;
		size_t newQueryLen;

		log(("Redirect..."));
		if (match(fp, "Location: http://")) {
			printLog(stderr, "new item location not found\n");
			return NULL;
		}
		if (strcasecmp(host, (newHost = getuntilchar(fp, '/')))) {
			log(("redirect hostname is %s\n", newHost));
			host = strdup(newHost);
		}
		newQuery = getuntilchar(fp, '\n');
		newQueryLen = strlen(newQuery);
		if (newQuery[newQueryLen - 1] == '\r')
			newQuery[--newQueryLen] = '\0';
		if (strlen(query) < newQueryLen)
			query = (char *)realloc(query, newQueryLen + 1);
		strcpy(query, newQuery);

		runout(fp);
		fclose(fp);

		return getItemInfo(item, amount, user, host, query);
	}

	ret = parseItem(fp, item, amount, user, host, query);

	/* done! */
	runout(fp);
	fclose(fp);

	return ret;
}

static const char PRE_BID_FMT[] =
	"POST /ws/eBayISAPI.dll HTTP/1.0\n" \
	"Referer: http://%s/%s\n" \
	"Connection: Keep-Alive\n" \
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\n" \
	"Host: %s\n" \
	"Accept-Language: en\n" \
	"Accept-Charset: iso-8859-1,*,utf-8\n" \
	"Content-type: application/x-www-form-urlencoded\n" \
	"Content-length: %d\n";
static const char PRE_BID_CMD[] =
	"MfcISAPICommand=MakeBid&item=%s&maxbid=%s\n\n";

static const char *
preBidItem(char *item, char *amount, char *host, char *referQuery)
{
	FILE *fp;
	char *tmpkey;
	char *cp;
	char *key;
	size_t cmdlen = sizeof(PRE_BID_CMD) + strlen(item) + strlen(amount) -7;

	log(("\n\n*** preBidItem item %s amount %s\n", item,amount));

	if (!(fp = verboseConnect(HOSTNAME, 6, 5))) {
		printLog(stderr, "Connect Failed.\n");
		return 0;
	}


	log(("\n\nquery string:\n"));
	printLog(fp, PRE_BID_FMT, host, referQuery, host, cmdlen);
	printLog(fp, PRE_BID_CMD, item, amount);
	fflush(fp);

	log(("sent pre-bid\n"));

	if (match(fp, "<input type=hidden name=key value=\""))
		return 0;
	tmpkey = getuntilchar(fp, '\"');
	log(("  reported key is: %s\n", tmpkey));

	/* search for quantity here
	 * ...not done yet
	 */

	/* translate key for URL */
	key = (char *)malloc(strlen(tmpkey)*3 + 1);
	for (cp = key; *tmpkey; ++tmpkey) {
		if (*tmpkey == '$') {
			*cp++ = '%';
			*cp++ = '2';
			*cp++ = '4';
		} else
			*cp++ = *tmpkey;
	}
	*cp = '\0';

	log(("\n\ntranslated key is: %s\n\n", key));

	runout(fp);
	fclose(fp);

	log(("socket closed\n"));

	return key;
}

static const char BID_FMT[] =
	"POST /ws/eBayISAPI.dll HTTP/1.0\n"	\
	"Referer: http://%s/ws/eBayISAPI.dll\n" \
	"Connection: Keep-Alive\n" \
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\n" \
	"Host: %s\n" \
	"Accept-Language: en\n" \
	"Accept-Charset: iso-8859-1,*,utf-8\n" \
	"Content-type: application/x-www-form-urlencoded\n"
	"Content-length: %d\n";
static const char BID_CMD[] =
	"MfcISAPICommand=AcceptBid&item=%s&key=%s&maxbid=%s&quant=%s&userid=%s&pass=%s\n\n";

int
bidItem(int bid, const char *item, const char *amount, const char *quantity, const char *user, const char *password, const char *key, const char *host)
{
	FILE *fp;
	size_t cmdlen;

	log(("\n\n*** bidItem item %s amount %s quantity %s user %s password %s\n", item, amount, quantity, user, password));

	if (!bid) {
		printLog(stdout, "Bidding disabled\n");
		return 0;
	}
	if (!(fp = verboseConnect(HOSTNAME, 6, 5))) {
		printLog(stderr, "Connect Failed.\n");
		return -1;
	}

	log(("\n\nquery string:\n"));
	cmdlen = sizeof(BID_CMD) + strlen(item) + strlen(key) +
		strlen(amount) + strlen(quantity) + strlen(user) +
		strlen(password) - 15;
	printLog(fp, BID_FMT, host, host, cmdlen);
	printLog(fp, BID_CMD, item, key, amount, quantity, user, password);
	fflush(fp);

	runout(fp);
	fclose(fp);

	printLog(stdout, "Bid completed!\n");
	return 0;
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

int
main(int argc, char *argv[])
{
	long bidtime = 0;
	char *item, *amount, *quantity, *user, *password;
	const char *key = 0;
	int retryCount, now = 0, errcnt = 0, usage = 0;
	int bid = 1;	/* really make a bid? */
	const char *progname = basename(argv[0]);
	itemInfo ii, *iip;
	int c;

	while ((c = getopt(argc, argv, "dnv")) != EOF) {
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'n':
			bid = 0;
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
	ii.host = NULL;
	ii.query = NULL;
	iip = &ii;
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

	log(("item %s amount %s quantity %s user %s password %s bidtime %ld\n", item, amount, quantity, user, password, bidtime));

	signal(SIGALRM, sigAlarm);
	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, sigTerm);

	if (now) {
		iip = getItemInfo(item, amount, user, iip->host, iip->query);
		key = preBidItem(item, amount, iip->host, iip->query);
	} else {
		long prevSecs = -1;

		log(("*** WATCHING item %s amount-each %s quantity %s user %s password %s bidtime %ld\n", item, amount, quantity, user, password, bidtime));

		for (;;) {
			time_t start, latency;
			long secs;

			start = time(NULL);
			iip = getItemInfo(item, amount, user, iip->host, iip->query);
			latency = time(NULL) - start;

			if (!iip) {
				/*
				 * error!
				 *
				 * If this is the first time through the loop
				 * (i.e. prevSecs < 0), then this causes an
				 * an exit.  Otherwise, we just assume the
				 * 'net (or eBay) is flakey, sleep a reasonable
				 * time and try again.
				 */
				if (prevSecs < 0)
					exit(1);
				log((" ERROR %d!!!\n", ++errcnt));
				if (errcnt > 50) {
					printLog(stderr, "Cannot get item info\n");
					exit(1);
				}
				printLog(stdout, "Cannot find item - internet or eBay problem?\nWill try again after sleep.\n");
				secs = prevSecs/2;
			} else {
				secs = (long)(iip->remain) - bidtime;

				/* it's time!!! */
				if (secs < 0)
					break;
				/*
				 * if we're less than a minute away,
				 * get key for bid
				 */
				if (secs < 60 && !key) {
					printf("\n");
					key = preBidItem(item, amount, iip->host, iip->query);
				}
				/* laggy connection at end of auction? */
				if (secs > 7 && secs < 60 && latency > 5) {
					log((" latency %ld NO SLEEP\n", latency));
					continue;
				}
				/*
				 * special handling when we're right on the
				 * cusp
				 */
				if (secs < 8) {
					secs -= latency;
					log((" latency %ld CLOSE!!! SLEEP %ld",
						latency, secs));
					if (secs > 0)
						sleep(secs);
					break;
				} else	/* logarithmic decay by twos */
					secs /= 2;

				log((" latency %ld sleep %ld\n", latency, secs));
			}
			printf("Sleeping for %ld seconds\n", secs);
			prevSecs = secs;
			sleep(secs);
			printf("\n");
		}
	}

	log((" IT'S TIME!!!\n"));

	if (!key && bid) {
		printLog(stderr, "Problem with bid.  No bid placed.\n");
		exit(1);
	}

	log(("*** BIDDING!!! item %s amount %s quantity %s user %s password %s\n", item, amount, quantity, user, password));

	for (retryCount = 0; retryCount < 3; retryCount++) {
		if (bidItem(bid, item, amount, quantity, user, password, key, iip->host)) {
			printLog(stderr, "Bid failed!\n");
		} else
			break;
	}

	/* view item after bid */
	printLog(stdout, "\nPost-bid info:\n");
	getItemInfo(item, amount, user, iip->host, iip->query);

	exit(0);
}
