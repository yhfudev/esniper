/*
 * Copyright (c) 2002, 2003, Scott Nicol <esniper@users.sf.net>
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

#include "util.h"
#include "esniper.h"
#include "buffer.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#if defined(WIN32)
#	include <windows.h>
#	include <io.h>
#	include <sys/timeb.h>
#	define strncasecmp(s1, s2, len) strnicmp((s1), (s2), (len))
#else
#	include <sys/time.h>
#	include <termios.h>
#	include <unistd.h>
#endif

static void toLowerString(char *s);
static void seedPasswordRandom(void);
static void cryptPassword(char *password);

/*
 * various utility functions used in esniper.
 */

/*
 * Replacement malloc/realloc/strdup, with error checking
 */

void *
myMalloc(size_t size)
{
	void *ret = malloc(size);

	if (!ret) {
		printLog(stderr, "Cannot allocate memory: %s\n", strerror(errno));
		exit(1);
	}
	return ret;
}

void *
myRealloc(void *buf, size_t size)
{
	void *ret = buf ? realloc(buf, size) : malloc(size);

	if (!ret) {
		printLog(stderr, "Cannot reallocate memory: %s\n", strerror(errno));
		exit(1);
	}
	return ret;
}

char *
myStrdup(const char *s)
{
	char *ret;
	size_t len;

	if (!s)
		return NULL;
	len = strlen(s);
	ret = myMalloc(len + 1);
	memcpy(ret, s, len);
	ret[len] = '\0';
	return ret;
}

char *
myStrndup(const char *s, size_t len)
{
	char *ret;

	if (!s)
		return NULL;
	ret = myMalloc(len + 1);
	memcpy(ret, s, len);
	ret[len] = '\0';
	return ret;
}

char *
myStrdup2(const char *s1, const char *s2)
{
	char *ret = myMalloc(strlen(s1) + strlen(s2) + 1);

	sprintf(ret, "%s%s", s1, s2);
	return ret;
}

char *
myStrdup3(const char *s1, const char *s2, const char *s3)
{
	char *ret = myMalloc(strlen(s1) + strlen(s2) + strlen(s3) + 1);

	sprintf(ret, "%s%s%s", s1, s2, s3);
	return ret;
}

char *
myStrdup4(const char *s1, const char *s2, const char *s3, const char *s4)
{
	char *ret = myMalloc(strlen(s1) + strlen(s2) + strlen(s3) + strlen(s4) + 1);

	sprintf(ret, "%s%s%s%s", s1, s2, s3, s4);
	return ret;
}

/*
 * Debugging functions.
 */

static FILE *logfile = NULL;

void
logClose()
{
	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}
}

void
logOpen(const char *progname, const auctionInfo *aip, const char *logdir)
{
	char *logfilename;

	if (aip == NULL)
		logfilename = myStrdup2(progname, ".log");
	else
		logfilename = myStrdup4(progname, ".", aip->auction, ".log");
	if (logdir) {
		char *tmp = logfilename;

		logfilename = myStrdup3(logdir, "/", logfilename);
		free(tmp);
	}
	logClose();
	if (!(logfile = fopen(logfilename, "a"))) {
                /* non-fatal error! */
		fprintf(stderr, "Unable to open log file %s: %s\n",
			logfilename, strerror(errno));
	}
	free(logfilename);
}

/*
 * va_list version of log
 */
void
vlog(const char *fmt, va_list arglist)
{
#if defined(WIN32)
	struct timeb tb;
#else
	struct timeval tv;
#endif
	char timebuf[80];	/* more than big enough */
	time_t t;

	if (!logfile)
		return;

#if defined(WIN32)
	ftime(&tb);
	t = (time_t)(tb.time);
	strftime(timebuf, sizeof(timebuf), "\n\n*** %Y-%m-%d %H:%M:%S", localtime(&t));
	fprintf(logfile, "%s.%03d ", timebuf, tb.millitm);
#else
	gettimeofday(&tv, NULL);
	t = (time_t)(tv.tv_sec);
	strftime(timebuf, sizeof(timebuf), "\n\n*** %Y-%m-%d %H:%M:%S", localtime(&t));
	fprintf(logfile, "%s.%06ld ", timebuf, (long)tv.tv_usec);
#endif
	vfprintf(logfile, fmt, arglist);
	fflush(logfile);
}

/*
 * Debugging log function.  Use like printf.  Or, better yet, use the log()
 * macro (but be sure to enclose the argument list in two parens)
 */
