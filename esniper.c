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

static const char version[]="esniper version 2.0";
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
static char * getnontag(FILE *fp);

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
 * Bidding increments
 *
 * first number is threshold for next increment range, second is increment.
 * For example, 1.00, 0.05 means that under $1.00 the increment is $0.05.
 *
 * Increments obtained from:
 *	http://pages.ebay.com/help/basics/g-bid-increment.html
 */
static double increments[] = {
	1.00, 0.05,
	5.00, 0.25,
	25.00, 0.50,
	100.00, 1.00,
	250.00, 2.50,
	500.00, 5.00,
	1000.00, 10.00,
	2500.00, 25.00,
	5000.00, 50.00,
	-1.00, 100.00
};

/*
 * errors from parseError(), getAuctionInfo(), watchAuction()
 */
enum auctionErrorCode {
	ae_none,
	ae_baditem,
	ae_notitle,
	ae_noprice,
	ae_convprice,
	ae_noquantity,
	ae_nonumbid,
	ae_notime,
	ae_badtime,
	ae_nohighbid,
	ae_connect,
	ae_badredirect,
	ae_bidprice,
	ae_bidkey,
	ae_badpass,
	ae_outbid,
	ae_reservenotmet,
	ae_ended,
	ae_duplicate,
	ae_toomany,
	/* ae_unknown must be last error */
	ae_unknown
};

static const char *auctionErrorString[] = {
	"",
	"Auction %s: Unknown item\n",
	"Auction %s: Title not found\n",
	"Auction %s: Current price not found\n",
	"Auction %s: Cannot convert price \"%s\"\n",
	"Auction %s: Quantity not found\n",
	"Auction %s: Number of bids not found\n",
	"Auction %s: Time remaining not found\n",
	"Auction %s: Unknown time interval \"%s\"\n",
	"Auction %s: High bidder not found\n",
	"Auction %s: Connect failed\n",
	"Auction %s: Redirect failed\n",
	"Auction %s: Bid price less than minimum bid price\n",
	"Auction %s: Bid key not found\n",
	"Auction %s: Bad username or password\n",
	"Auction %s: You have been outbid\n",
	"Auction %s: Reserve not met\n",
	"Auction %s: Auction has ended\n",
	"Auction %s: Duplicate auction\n",
	"Auction %s: Too many errors, quitting\n",
	/* ae_unknown must be last error */
	"Auction %s: Unknown error code %d\n",
};

/*
 * All information associated with an auction
 */
typedef struct {
	char *auction;	/* auction number */
	char *bidPriceStr;/* price you want to bid */
	double bidPrice;/* price you want to bid (converted to double) */
	long remain;	/* seconds remaining */
	char *host;	/* bid history host */
	char *query;	/* bid history query */
	char *key;	/* bid key */
	int quantity;	/* number of items available */
	int bids;	/* number of bids made */
	double price;	/* current price */
	int bidResult;  /* result code from bid (-1=no bid yet, 0=success) */
	int won;	/* number won (-1 = no clue, 0 or greater = actual # */
	enum auctionErrorCode auctionError;/* error encountered while parsing */
	char *auctionErrorDetail;/* details of error */
} auctionInfo;

static auctionInfo *
newAuctionInfo(char *auction, char *bidPriceStr)
{
	auctionInfo *aip = (auctionInfo *)myMalloc(sizeof(auctionInfo));

	aip->auction = auction;
	aip->bidPriceStr = bidPriceStr;
	aip->bidPrice = atof(bidPriceStr);
	aip->remain = 0;
	aip->host = NULL;
	aip->query = NULL;
	aip->key = NULL;
	aip->quantity = 0;
	aip->bids = 0;
	aip->price = 0;
	aip->bidResult = -1;
	aip->won = -1;
	aip->auctionError = ae_none;
	aip->auctionErrorDetail = NULL;
	return aip;
}

static void
freeAuction(auctionInfo *aip)
{
	if (!aip)
		return;
	/* Not allocated */
	/*free(aip->auction);*/
	/*free(aip->bidPriceStr);*/
	free(aip->host);
	free(aip->query);
	free(aip->key);
	/* Not allocated */
	/*free(aip->auctionErrorDetail);*/
	free(aip);
}

/*
 * compareAuctionInfo(): used to sort auctionInfo table
 *
 * returns (-1, 0, 1) if time remaining in p1 is (less than, equal to, greater
 * than) p2
 */
static int compareAuctionInfo(const void *p1, const void *p2)
{
	long r1 = (*((auctionInfo **)p1))->remain;
	long r2 = (*((auctionInfo **)p2))->remain;

	return (r1 == r2) ? 0 : (r1 < r2 ? -1 : 1);
}

