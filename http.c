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

#include "http.h"
#include "esniper.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#if defined(WIN32)
#	include <winsock.h>
#	include <io.h>
#	include <fcntl.h>
#	define sleep(t) _sleep((t) * 1000)
#	define strcasecmp(s1, s2) stricmp((s1), (s2))
#	define strncasecmp(s1, s2, n) strnicmp((s1), (s2), (n))
#else
#	include <signal.h>
#	include <unistd.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	if defined(_XOPEN_SOURCE_EXTENDED)
#		include <arpa/inet.h>
#	endif
#	if defined(__aix)
#		include <strings.h>	/* AIX 4.2 strcasecmp() */
#	endif
#endif

enum requestType {GET, POST};

static FILE *verboseConnect(proxy_t *proxy, const char *host, unsigned int retryTime, int retryCount);
static int getRedirect(FILE *fp, char **host, char **query, char **cookie);
static int getStatus(FILE *fp);
static FILE *httpRequest(auctionInfo *, const char *host, const char *url, const char *cookies, const char *data, const char *logData, int saveRedirect, enum requestType);
static FILE *checkStatus(auctionInfo *aip, FILE *fp, const char *cookies, const char *data, const char *logData, int saveRedirect, enum requestType rt);
static int closeFdSocket(int fd);

#if defined(WIN32)
typedef struct fdToSock {
	int fd;
	SOCKET sock;
	struct fdToSock *next;
} fdToSock;
static fdToSock *fdToSockHead = NULL;

static void storeSocket(int fd, SOCKET sock);
static SOCKET getSocket(int fd);
static int isNT();
static int openOsfhandleHack(SOCKET sock, int flags);
static const char *winsockStrerror(int err);
#endif

/*
 * Open a connection to the host.  Return valid FILE * if successful, NULL
 * otherwise
 */
