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

#include "http.h"
#include "esniper.h"
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

enum requestType {GET, POST};

static FILE *verboseConnect(proxy_t *proxy, const char *host, unsigned int retryTime, int retryCount);
static int getRedirect(FILE *fp, char **host, char **query, char **cookie);
static int getStatus(FILE *fp);
static FILE *httpRequest(auctionInfo *, const char *host, const char *url, const char *cookies, const char *data, const char *logData, int saveRedirect, enum requestType);
static FILE *checkStatus(auctionInfo *aip, FILE *fp, const char *cookies, const char *data, const char *logData, int saveRedirect, enum requestType rt);

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

/* get redirection info.  Return 0 on success */
static int
getRedirect(FILE *fp, char **host, char **query, char **cookie)
{
	char *line;
	static char LOCATION[] = "Location: http://";
	static char SETCOOKIE[] = "Set-Cookie: ";

	log(("Redirect..."));
	*host = NULL;
	*query = NULL;
	*cookie = NULL;
	for (line = getLine(fp); line && *line != '\0'; line = getLine(fp)) {
		if (!strncasecmp(line, LOCATION, sizeof(LOCATION) - 1)) {
			char *slash, *tmp;

			tmp = line + sizeof(LOCATION) - 1;
			slash = strchr(tmp, '/');
			if (!slash) {
				log(("Badly formatted Location header: %s", line));
				continue;
			}
			*slash = '\0';
			free(*host);
			free(*query);
			*host = myStrdup(tmp);
			*query = myStrdup(slash + 1);
		} else if (!strncasecmp(line, SETCOOKIE, sizeof(SETCOOKIE)-1)) {
			char *version = NULL, *namevalue = NULL, *path = NULL,
			     *domain = NULL, *tmp, *cp;

			log(("getRedirect(): found cookie\n"));
			cp = line + sizeof(SETCOOKIE) - 1;
			for (;;) {
				char *name, *value = NULL;

				for (; isspace((int)*cp); ++cp)
					;
				if (!*cp)
					break;
				name = cp;
				for (; *cp != ';' && *cp != '=' && !isspace((int)*cp) && *cp; ++cp)
					;
				while (isspace((int)*cp))
					*cp++ = '\0';
				if (*cp == '=') {
					int inString = 0;

					*cp++ = '\0';
					for (; isspace((int)*cp); ++cp)
						;
					value = cp;
					for ( ; *cp ; ++cp) {
						if (!inString && *cp == ';') {
							*cp++ = '\0';
							break;
						}
						if (*cp == '"')
							inString = !inString;
					}
				}
				if (!strcasecmp(name, "Comment")) {
					/* ignore */
				} else if (!strcasecmp(name, "Domain")) {
					free(domain);
					domain = myStrdup(value);
					log(("getRedirect(): domain is %s\n", domain));
				} else if (!strcasecmp(name, "Expires")) {
					/* ignore */
				} else if (!strcasecmp(name, "Max-Age")) {
					/* ignore */
				} else if (!strcasecmp(name, "Path")) {
					free(path);
					path = myStrdup(value);
					log(("getRedirect(): path is %s\n", path));
				} else if (!strcasecmp(name, "Secure")) {
					/* ignore */
				} else if (!strcasecmp(name, "Version")) {
					free(version);
					version = myStrdup(value);
					log(("getRedirect(): version is %s\n", domain));
				} else {
					free(namevalue);
					namevalue = myStrdup3(name, "=",
							value ? value : "");
					log(("getRedirect(): namevalue is %s\n", namevalue));
				}
			}

			log(("getRedirect(): out of loop, namevalue is %s\n", namevalue));
			if (!namevalue) {
				free(path);
				free(domain);
				continue;
			}

			/* cookie header */
			if (!*cookie)
				*cookie = myStrdup2("Cookie: $Version=",
						    version ? version : "1");

			log(("getRedirect(): out of loop, *cookie is %s\n", nullStr(*cookie)));

			/* tack on name=value */
			tmp = *cookie;
			*cookie = myStrdup3(*cookie, "; ", namevalue);
			free(tmp);
			free(namevalue);

			/* and path? */
			if (path) {
				tmp = *cookie;
				*cookie = myStrdup3(*cookie, "; $Path=", path);
				free(tmp);
				free(path);
			}

			/* and domain? */
			if (domain) {
				tmp = *cookie;
				*cookie = myStrdup3(*cookie, "; $Domain=", domain);
				free(tmp);
				free(domain);
			}
			log(("getRedirect(): finished cookie, *cookie is %s\n", nullStr(*cookie)));
		}
	}
	log(("redirect host: %s, query: %s, cookie: %s", nullStr(*host), nullStr(*query), nullStr(*cookie)));
	if (!*host || !*query) {
		log(("Cannot find Location: header for redirect"));
		return 1;
	}
	return 0;
}