void
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
void
printLog(FILE *fp, const char *fmt, ...)
{
	va_list arglist;

	if (options.debug && logfile) {
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
void
logChar(int c)
{
	if (!logfile)
		return;

	if (c == EOF)
		fflush(logfile);
	else
		putc(c, logfile);
}

/* read from file until you see the given character. */
char *
getUntil(FILE *fp, int until)
{
	static char *buf = NULL;
	static size_t bufsize = 0;
	size_t count = 0;
	int c;

	log(("\n\ngetUntilChar('%c')\n\n", until));

	while ((c = getc(fp)) != EOF) {
		if (options.debug)
			logChar(c);
		if ((char)c == until) {
			term(buf, bufsize, count);
			if (options.debug)
				logChar(EOF);
			return buf;
		}
		addchar(buf, bufsize, count, c);
	}
	if (options.debug)
		logChar(EOF);
	return NULL;
}

/* read one complete line, discarding \r and \n */
char *
getLine(FILE *fp)
{
	char *line = getUntil(fp, '\n');

	if (line) {
		int len = strlen(line);

		if (line[len - 1] == '\r')
			line[len - 1] = '\0';
	}
	return line;
}

/* Runout remainder of file, logging its contents */
void
runout(FILE *fp)
{
	if (options.debug) {
		int c, count;

		dlog("\n\nrunout()\n\n");
		for (count = 0, c = getc(fp); c != EOF; ++count, c = getc(fp))
			logChar(c);
		logChar(EOF);
		dlog("%d bytes", count);
	}
}

/*
 * Return a valid string, even if it is null
 */
const char *
nullStr(const char *s)
{
	return s ? s : "(null)";
}

/*
 * Current date/time
 */
char *
timestamp()
{
	static char buf[80];	/* much larger than needed */
	static time_t saveTime = 0;
	time_t t = time(0);

	if (t != saveTime) {
		struct tm *tmp = localtime(&t);

		strftime(buf, (size_t)sizeof(buf), "%c", tmp);
		saveTime = t;
	}
	return buf;
}

/*
 * skip rest of line, up to newline.  Useful for handling comments.
 */
int
skipline(FILE *fp)
{
	int c;

	for (c = getc(fp); c != EOF && c != '\n'; c = getc(fp))
		;
	return c;
}

/*
 * Prompt, with or without echo.  Returns malloc()'ed buffer containing
 * response.
 */
char *
prompt(const char *p, int noecho)
{
	char *buf = NULL;
	size_t size = 0, count = 0;
	int c;
#if defined(WIN32)
	HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
	DWORD save, tmp;

	if (in == INVALID_HANDLE_VALUE || GetFileType (in) != FILE_TYPE_CHAR)
		noecho = 0;
#else
	struct termios save, tmp;
#endif

	if (!isatty(fileno(stdin))) {
		printLog(stderr, "Cannot prompt, stdin is not a terminal\n");
		return NULL;
	}

	fputs(p, stdout);

	if (noecho) {	/* echo off */
#if defined(WIN32)
		GetConsoleMode(in, &save);
		tmp = save & (~ENABLE_ECHO_INPUT);
		SetConsoleMode(in, tmp);
#else
		tcgetattr(fileno(stdin), &save);
		memcpy(&tmp, &save, sizeof(struct termios));
		tmp.c_lflag &= (~ECHO);
		tcsetattr(fileno(stdin), TCSANOW, &tmp);
#endif
	}

	/* read value */
	for (c = getc(stdin); c != EOF && c != '\n'; c = getc(stdin))
		addcharinc(buf, size, count, c, (size_t)20);
	terminc(buf, size, count, (size_t)20);

	if (noecho) {	/* echo on */
#if defined(WIN32)
		SetConsoleMode(in, save);
#else
		tcsetattr(fileno(stdin), TCSANOW, &save);
#endif
		putchar('\n');
	}

	return buf;
}

/*
 * Converts string to boolean.
 *  returns 0 (false), 1 (true), or -1 (invalid).  NULL is true.
 */
int
boolValue(const char *value)
{
   static const char* boolvalues[] =
      {
         "0",
         "1",
         "n",
         "y",
         "no",
         "yes",
         "off",
         "on",
         "false",
         "true",
         "disabled",
         "enabled",
         NULL
      };
   int i;
   char *buf;

   if (!value)
      return 1;

   buf = myStrdup(value);
   toLowerString(buf);
   for (i = 0; boolvalues[i]; i++) {
      if (!strcmp(buf, boolvalues[i]))
         break;
   }
   free(buf);
   return boolvalues[i] ? i % 2 : -1;
}

/*
 * Converts string to Proxy host/port.  Returns 0 if parsed OK, 1 on error.
 *
 * Proxy can be of the following forms:
 *
 *	"http://host.at.some.domain:80/"
 *	"http://host.at.some.domain/"
 *	"host.at.some.domain:8080"
 *	"host.at.some.domain"
 *	""
 *
 * If the port is not specified, it is 80.  If the string is empty, proxy is
 * disabled.
 */
int
parseProxy(const char *value, proxy_t *proxy)
{
	const char *cp = value, *host;
	int port = 80;	/* default */
	size_t len;

	if (!cp) {
		free(proxy->host);
		proxy->host = NULL;
		return 0;
	}

	if (!strncasecmp(cp, "http://", 7))
		cp += 7;
	len = strcspn(cp, ":/");
	if (!len) {
		free(proxy->host);
		proxy->host = NULL;
		return 0;
	}
	host = cp;
	cp += len;
	switch (*cp) {
	case ':':
		if (isdigit((int)(*++cp))) {
			char *end = NULL;

			errno = 0;
			port = (int)strtol(cp, &end, 10);
			if (errno || !end)
				return 1;
			cp = end;
		}
		switch (*cp) {
		case '/':
			if (*(cp + 1) != '\0')
				return 1;
			break;
		case '\0':
			break;
		default:
			return 1;
		}
		break;
	case '/':
		if (*(cp+1) != '\0')
			return 1;
		break;
	case '\0':
		break;
	default:
		return 1;
	}
	free(proxy->host);
	proxy->host = myStrndup(host, len);
	proxy->port = port;
	return 0;
}

static void
toLowerString(char *s)
{
	for (; *s; ++s)
		*s = (char)tolower((int)*s);
}

/*
 * Password encrpytion/decryption.  xor with pseudo-random one-time pad,
 * so password isn't (usually) obvious in memory dump.
 */
static char *passwordPad = NULL;
static size_t passwordLen = 0;
static int needSeed = 1;

static void
seedPasswordRandom(void)
{
	if (needSeed) {
#if defined(WIN32)
		srand(time(0));
#else
		srandom((unsigned int)(getpid() * time(0)));
#endif
		needSeed = 0;
	}
}

/* create a malloc'ed string filled with '*' (to cover username/password
 * in logs)
 */
char *
stars(size_t len)
{
	char *s = (char *)myMalloc(len + 1);

	memset(s, '*', len);
	s[len] = '\0';
	return s;
}

void
setUsername(char *username)
{
	toLowerString(username);
	free(options.username);
	options.username = username;
}

void
setPassword(char *password)
{
	int i;

	seedPasswordRandom();
	free(passwordPad);
	passwordLen = strlen(password) + 1;
	passwordPad = (char *)myMalloc(passwordLen);
	for (i = 0; i < (int)passwordLen; ++i)
#if defined(WIN32)
		passwordPad[i] = (char)rand();
#else
		passwordPad[i] = (char)random();
#endif
	toLowerString(password);
	cryptPassword(password);
	free(options.password);
	options.password = password;
}

static void
cryptPassword(char *password)
{
	int i;

	for (i = 0; i < (int)passwordLen; ++i)
		password[i] ^= passwordPad[i];
}

char *
getPassword()
{
	char *password = (char *)myMalloc(passwordLen);

	memcpy(password, options.password, passwordLen);
	cryptPassword(password);
	return password;
}

void
freePassword(char *password)
{
	memset(password, '\0', passwordLen);
	free(password);
}

/*
 * Cygwin doesn't provide basename and dirname?
 *
 * Windows-specific code wrapped in ifdefs below, just in case basename
 * or dirname is needed on a non-windows system.
 */
#if defined(__CYGWIN__) || defined(WIN32)
char *
basename(char *name)
{
	int len;
	char *cp;

        if (!name) return name;

	len = strlen(name);
	if (len == 0)
		return (char *)".";	/* cast away const */
	cp = name + len - 1;
#if defined(__CYGWIN__) || defined(WIN32)
	if (*cp == '/' || *cp == '\\') {
		for (; cp >= name && (*cp == '/' || *cp == '\\'); --cp)
#else
	if (*cp == '/') {
		for (; cp >= name && *cp == '/'; --cp)
#endif
			*cp = '\0';
		if (cp < name)
			return (char *)"/";	/* cast away const */
	}
#if defined(__CYGWIN__) || defined(WIN32)
	for (; cp >= name && *cp != '/' && *cp != '\\'; --cp)
#else
	for (; cp >= name && *cp != '/'; --cp)
#endif
		;
	return cp + 1;
}

char *
dirname(char *name)
{
	int len;
	char *cp;

	if (!name) return name;

	len = strlen(name);
	if (len == 0)
		return (char *)".";	/* cast away const */
	cp = name + len - 1;
#if defined(__CYGWIN__) || defined(WIN32)
	if (*cp == '/' || *cp == '\\') {
		for (; cp >= name && (*cp == '/' || *cp == '\\'); --cp)
#else
	if (*cp == '/') {
		for (; cp >= name && *cp == '/'; --cp)
#endif
			*cp = '\0';
		if (cp <= name)
			return (char *)"/";	/* cast away const */
	}
#if defined(__CYGWIN__) || defined(WIN32)
	for (; cp >= name && *cp != '/' && *cp != '\\'; --cp)
#else
	for (; cp >= name && *cp != '/'; --cp)
#endif
		;
	if (cp < name)
		return (char *)".";	/* cast away const */
	if (cp == name)
		return (char *)"/";	/* cast away const */
	*cp = '\0';
	return name;
}
#endif
