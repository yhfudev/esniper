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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#ifndef __CYGWIN__
#include <libgen.h>
#endif
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* various buffer sizes.  Assume these sizes are big enough (I hope!) */
#define COMMAND_LEN	1024
#define QUERY_LEN	1024
#define KEY_LEN		1024
#define TIME_BUF_SIZE	1024


/* bid time, in seconds before end of auction */
#define MIN_BIDTIME	5
#define DEFAULT_BIDTIME	10

#define HOSTNAME	"cgi.ebay.com"

static FILE *logfile = NULL;
static int debug = 0;

/*
 * Debugging functions and macros.
 */
static void
logOpen(const char *item)
{
	char logfilename[128];

	sprintf(logfilename, "sniper.log.%s", item);
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

#ifdef __CYGWIN__
/*
 * Cygwin doesn't provide basename?
 */
static char *
basename(char *s)
{
	char *slash;
	char *backslash;

	if (!s)
		return s;

	slash = strrchr(s, '/');
	backslash = strrchr(s, '\\');
	return slash > backslash ? slash + 1 : (backslash ? backslash + 1 : s);
}
#endif

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
		rc = connect(sockfd, (struct sockaddr *) &servAddr, sizeof(struct sockaddr_in));
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

// attempt to match some input, ignoring \r and \n
// returns 0 on success, -1 on failure
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

// attempt to match some input, case insensitive
// returns 0 on success, -1 on failure
static int
matchcase(FILE *fp, const char *str)
{
	const char *cursor;
	int c;

	log(("\n\nmatchcase(\"%s\")\n\n", str));

	cursor = str;
	while ((c = getc(fp)) != EOF) {
		if (debug)
			logChar(c);
		if (toupper((char)c) == toupper(*cursor)) {
			if (*++cursor == '\0')
				return 0;
		} else if (c != '\n' && c != '\r')
			cursor = str;
	}
	return -1;
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

/*
 * Strip out multiple spaces and cr/lf
 */
static char *
stripchars(char *cp)
{
	char *ret = cp, *cp1 = cp;

	// skip leading whitespace
	for (; isspace((int)*cp); ++cp)
		;
	// cut whitespace (except for a single space) between non-space chars
	for (; *cp; ++cp) {
		if (isspace((int)*cp)) {
			if (!isspace((int)*(cp+1)))
				*cp1++ = ' ';
		} else
			*cp1++ = *cp;
	}
	// knock off trailing whitespace (there can only be 1, due to above)
	if (isspace((int)*(cp1-1)))
		*(cp1-1) = '\0';
	else
		*cp1 = '\0';

	return ret;
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

/*
 * Get item information, return time remaining in auction if successful, -1
 * otherwise
 */
static long
getItemInfo(char *item, char *amount, char *user)
{
	FILE *sockfp;
	static char host[QUERY_LEN];
	static char query[QUERY_LEN];
	static int firstTime = 1;
	char *timestr;
	long secs;
	char currently[QUERY_LEN];
	double currentlyD;
	char highBidder[QUERY_LEN];

	log(("\n\n*** getItemInfo item %s amount %s user %s\n", item, amount, user));

	if (firstTime)
		strcpy(host, HOSTNAME);

	if (!(sockfp = verboseConnect(host, 10, 5))) {
		printLog(stderr, "Connect Failed.\n");
		return -1;
	}

	if (firstTime) {
		firstTime = 0;
		snprintf(query, QUERY_LEN, "GET /aw-cgi/eBayISAPI.dll?ViewItem&item=%s\n", item);
	}

	log(("\n\nquery string:\n\n%s\n", query));
	fwrite(query, 1, strlen(query), sockfp);
	fflush(sockfp);

	/* redirect? */
	if (!strncmp("HTTP/1.0 302", getuntilchar(sockfp, '\n'), 12)) {
		char *newHost;
		char *newQuery;
		size_t newQueryLen;

		log(("Redirect..."));
		if (match(sockfp, "Location: http://")) {
			printLog(stderr, "new item location not found\n");
			return -1;
		}
		if (strcasecmp(host, (newHost = getuntilchar(sockfp, '/')))) {
			printLog(stdout, "redirect hostname is %s, not %s\n",
				newHost, host);
			strcpy(host, newHost);
		}
		newQuery = getuntilchar(sockfp, '\n');
		newQueryLen = strlen(newQuery);
		if (newQuery[newQueryLen - 1] == '\r')
			newQuery[newQueryLen - 1] = '\0';
		snprintf(query, QUERY_LEN, "GET /%s\n", newQuery);

		runout(sockfp);
		fclose(sockfp);
		return getItemInfo(item, amount, user);
	}

	if (matchcase(sockfp, "<title>") ||
	    match(sockfp, " - ")) {
		printLog(stderr, "Item title not found\n");
		return -1;
	}
	printLog(stdout, "Item %s: %s\n", item,
		stripchars(getuntilchar(sockfp, '<')));

	if (match(sockfp, "Currently") ||
	    matchcase(sockfp, "<b>")) {
		printLog(stderr, "\"Currently\" not found\n");
		return -1;
	}
	strncpy(currently, getuntilchar(sockfp, '<'), 1024);
	printLog(stdout, "Currently: %s\n", currently);

	if (match(sockfp, "# of bids") ||
	    matchcase(sockfp, "<b>")) {
		printLog(stderr, "\"# of bids\" not found\n");
		return -1;
	}
	printLog(stdout, "Bids: %s\n", getuntilchar(sockfp, '<'));

	if (match(sockfp, "Time left") ||
	    matchcase(sockfp, "<b>")) {
		printLog(stderr, "\"Time left\" not found\n");
		return -1;
	}
	timestr = stripchars(getuntilchar(sockfp, '<'));
	secs = getseconds(timestr);
	printLog(stdout, "Time remaining: %s (%ld seconds)\n", timestr, secs);

	if (match(sockfp, "High bid") ||
	    matchcase(sockfp, "colspan=\"4\">")) {
		printLog(stderr, "\"High bid\" not found\n");
		return -1;
	}
	if (strcmp(stripchars(getuntilchar(sockfp, '<')), "--")) {
		if (matchcase(sockfp, "<a href") ||
		    match(sockfp, ">")) {
			printLog(stderr, "High bidder amount not found\n");
			return -1;
		}
		log(("High:"));
		strncpy(highBidder, getuntilchar(sockfp, '<'), 1024);
		/* sometimes <a href ...> is duplicated */
		while (*highBidder == '\0') {
			if (match(sockfp, ">")) {
				printLog(stderr, "High bidder amount not found\n");
				return -1;
			}
			strncpy(highBidder, getuntilchar(sockfp, '<'), 1024);
		}
		printf("High bidder: %s", highBidder);
		if (strcmp(highBidder, user))
			printf(" (NOT %s)\n", user);
		else
			printf("!!!\n");
	} else {
		printf("High bidder: --\n");
	}

	currentlyD = atof(currently + strcspn(currently, "0123456789"));
	if (currentlyD < 0.01) {
		printLog(stderr, "Cannot convert amount %s\n", currently);
		exit(1);
	}
	if (currentlyD > atof(amount)) {
		printLog(stdout, "Maximum bid exceeded!\n");
		exit(0);
	}

	runout(sockfp);
	fclose(sockfp);

	return secs;
}

#define PRE_BID_PFX	\
	"POST /aw-cgi/eBayISAPI.dll HTTP/1.0\n" \
	"Referer: http://" HOSTNAME "/aw-cgi/eBayISAPI.dll?ViewItem&item=%s\n" \
	"Connection: Keep-Alive\n" \
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\n" \
	"Host: " HOSTNAME "\n" \
	"Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\n" \
	"Accept-Encoding: gzip\n" \
	"Accept-Language: en\n" \
	"Accept-Charset: iso-8859-1,*,utf-8\n" \
	"Content-type: application/x-www-form-urlencoded\n"

static const char *
preBidItem(char *item, char *amount)
{
	FILE *sockfp;
	char query[QUERY_LEN];
	char eBayISAPICommand[COMMAND_LEN];
	char *tmpkey;
	char *cp;
	static char key[KEY_LEN];

	log(("\n\n*** preBidItem item %s amount %s\n", item,amount));

	if (!(sockfp = verboseConnect(HOSTNAME, 6, 5))) {
		printLog(stderr, "Connect Failed.\n");
		return 0;
	}

	snprintf(eBayISAPICommand, COMMAND_LEN,
		 "MfcISAPICommand=MakeBid&item=%s&maxbid=%s\n", item, amount);

	snprintf(query, QUERY_LEN,
		 PRE_BID_PFX "Content-length: %d\n%s\n",
		 item, strlen(eBayISAPICommand), eBayISAPICommand);

	log(("\n\nquery string: %s\n", query));
	fwrite(query, 1, strlen(query), sockfp);
	fflush(sockfp);

	log(("sent pre-bid\n"));

	if (match(sockfp, "<input type=hidden name=key value=\""))
		return 0;
	tmpkey = getuntilchar(sockfp, '\"');
	log(("  reported key is: %s\n", tmpkey));

	/* search for quantity here
	 * ...not done yet
	 */

	/* translate key for URL */
	cp = key;
	*cp = '\0';
	for (; *tmpkey; ++tmpkey) {
		if (*tmpkey == '$') {
			*cp++ = '%';
			*cp++ = '2';
			*cp++ = '4';
		} else
			*cp++ = *tmpkey;
	}

	log(("\n\ntranslated key is: %s\n\n", key));

	runout(sockfp);
	fclose(sockfp);

	log((" socket closed\n"));

	return key;
}

#define BID_PFX	\
	"POST /aw-cgi/eBayISAPI.dll HTTP/1.0\n"	\
	"Referer: http://" HOSTNAME "/aw-cgi/eBayISAPI.dll\n" \
	"Connection: Keep-Alive\n" \
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\n" \
	"Host: " HOSTNAME "\n" \
	"Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\n" \
	"Accept-Encoding: gzip\n" \
	"Accept-Language: en\n" \
	"Accept-Charset: iso-8859-1,*,utf-8\n" \
	"Content-type: application/x-www-form-urlencoded\n"

int
bidItem(int bid, const char *item, const char *amount, const char *quantity, const char *user, const char *password, const char *key)
{
	FILE *sockfp;
	char query[QUERY_LEN];
	char eBayISAPICommand[COMMAND_LEN];

	log(("\n\n*** bidItem item %s amount %s quantity %s user %s password %s\n", item, amount, quantity, user, password));

	if (bid) {
		if (!(sockfp = verboseConnect(HOSTNAME, 6, 5))) {
			printLog(stderr, "Connect Failed.\n");
			return -1;
		}

		snprintf(eBayISAPICommand, COMMAND_LEN,
			 "MfcISAPICommand=AcceptBid&item=%s&key=%s&maxbid=%s&quant=%s&userid=%s&pass=%s\n",
			 item, key, amount, quantity, user, password);

		snprintf(query, QUERY_LEN,
			 BID_PFX "Content-length: %d\n" "%s\n",
			 strlen(eBayISAPICommand), eBayISAPICommand);

		log(("\n\nquery string: %s\n", query));
		fwrite(query, 1, strlen(query), sockfp);
		fflush(sockfp);

		runout(sockfp);
		fclose(sockfp);

		printLog(stdout, "Bid completed!\n");
	} else
		printLog(stdout, "Bidding disabled\n");
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
	time_t start, latency;
	char *item, *amount, *quantity, *user, *password;
	const char *key = 0;
	int retryCount, now = 0, errcnt = 0, usage = 0;
	int bid = 1;	/* really make a bid? */
	const char *progname = basename(argv[0]);
	int c;

	while ((c = getopt(argc, argv, "dn")) != EOF) {
		switch (c) {
		case 'd':
			debug = 1;
			break;
		case 'n':
			bid = 0;
			break;
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
			"\n"
			"Bid is placed %d seconds before the end of auction unless some other time or\n"
			"\"now\" is specified.\n",
			progname, DEFAULT_BIDTIME);
		return(1);
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

	start = time(NULL);

	log(("item %s amount %s quantity %s user %s password %s bidtime %ld\n", item, amount, quantity, user, password, bidtime));
	log(("program start time %s\n\n", ctime(&start)));

	signal(SIGALRM, sigAlarm);
	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, sigTerm);

	if (now) {
		getItemInfo(item, amount, user);
		key = preBidItem(item, amount);
	} else {
		long secs, prevSleep = -1;

		log(("*** WATCHING item %s amount-each %s quantity %s user %s password %s bidtime %ld\n", item, amount, quantity, user, password, bidtime));

		for (;;) {
			start = time(NULL);
			secs = getItemInfo(item, amount, user);
			latency = time(NULL) - start;

			if (secs < 0) {
				/*
				 * error!
				 *
				 * If this is the first time through the loop
				 * (i.e. prevSleep < 0), then this causes an
				 * an exit.  Otherwise, we just assume the
				 * 'net (or eBay) is flakey, sleep a reasonable
				 * time and try again.
				 */
				if (prevSleep < 0)
					exit(1);
				log((" ERROR %d!!!\n", ++errcnt));
				if (errcnt > 50) {
					printLog(stderr, "Cannot get item info\n");
					exit(1);
				}
				printLog(stdout, "Cannot find item - internet or eBay problem?\nWill try again after sleep.\n");
				secs = prevSleep/2;
			} else {
				secs -= bidtime;
				prevSleep = secs;

				/* it's time!!! */
				if (secs < 0)
					break;
				/*
				 * if we're less than a minute away,
				 * get key for bid
				 */
				if (secs < 60 && !key) {
					printf("\n");
					key = preBidItem(item, amount);
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
			prevSleep = secs;
			sleep(secs);
			printf("\n");
		}
	}

	log((" IT'S TIME!!!\n"));

	if (!key) {
		printLog(stderr, "Problem with bid.  No bid placed.\n");
		return(1);
	}

	log(("*** BIDDING!!! item %s amount %s quantity %s user %s password %s\n", item, amount, quantity, user, password));

	for (retryCount = 0; retryCount < 3; retryCount++) {
		if (bidItem(bid, item, amount, quantity, user, password, key)) {
			printLog(stderr, "Bid failed!\n");
		} else
			break;
	}

	/* view item after bid */
	printLog(stdout, "\nPost-bid info:\n");
	getItemInfo(item, amount, user);

	return(0);
}