static FILE *
verboseConnect(proxy_t *proxy, const char *host, unsigned int retryTime, int retryCount)
{
	int sockfd = -1, rc = -1, count;
	struct sockaddr_in servAddr;
	struct hostent *entry = NULL;
	static int firstTime = 1;
	const char *connectHost;
	int connectPort;
#if defined(WIN32)
	SOCKET winSocket;
#else
	static struct sigaction alarmAction;
	int saveErrno;
#endif

	/* use proxy? */
	if (proxy->host) {
		connectHost = proxy->host;
		connectPort = proxy->port;
	} else {
		connectHost = host;
		connectPort = 80;
	}

	if (firstTime) {
#if defined(WIN32)
		WSADATA wsaData;
		int sockopt = SO_SYNCHRONOUS_NONALERT;

		if (WSAStartup(MAKEWORD(1, 1), &wsaData) != NO_ERROR) {
			printLog(stderr, "Error %d during WSAStartup: %s\n",
				 WSAGetLastError(),
				 winsockStrerror(WSAGetLastError()));
			exit(1);
		}
		if (setsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE,
				(char *)&sockopt,sizeof(sockopt)) < 0) {
			printLog(stderr, "Error %d during setsockopt: %s\n",
				 errno, strerror(errno));
			exit(1);
		}
#else
		sigaction(SIGALRM, NULL, &alarmAction);
#endif
		firstTime = 0;
	}
	for (count = 0; count < 10; count++) {
		if (!(entry = gethostbyname(connectHost))) {
			if (options.debug) {
#if defined(WIN32)
				int err = WSAGetLastError();
				const char *errmsg = winsockStrerror(err);
#else
				int err = h_errno;
				const char *errmsg = strerror(h_errno);
#endif
				log(("gethostbyname errno %d: %s\n",
				     err, errmsg));
			}
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

	log(("Creating socket"));

#if defined(WIN32)
	winSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (winSocket == INVALID_SOCKET) {
		printLog(stderr, "Socket error %d: %s\n", WSAGetLastError(),
			 winsockStrerror(WSAGetLastError()));
		return NULL;
	}
	/*
	 * Note: _open_osfhandle of a socket doesn't work in Windows
	 *	95 and 98 (and probably not Me).  It does work in NT,
	 *	2000, and XP.  If you want to use esniper and you have
	 *	95/98/Me, you must use the cygwin version.
	 *
	 * I'm not sure if _O_RDWR is needed.
	 */
	sockfd = openOsfhandleHack(winSocket, _O_RDWR | _O_APPEND);
	storeSocket(sockfd, winSocket);
#else
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
#endif
	if (sockfd == -1) {
		printLog(stderr, "Socket error %d: %s\n", errno,
			strerror(errno));
		return NULL;
	}

	log(("Connecting to %s:%d", connectHost, connectPort));
	while (retryCount-- > 0) {
#if defined(WIN32)
		/*
		 * Use default connect timeout (about a minute...).  Can't
		 * use SIGALRM on Win32 because it doesn't exist.  There are
		 * a few workarounds, all involving doing the connect in its
		 * own thread and setting a timer.  If the timer goes off
		 * before the connect completes, kill the connect thread.
		 * You can use either SetTimer() or CreateWaitableTimers().
		 * See http://tangentsoft.net/wskfaq/newbie.html#timeout
		 * for a more complete description.
		 */
		rc = connect(winSocket, (struct sockaddr *)&servAddr, (int)sizeof(struct sockaddr_in));
#else
		alarmAction.sa_flags &= ~SA_RESTART;
		sigaction(SIGALRM, &alarmAction, NULL);
		alarm(retryTime);
		rc = connect(sockfd, (struct sockaddr *)&servAddr, (size_t)sizeof(struct sockaddr_in));
		saveErrno = errno;
		alarm(0);
		alarmAction.sa_flags |= SA_RESTART;
		sigaction(SIGALRM, &alarmAction, NULL);
#endif
		if (!rc)
			break;
#if defined(WIN32)
		log(("Connect errno %d: %s", WSAGetLastError(),
		     winsockStrerror(WSAGetLastError())));
#else
		log(("Connect errno %d: %s", saveErrno, strerror(saveErrno)));
#endif
	}
	if (!rc) {
		log((" OK "));
		return fdopen(sockfd, "a+");
	}
	closeFdSocket(sockfd);
	return NULL;
}

int
closeSocket(FILE *fp)
{
	int ret;

	runout(fp);
	ret = closeFdSocket(fileno(fp));
	fclose(fp);
	return ret;
}

static int
closeFdSocket(int fd)
{
	char buf[1024];
	int ret;

#if defined(WIN32)
	SOCKET sock = getSocket(fd);

	if (sock != INVALID_SOCKET) {
		shutdown(sock, 1);
		while ((ret = recv(sock, buf, 1024, 0)) != 0) {
			if (ret == -1 && WSAGetLastError() != WSAEINTR)
				break;
		}
		while ((ret = closesocket(sock)) == -1) {
			if (WSAGetLastError() != WSAEINTR)
				break;
		}
	}
	while ((ret = _close(fd)) == -1) {
		if (errno != EINTR)
			break;
	}
#else
	shutdown(fd, 1);
	while ((ret = recv(fd, buf, 1024, 0)) != 0) {
		if (ret == -1 && errno != EINTR)
			break;
	}
	while ((ret = close(fd)) == -1) {
		if (errno != EINTR)
			break;
	}
#endif
	return ret;
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
		closeSocket(fp);
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
		closeSocket(fp);
		ret = NULL;
	    }
	}
	return ret;
}

#if defined(WIN32)
static void
storeSocket(int fd, SOCKET sock)
{
	struct fdToSock *fs = (fdToSock *)myMalloc(sizeof(fdToSock));

	fs->fd = fd;
	fs->sock = sock;
	fs->next = fdToSockHead;
	fdToSockHead = fs;
}

static SOCKET
getSocket(int fd)
{
	fdToSock *fs, *prev = NULL;
	SOCKET ret = INVALID_SOCKET;

	for (fs = fdToSockHead; fs; prev = fs, fs = fs->next) {
		if (fs->fd == fd) {
			if (prev)
				prev->next = fs->next;
			else
				fdToSockHead = fs->next;
			ret = fs->sock;
			free(fs);
			break;
		}
	}
	return ret;
}

/*
 * Return 1 is Windows OS is NT, 0 otherwise (95, etc)
 */