/* Get status from HTTP response */
static int
getStatus(FILE *fp)
{
	char *line = getLine(fp);
	char *version = line, *status, *desc;
	int ret;

	if (!version) {
		log(("getStatus(): Server closed connection without response, error is %s", strerror(errno)));
		return 0;
	}
	status = version + strcspn(version, " \t");
	while (*status == ' ' || *status == '\t')
		*status++ = '\0';
	desc = status + strcspn(status, " \t");
	while (*desc == ' ' || *desc == '\t')
		*desc++ = '\0';
	ret = atoi(status);
	log(("Response from server: %s %d %s\n", version, ret, desc));
	return ret;
}

/* returns open socket, or NULL on error */
FILE *
httpGet(auctionInfo *aip, const char *host, const char *url, const char *cookies, int saveRedirect)
{
	return httpRequest(aip, host, url, cookies, "", NULL, saveRedirect, GET);
}

/* returns open socket, or NULL on error */
FILE *
httpPost(auctionInfo *aip, const char *host, const char *url, const char *cookies, const char *data, const char *logData, int saveRedirect)
{
	return httpRequest(aip, host, url, cookies, data, logData, saveRedirect, POST);
}

static const char STANDARD_HEADERS[] =
	"User-Agent: Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)\r\n"
	"Host: %s\r\n"
	"Accept: text/*\r\n"
	"Accept-Language: en\r\n"
	"Accept-Charset: iso-8859-1,*,utf-8\r\n"
	"Pragma: no-cache\r\n"
	"Cache-Control: no-cache\r\n";
static const char GET_FMT[] = "GET %s%s/%s HTTP/1.0\r\n";
static const char GET_TRAILER[] = "\r\n";
static const char POST_FMT[] = "POST %s%s/%s HTTP/1.0\r\n";
static const char POST_TRAILER[] =
	"Content-type: application/x-www-form-urlencoded\r\n"
	"Content-length: %d\r\n"
	"\r\n"
	"%s\r\n";
static const char UNAVAILABLE[] = "unavailable/";

static FILE *
httpRequest(auctionInfo *aip, const char *host, const char *url, const char *cookies, const char *data, const char *logData, int saveRedirect, enum requestType rt)
{
	FILE *fp, *ret = NULL;
	const char *reqHttp = options.proxy.host ? "http://" : "";
	const char *reqHost = options.proxy.host ? host : "";
	const char *reqFmt = rt == GET ? GET_FMT : POST_FMT;
	const char *trailer = rt == GET ? GET_TRAILER : POST_TRAILER;
	const char *nonNullData = data ? data : "";

	if (!(fp = verboseConnect(&options.proxy, host, 10, 5))) {
		auctionError(aip, ae_connect, NULL);
		return NULL;
	}

	printLog(fp, reqFmt, reqHttp, reqHost, url);
	printLog(fp, STANDARD_HEADERS, host);
	if (cookies && *cookies)
		printLog(fp, "%s\r\n", cookies);
	fprintf(fp, trailer, strlen(nonNullData), nonNullData);
	log((trailer, strlen(nonNullData), logData ? logData : nonNullData));
	fflush(fp);

	ret = checkStatus(aip, fp, cookies, data, logData, saveRedirect, rt);
	return ret;
}

static FILE *
checkStatus(auctionInfo *aip, FILE *fp, const char *cookies, const char *data, const char *logData, int saveRedirect, enum requestType rt)
{
	FILE *ret = NULL;
	int status = getStatus(fp);

	switch (status) {
	case 200: /* OK */
		log(("checkStatus(): OK!\n"));
		ret = fp;
		break;
	case 301: /* redirect to requestType */
	case 302: /* redirect to a GET */
	case 303: /* redirect to a GET */
	case 307: /* redirect to requestType */
	    {
		char *newHost, *newQuery, *newCookies;
		int redirectStatus;

		log(("checkStatus(): Redirect! %d\n", status));
		redirectStatus = getRedirect(fp, &newHost, &newQuery, &newCookies);
		runout(fp);
		fclose(fp);
		if (redirectStatus)
			auctionError(aip, ae_badredirect, NULL);
		else {
			if (saveRedirect) {
				free(aip->host);
				aip->host = myStrdup(newHost);
				free(aip->query);
				aip->query = myStrdup(newQuery);
			}
			if (!strncmp(newQuery, UNAVAILABLE, sizeof(UNAVAILABLE) - 1)) {
				log(("Unavailable!"));
				auctionError(aip, ae_unavailable, NULL);
			} else if (status == 301 || status == 307) {
				ret = httpRequest(aip, newHost, newQuery, newCookies ? newCookies : cookies, data, logData, saveRedirect, rt);
			} else {
				/* force a GET for 302, 303 */
				ret = httpRequest(aip, newHost, newQuery, newCookies ? newCookies : cookies, "", NULL, saveRedirect, GET);
			}
		}
		free(newHost);
		free(newQuery);
		free(newCookies);
		break;
	    }
	default:
	    {
		/* Unexpected status */
		char badStat[12];

		log(("checkStatus(): Bad status! %d\n", status));
		sprintf(badStat, "%d", status);
		auctionError(aip, ae_badstatus, myStrdup(badStat));
		runout(fp);
		fclose(fp);
		ret = NULL;
	    }
	}
	return ret;
}