/*
 * printAuctionError()
 */
static void
printAuctionError(auctionInfo *aip, FILE *fp)
{
	enum auctionErrorCode err = aip->auctionError;

	if (err == 0)
		;
	else if (err < 0 || err >= ae_unknown)
		printLog(fp, auctionErrorString[ae_unknown], aip->auction, err);
	else
		printLog(fp, auctionErrorString[err], aip->auction,
			 aip->auctionErrorDetail);
}

/*
 * reset parse error code.
 */
static void
resetAuctionError(auctionInfo *aip)
{
	aip->auctionError = ae_none;
	free(aip->auctionErrorDetail);
	aip->auctionErrorDetail = NULL;
}

/*
 * Set parse error.
 *
 * returns: 1, so functions that fail with code 1 can return auctionError().
 */
static int
auctionError(auctionInfo *aip, enum auctionErrorCode pe, const char *details)
{
	resetAuctionError(aip);
	aip->auctionError = pe;
	if (details)
		aip->auctionErrorDetail = myStrdup(details);
	return 1;
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
logClose()
{
	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}
}

static void
logOpen(const auctionInfo *aip)
{
	char logfilename[128];

	logClose();
	sprintf(logfilename, "esniper.%s.log", aip->auction);
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

	if (debug && logfile) {
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
 * Log the next nontag.
 */
static int
logNonTag(const char *msg, FILE *fp, int ret)
{
	if (debug && logfile) {
		dlog("%s ", msg);
		dlog("%s\n", getnontag(fp));
	}
	return ret;
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
 * isValidBidPrice(): Determine if the bid price is valid.
 */
static int
isValidBidPrice(const auctionInfo *aip)
{
	double increment = 0.0;

	if (aip->bids) {
		int i;

		for (i = 0; increments[i] > 0; i += 2) {
			if (aip->bidPrice < increments[i])
				break;
		}
		increment = increments[i+1];
	}
	return aip->bidPrice >= (aip->price + increment);
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
 * skip rest of line, up to newline.  Useful for handling comments.
 */
static int
skipline(FILE *fp)
{
	int c;

	for (c = getc(fp); c!=EOF && c!='\n'; c = getc(fp))
		;
	return c;
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
parseAuction(FILE *fp, auctionInfo *aip, int quantity, char *user)
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
	printLog(stdout, "Currently: %s\n", line);
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
		while((line = getnontag(fp))) {
			if (!strcmp("Date of Bid", line))
				break;
		}
		if (!line || !(line = getnontag(fp)))
			return auctionError(aip, ae_nohighbid, NULL);
		if (strstr(line, "private auction")) {
			if (aip->bidResult == 0 && aip->price <= aip->bidPrice)
				line = user;
			else
				line = "[private]";
		}
		if (strcmp(line, user)) {
			printLog(stdout, "High bidder: %s (NOT %s)\n", line, user);
			if (!aip->remain)
				aip->won = 0;
		} else {
			printLog(stdout, "High bidder: %s!!!\n", line);
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
	"GET /%s HTTP/1.0\r\n"
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\r\n"
	"Host: %s:80\r\n"
	"Accept: text/*\r\n"
	"Accept-Language: en\r\n"
	"Accept-Charset: iso-8859-1,*,utf-8\r\n"
	"\r\n";
static const char QUERY_CMD[] = "aw-cgi/eBayISAPI.dll?ViewBids&item=%s";

/*
 * getAuctionInfo(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
static int
getAuctionInfo(auctionInfo *aip, int quantity, char *user)
{
	FILE *fp;
	char *line, *s1, *s2;
	int ret;

	log(("\n\n*** getAuctionInfo auction %s price %s user %s\n", aip->auction, aip->bidPriceStr, user));

	if (!aip->host)
		aip->host = myStrdup(HOSTNAME);
	if (!(fp = verboseConnect(aip->host, 10, 5)))
		return auctionError(aip, ae_connect, NULL);

	if (!aip->query) {
		aip->query = (char *)myMalloc(sizeof(QUERY_CMD)+strlen(aip->auction));
		sprintf(aip->query, QUERY_CMD, aip->auction);
	}

	printLog(fp, QUERY_FMT, aip->query, aip->host);
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
		if (match(fp, "Location: http://"))
			return auctionError(aip, ae_badredirect, NULL);
		if (strcasecmp(aip->host, (newHost = getuntilchar(fp, '/')))) {
			log(("redirect hostname is %s\n", newHost));
			free(aip->host);
			aip->host= myStrdup(newHost);
		}
		newQuery = getuntilchar(fp, '\n');
		newQueryLen = strlen(newQuery);
		if (newQuery[newQueryLen - 1] == '\r')
			newQuery[--newQueryLen] = '\0';
		if (strcmp(aip->query, newQuery)) {
			free(aip->query);
			aip->query = myStrdup(newQuery);
		}

		runout(fp);
		fclose(fp);

		return getAuctionInfo(aip, quantity, user);
	}

	ret = parseAuction(fp, aip, quantity, user);

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
preBidAuction(auctionInfo *aip)
{
	FILE *fp;
	char *tmpkey;
	char *cp;
	size_t cmdlen = sizeof(PRE_BID_CMD) + strlen(aip->auction) + strlen(aip->bidPriceStr) -9;
	int ret = 0;

	log(("\n\n*** preBidAuction auction %s price %s\n", aip->auction, aip->bidPriceStr));

	if (!(fp = verboseConnect(HOSTNAME, 6, 5)))
		return auctionError(aip, ae_connect, NULL);


	log(("\n\nquery string:\n"));
	printLog(fp, PRE_BID_FMT, aip->host, aip->query, HOSTNAME, cmdlen);
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

static const char CONGRATS[] = "Congratulations...";

/*
 * Place bid.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
int
bidAuction(int bid, auctionInfo *aip, int quantity, const char *user, const char *password)
{
	FILE *fp;
	size_t cmdlen;
	int i;
	char *line;
	char quantityStr[12];	/* must hold an int */

	log(("\n\n*** bidAuction auction %s price %s quantity %d user %s\n", aip->auction, aip->bidPriceStr, quantity, user));

	if (!bid) {
		printLog(stdout, "Bidding disabled\n");
		return aip->bidResult = 0;
	}
	if (!(fp = verboseConnect(HOSTNAME, 6, 5)))
		return aip->bidResult = auctionError(aip, ae_connect, NULL);

	if (aip->quantity < quantity)
		quantity = aip->quantity;
	sprintf(quantityStr, "%d", quantity);

	log(("\n\nquery string:\n"));
	cmdlen = sizeof(BID_CMD) + strlen(aip->auction) + strlen(aip->key) +
		strlen(aip->bidPriceStr) + strlen(quantityStr) + strlen(user) +
		strlen(password) - 17;
	printLog(fp, BID_FMT, aip->host, HOSTNAME, cmdlen);
	printLog(fp, BID_CMD, aip->auction, aip->key, aip->bidPriceStr, quantityStr, user,password);
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

	runout(fp);
	fclose(fp);

	return aip->bidResult;
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

	strftime(buf, 80, "%c", tmp);
	return buf;
}

/*
 * watchAuction(): watch auction until it is time to bid
 *
 * returns:
 *	0 OK
 *	1 Error
 */
static int
watchAuction(auctionInfo *aip, int quantity, char *user, long bidtime)
{
	int errorCount = 0;
	long remain = -1, sleepTime = -1;

	log(("*** WATCHING auction %s price-each %s quantity %d user %s bidtime %ld\n", aip->auction, aip->bidPriceStr, quantity, user, bidtime));

	for (;;) {
		time_t start = time(NULL);
		int ret = getAuctionInfo(aip, quantity, user);
		time_t latency = time(NULL) - start;

		if (ret) {
			printAuctionError(aip, stderr);

			/* fatal error? */
			if (remain == -1) {	/* first time */
				int j, ret = 1;

				for (j = 0; ret && j < 3 && aip->auctionError == ae_notitle; ++j)
					ret=getAuctionInfo(aip, quantity, user);
				if (!ret)
					remain = aip->remain - bidtime - (latency * 2);
				else
					return 1;
			} else {
				/* non-fatal error */
				log((" ERROR %d!!!\n", ++errorCount));
				if (errorCount > 50)
					return auctionError(aip, ae_toomany, NULL);
				printLog(stdout, "Cannot find auction - internet or eBay problem?\nWill try again after sleep.\n");
				remain -= sleepTime + (latency * 2);
			}
		} else if (!isValidBidPrice(aip))
			return auctionError(aip, ae_bidprice, NULL);
		else
			remain = aip->remain - bidtime - (latency * 2);

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
				if (!preBidAuction(aip))
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
} /* watchAuction() */

/*
 * readAuctions(): read auctions from file
 *
 * returns: number of auctions, or -1 if error
 */
static int
readAuctions(FILE *fp, auctionInfo ***aip)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;
	int i, j;
	int c;
	int numAuctions = 0;
	int line;

	while ((c = getc(fp)) != EOF) {
		if (isspace(c))
			continue;
		else if (c == '#')
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
					return -1;
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
					return -1;
				}
			}
			addchar(buf, bufsize, count, '\n');
		} else {
			fprintf(stderr, "Invalid auction line: ");
			do {
				putc(c, stderr);
			} while ((c = getc(fp)) != EOF && c != '\n');
			putc('\n', stderr);
			return -1;
		}
		if (c == EOF)
			break;
	}

	*aip = (auctionInfo **)malloc(sizeof(auctionInfo *) * numAuctions);

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

	return numAuctions;
} /* readAuctions() */

/*
 * readAuctionFile(): read a file listing auctions to watch.
 *
 * returns: number of auctions to watch
 */
static int
readAuctionFile(const char *filename, auctionInfo ***aip)
{
	FILE *fp = fopen(filename, "r");
	int numAuctions = 0;

	if (fp == NULL) {
		fprintf(stderr, "Cannot open %s: %s\n", filename,
			strerror(errno));
		exit(1);
	}
	numAuctions = readAuctions(fp, aip);
	fclose(fp);
	if (numAuctions == 0)
		fprintf(stderr, "Cannot find any auctions!\n");
	if (numAuctions <= 0)
		exit(1);
	return numAuctions;
} /* readAuctionFile() */

/*
 * Get initial auction info, sort items based on end time.
 */
static int
sortAuctions(auctionInfo **auctions, int numAuctions, char *user, int quantity)
{
	int i, sawError = 0;

	for (i = 0; i < numAuctions; ++i) {
		int j;

		if (debug)
			logOpen(auctions[i]);
		for (j = 0; j < 3; ++j) {
			if (j > 0)
				printLog(stderr, "Retrying...\n");
			if (!getAuctionInfo(auctions[i], quantity, user))
				break;
			printAuctionError(auctions[i], stderr);
		}
		if (j == 3)
			exit(1);
		printLog(stdout, "\n");
	}
	if (numAuctions > 1) {
		printLog(stdout, "Sorting auctions...\n");
		/* sort by end time */
		qsort(auctions, numAuctions, sizeof(auctionInfo *), compareAuctionInfo);
	}

	/* get rid of obvious cases */
	for (i = 0; i < numAuctions; ++i) {
		auctionInfo *aip = auctions[i];

		if (!aip->remain)
			auctionError(aip, ae_ended, NULL);
		else if (!isValidBidPrice(aip))
			auctionError(aip, ae_bidprice, NULL);
		else if (i > 0 && auctions[i-1] &&
			 !strcmp(aip->auction, auctions[i-1]->auction))
			auctionError(aip, ae_duplicate, NULL);
		else
			continue;
		printAuctionError(aip, stderr);
		freeAuction(auctions[i]);
		auctions[i] = NULL;
		++sawError;
	}

	/* eliminate dead auctions */
	if (sawError) {
		int j;

		for (i = j = 0; i < numAuctions; ++i) {
			if (auctions[i])
				auctions[j++] = auctions[i];
		}
		numAuctions -= sawError;
	}
	return numAuctions;
} /* sortAuctions() */

/* cleanup open files */
static void
cleanup()
{
	logClose();
}

int
main(int argc, char *argv[])
{
	long bidtime = 0;
	char *user, *password;
	int quantity;
	int now = 0, usage = 0;
	int bid = 1;	/* really make a bid? */
	int ret = 1;	/* assume failure, change if successful */
	const char *progname = basename(argv[0]);
	auctionInfo **auctions = NULL;
	char *filename = NULL;
	int c, i;
	int argcmin = 5, argcmax = 6;
	int numAuctions = 0, numAuctionsOrig = 0;

	atexit(cleanup);

	while ((c = getopt(argc, argv, "df:npv")) != EOF) {
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'f':
			filename = optarg;
			argcmin = 3;
			argcmax = 4;
			break;
		case 'n':
			bid = 0;
			break;
		case 'p':	/* secret option - for testing page parsing */
			testParser(argc, argv);
			exit(0);
		case 'v':
			fprintf(stderr, "%s\n%s\n", version, blurb);
			exit(0);
		case '?':
			usage = 1;
		}
	}
	argc -= optind;
	argv += optind;

	if (usage || argc < argcmin || argc > argcmax) {
		fprintf(stderr,
			"usage: %s [-dn] auction price quantity username password [secs|now]\n"
			"       %s [-dn] -f <file> quantity username password [secs|now]\n"
			"\n"
			"where:\n"
			"-d: write debug output to file\n"
			"-f: read auction data from file\n"
			"-n: do not place bid\n"
			"-v: print version and exit\n"
			"\n"
			"Bid is placed %d seconds before the end of auction unless some other time or\n"
			"\"now\" is specified.\n"
			"\n"
			"%s\n",
			progname, progname, DEFAULT_BIDTIME, blurb);
		exit(1);
	}

	/* init variables */
	if (filename) {
		numAuctions = readAuctionFile(filename, &auctions);
	} else {
		numAuctions = 1;
		auctions = (auctionInfo **)malloc(sizeof(auctionInfo *));
		auctions[0] = newAuctionInfo(argv[0], argv[1]);
	}
	quantity = atoi(argv[argcmax - 4]);
	if (quantity <= 0) {
		printLog(stderr, "Invalid quantity: %s\n", argv[argcmax - 4]);
		exit(1);
	}
	user = argv[argcmax - 3];
	password = argv[argcmax - 2];
	argv[argcmax - 2] = "";	/* clear password from command line */
	if (argc == argcmax) {
		now = !strcmp("now", argv[argcmax - 1]);
		if (!now) {
			if ((bidtime = atol(argv[argcmax - 1])) < MIN_BIDTIME) {
				printf("NOTE: minimum bid time %d seconds\n",
					MIN_BIDTIME);
				bidtime = MIN_BIDTIME;
			}
		}
	} else
		bidtime = DEFAULT_BIDTIME;

	signal(SIGALRM, sigAlarm);
	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, sigTerm);

	numAuctionsOrig = numAuctions;
	numAuctions = sortAuctions(auctions, numAuctions, user, quantity);

	for (i = 0; i < numAuctions && quantity > 0; ++i) {
		int retryCount, bidRet;

		if (!auctions[i])
			continue;

		if (debug)
			logOpen(auctions[i]);

		log(("auction %s price %s quantity %d user %s bidtime %ld\n",
			auctions[i]->auction, auctions[i]->bidPriceStr, quantity, user, bidtime));

		if (numAuctionsOrig > 1)
			printLog(stdout, "\nNeed to win %d item(s), %d auction(s) remain\n\n", quantity, numAuctions - i);

		if (now) {
			if (preBidAuction(auctions[i])) {
				printAuctionError(auctions[i], stderr);
				continue;
			}
		} else {
			if (watchAuction(auctions[i], quantity, user, bidtime)){
				printAuctionError(auctions[i], stderr);
				continue;
			}
		}

		/* ran out of time! */
		if (!auctions[i]->remain) {
			auctionError(auctions[i], ae_ended, NULL);
			printAuctionError(auctions[i], stderr);
			continue;
		}

		if (bid)
			printLog(stdout, "\nAuction %s: Bidding...\n", auctions[i]->auction);

		if (!auctions[i]->key && bid) {
			printLog(stderr, "Auction %s: Problem with bid.  No bid placed.\n", auctions[i]->auction);
			continue;
		}

		log(("*** BIDDING!!! auction %s price %s quantity %d user %s\n",
			auctions[i]->auction, auctions[i]->bidPriceStr, quantity, user));

		for (retryCount = 0; retryCount < 3; retryCount++) {
			bidRet = bidAuction(bid, auctions[i], quantity, user, password);

			if (!bidRet || auctions[i]->auctionError != ae_connect)
				break;
			printLog(stderr, "Auction %s: retrying...\n", auctions[i]->auction);
		}

		/* failed bid */
		if (bidRet) {
			printAuctionError(auctions[i], stderr);
			continue;
		}

		/* view auction after bid */
		if (bidtime > 0 && bidtime < 60) {
			printLog(stdout, "Auction %s: Waiting %d seconds for auction to complete...\n", auctions[i]->auction, bidtime);
			sleep(bidtime + 1); /* make sure it really is over */
		}

		printLog(stdout, "\nAuction %s: Post-bid info:\n", auctions[i]->auction);
		if (getAuctionInfo(auctions[i], quantity, user))
			printAuctionError(auctions[i], stderr);

		if (auctions[i]->won == -1) {
			int won = auctions[i]->quantity;

			if (quantity < won)
				won = quantity;
			quantity -= won;
			printLog(stdout, "\nunknown outcome, assume that you have won %d items\n", won);
		} else {
			quantity -= auctions[i]->won;
			printLog(stdout, "\nwon %d items\n", auctions[i]->won);
			ret = 0;
		}
	}

	exit(ret);
}