static int
isNT()
{
	static int firstTime = 1;
	static int ret = 0;

	if (firstTime) {
		OSVERSIONINFO info;

		info.dwOSVersionInfoSize = sizeof(info);
		if (GetVersionEx(&info)) {
			if (info.dwPlatformId == VER_PLATFORM_WIN32_NT)
				ret = 1;
		}
		firstTime = 0;
	}
	return ret;
}

/*
 * TODO: Hack to work around _open_osfhandle deficiency for Win 95/98/Me
 */
static int
openOsfhandleHack(SOCKET sock, int flags)
{
	if (isNT())
		return _open_osfhandle(sock, flags);

	printLog(stderr, "This program only works with Windows NT, 2000, and XP\n");
	exit(1);
}

static const char *
winsockStrerror(int err)
{
	switch (err) {
	case 0: /* fall through */
	case WSABASEERR: return "No Error";
	case WSAEINTR: return "Interrupted system call";
	case WSAEBADF: return "Bad file number";
	case WSAEACCES: return "Permission denied";
	case WSAEFAULT: return "Bad address";
	case WSAEINVAL: return "Invalid argument";
	case WSAEMFILE: return "Too many open files";
	case WSAEWOULDBLOCK: return "Operation would block";
	case WSAEINPROGRESS: return "Operation now in progress";
	case WSAEALREADY: return "Operation already in progres";
	case WSAENOTSOCK: return "Socket operation on non-socket";
	case WSAEDESTADDRREQ: return "Destination address required";
	case WSAEMSGSIZE: return "Message too long";
	case WSAEPROTOTYPE: return "Protocol wrong type for socket";
	case WSAENOPROTOOPT: return "Bad protocol option";
	case WSAEPROTONOSUPPORT: return "Protocol not supported";
	case WSAESOCKTNOSUPPORT: return "Socket type not supported";
	case WSAEOPNOTSUPP: return "Operation not supported on socket";
	case WSAEPFNOSUPPORT: return "Protocol family not supported";
	case WSAEAFNOSUPPORT: return "Address family not supported by protocol family";
	case WSAEADDRINUSE: return "Address already in use";
	case WSAEADDRNOTAVAIL: return "Can't assign requested address";
	case WSAENETDOWN: return "Network is down";
	case WSAENETUNREACH: return "Network is unreachable";
	case WSAENETRESET: return "Net dropped connection or reset";
	case WSAECONNABORTED: return "Software caused connection abort";
	case WSAECONNRESET: return "Connection reset by peer";
	case WSAENOBUFS: return "No buffer space available";
	case WSAEISCONN: return "Socket is already connected";
	case WSAENOTCONN: return "Socket is not connected";
	case WSAESHUTDOWN: return "Can't send after socket shutdown";
	case WSAETOOMANYREFS: return "Too many references, can't splice";
	case WSAETIMEDOUT: return "Connection timed out";
	case WSAECONNREFUSED: return "Connection refused";
	case WSAELOOP: return "Too many levels of symbolic links";
	case WSAENAMETOOLONG: return "File name too long";
	case WSAEHOSTDOWN: return "Host is down";
	case WSAEHOSTUNREACH: return "No Route to Host";
	case WSAENOTEMPTY: return "Directory not empty";
	case WSAEPROCLIM: return "Too many processes";
	case WSAEUSERS: return "Too many users";
	case WSAEDQUOT: return "Disc Quota Exceeded";
	case WSAESTALE: return "Stale NFS file handle";
	case WSAEREMOTE: return "Too many levels of remote in path";
	case WSASYSNOTREADY: return "Network SubSystem is unavailable";
	case WSAVERNOTSUPPORTED: return "WINSOCK DLL Version out of range";
	case WSANOTINITIALISED: return "Successful WSASTARTUP not yet performed";
	case WSAHOST_NOT_FOUND: return "Host not found";
	case WSATRY_AGAIN: return "Non-Authoritative Host not found";
	case WSANO_RECOVERY: return "Non-Recoverable errors: FORMERR, REFUSED, NOTIMP";
	case WSANO_DATA: return "Valid name, no data record of requested type";
	}
	return "Unknown error";
}
#endif
