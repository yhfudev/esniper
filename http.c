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

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>
#include "esniper.h"

enum requestType {GET, POST};

static memBuf_t *httpRequest(auctionInfo *, const char *url, const char *logUrl, const char *data, const char *logData, enum requestType);
static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data);

#ifdef NEED_CURL_EASY_STRERROR
static const char *curl_easy_strerror(CURLcode error);
#endif

/* returns open socket, or NULL on error */
memBuf_t *
httpGet(auctionInfo *aip, const char *url, const char *logUrl)
{
	return httpRequest(aip, url, logUrl, "", NULL, GET);
}

/* returns open socket, or NULL on error */
memBuf_t *
httpPost(auctionInfo *aip, const char *url, const char *data, const char *logData)
{
	return httpRequest(aip, url, NULL, data, logData, POST);
}

static const char UNAVAILABLE[] = "unavailable/";

static CURL *easyhandle=NULL;
char globalErrorbuf[CURL_ERROR_SIZE];

static int curlInitDone=0;

static memBuf_t *
httpRequest(auctionInfo *aip, const char *url, const char *logUrl, const char *data, const char *logData, enum requestType rt)
{
   const char *nonNullData = data ? data : "";

   static memBuf_t membuf = { NULL, 0, NULL, 0 };

   CURLcode curlrc;

   if(!curlInitDone)
   {
      if(initCurlStuff())
      {
         return NULL;
      }
   }

   if(membuf.memory)
   {
      clearMembuf(&membuf);
   }

   curlrc=curl_easy_setopt(easyhandle, CURLOPT_WRITEDATA, (void *)&membuf);
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      log((globalErrorbuf));
      return NULL;
   }

   if(rt == GET)
   {
      curlrc=curl_easy_setopt(easyhandle, CURLOPT_HTTPGET, 1);
      if(curlrc)
      {
         log((curl_easy_strerror(curlrc)));
         log((globalErrorbuf));
         return NULL;
      }
   }
   else
   {
      log((logData ? logData : nonNullData));
      curlrc= curl_easy_setopt(easyhandle, CURLOPT_POSTFIELDS, nonNullData);
      if(curlrc)
      {
         log((curl_easy_strerror(curlrc)));
         log((globalErrorbuf));
         return NULL;
      }
   }

   log((logUrl ? logUrl : url));
   curlrc=curl_easy_setopt(easyhandle, CURLOPT_URL, url);
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      log((globalErrorbuf));
      return NULL;
   }

   curlrc=curl_easy_perform(easyhandle);
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      log((globalErrorbuf));
      return NULL;
   }

   return &membuf;
}

void
clearMembuf(memBuf_t *mp)
{
	free(mp->memory);
	mp->memory=NULL;
	mp->size=0;
	mp->readptr=NULL;
	mp->timeToFirstByte=0;
}

int initCurlStuff(void)
{
   /* list for custom headers */
   struct curl_slist *slist=NULL;
   CURLcode curlrc;

   curl_global_init(CURL_GLOBAL_ALL);

   /* init the curl session */
   easyhandle = curl_easy_init();

   if(!easyhandle) return -1;

   /* buffer for error messages */
   curlrc=curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, globalErrorbuf);
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      return -1;
   }

   if(options.curldebug)
   {
      /* debug output, show what libcurl does */
      curl_easy_setopt(easyhandle, CURLOPT_VERBOSE, 1);
      if(curlrc)
      {
         log((curl_easy_strerror(curlrc)));
         log((globalErrorbuf));
         return -1;
      }
   }

   /* follow all redirects */
   curl_easy_setopt(easyhandle, CURLOPT_FOLLOWLOCATION, 1);
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      log((globalErrorbuf));
      return -1;
   }

   /* send all data to this function  */
   curl_easy_setopt(easyhandle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      log((globalErrorbuf));
      return -1;
   }

   /* some servers don't like requests that are made without a user-agent
      field, so we provide one */
   curlrc=curl_easy_setopt(easyhandle, CURLOPT_USERAGENT, 
                    "Mozilla/4.7 [en] (X11; U; Linux 2.2.12 i686)");
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      log((globalErrorbuf));
      return -1;
   }

   slist = curl_slist_append(slist, "Accept: text/*");
   slist = curl_slist_append(slist, "Accept-Language: en");
   slist = curl_slist_append(slist, "Accept-Charset: iso-8859-1,*,utf-8");
   slist = curl_slist_append(slist, "Cache-Control: no-cache");
   curlrc=curl_easy_setopt(easyhandle, CURLOPT_HTTPHEADER, slist);
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      log((globalErrorbuf));
      return -1;
   }

   curlrc=curl_easy_setopt(easyhandle, CURLOPT_COOKIEFILE, "/dev/null");
   if(curlrc)
   {
      log((curl_easy_strerror(curlrc)));
      log((globalErrorbuf));
      return -1;
   }

   curlInitDone=1;

   return 0;
}

void cleanupCurlStuff(void)
{
   curl_easy_cleanup(easyhandle);
   easyhandle=NULL;
   curlInitDone=0;
}

static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
   register size_t realsize = size * nmemb;
   memBuf_t *mp = (memBuf_t *)data;

   if(!mp->timeToFirstByte)
   {
      mp->timeToFirstByte = time(NULL);
   }
    
   mp->memory = (char *)realloc(mp->memory, mp->size + realsize + 1);
   if (mp->memory) {
      mp->readptr=mp->memory;
      memcpy(&(mp->memory[mp->size]), ptr, realsize);
      mp->size += realsize;
      mp->memory[mp->size] = 0;
   }
   return realsize;
}

int memEof(memBuf_t *mp)
{
	return (!mp || !mp->size || mp->readptr == (mp->memory + mp->size));
}

int memGetc(memBuf_t *mp)
{
	return memEof(mp) ? EOF : (int)(*(unsigned char *)(mp->readptr++));
}

void memUngetc(int c, memBuf_t *mp)
{
	if (mp->readptr > mp->memory)
		mp->readptr--;
}

time_t getTimeToFirstByte(memBuf_t *mp)
{
	return mp->timeToFirstByte;
}

#ifdef NEED_CURL_EASY_STRERROR
/*
 * This is only needed if we are using libcurl that doesn't have its own
 * curl_easy_strerror.  curl_easy_strerror was added in 7.11.2.
 */
static const char *curlErrorTable[] = {
	"no error", /* CURLE_OK */
	"unsupported protocol", /* CURLE_UNSUPPORTED_PROTOCOL */
	"failed init", /* CURLE_FAILED_INIT */
	"URL using bad/illegal format or missing URL", /* CURLE_URL_MALFORMAT */
	"CURLE_URL_MALFORMAT_USER", /* CURLE_URL_MALFORMAT_USER */
	"couldnt resolve proxy name", /* CURLE_COULDNT_RESOLVE_PROXY */ 
	"couldnt resolve host name", /* CURLE_COULDNT_RESOLVE_HOST */ 
	"couldn't connect to server", /* CURLE_COULDNT_CONNECT */ 
	"FTP: weird server reply", /* CURLE_FTP_WEIRD_SERVER_REPLY */ 
	"FTP: access denied", /* CURLE_FTP_ACCESS_DENIED */
	"FTP: user and/or password incorrect", /* CURLE_FTP_USER_PASSWORD_INCORRECT */
	"FTP: unknown PASS reply", /* CURLE_FTP_WEIRD_PASS_REPLY */
	"FTP: unknown USER reply", /* CURLE_FTP_WEIRD_USER_REPLY */
	"FTP: unknown PASV reply", /* CURLE_FTP_WEIRD_PASV_REPLY */
	"FTP: unknown 227 response format", /* CURLE_FTP_WEIRD_227_FORMAT */
	"FTP: can't figure out the host in the PASV response", /* CURLE_FTP_CANT_GET_HOST */
	"FTP: can't connect to server the response code is unknown", /* CURLE_FTP_CANT_RECONNECT */
	"FTP: couldn't set binary mode", /* CURLE_FTP_COULDNT_SET_BINARY */
	"Transferred a partial file", /* CURLE_PARTIAL_FILE */
	"FTP: couldn't retrieve (RETR failed) the specified file", /* CURLE_FTP_COULDNT_RETR_FILE */
	"FTP: the post-transfer acknowledge response was not OK", /* CURLE_FTP_WRITE_ERROR */
	"FTP: a quote command returned error", /* CURLE_FTP_QUOTE_ERROR */
	"HTTP response code said error", /* CURLE_HTTP_RETURNED_ERROR */
	"failed writing received data to disk/application", /* CURLE_WRITE_ERROR */
	"CURLE_MALFORMAT_USER", /* CURLE_MALFORMAT_USER */
	"failed FTP upload (the STOR command)", /* CURLE_FTP_COULDNT_STOR_FILE */
	"failed to open/read local data from file/application", /* CURLE_READ_ERROR */
	"out of memory", /* CURLE_OUT_OF_MEMORY */
	"a timeout was reached", /* CURLE_OPERATION_TIMEOUTED */
	"FTP could not set ASCII mode (TYPE A)", /* CURLE_FTP_COULDNT_SET_ASCII */
	"FTP command PORT failed", /* CURLE_FTP_PORT_FAILED */
	"FTP command REST failed", /* CURLE_FTP_COULDNT_USE_REST */
	"FTP command SIZE failed", /* CURLE_FTP_COULDNT_GET_SIZE */
	"a range was requested but the server did not deliver it", /* CURLE_HTTP_RANGE_ERROR */
	"internal problem setting up the POST", /* CURLE_HTTP_POST_ERROR */
	"SSL connect error", /* CURLE_SSL_CONNECT_ERROR */
	"couldn't resume FTP download", /* CURLE_FTP_BAD_DOWNLOAD_RESUME */
	"couldn't read a file:// file", /* CURLE_FILE_COULDNT_READ_FILE */
	"LDAP: cannot bind", /* CURLE_LDAP_CANNOT_BIND */
	"LDAP: search failed", /* CURLE_LDAP_SEARCH_FAILED */
	"a required shared library was not found", /* CURLE_LIBRARY_NOT_FOUND */
	"a required function in the shared library was not found", /* CURLE_FUNCTION_NOT_FOUND */
	"the operation was aborted by an application callback", /* CURLE_ABORTED_BY_CALLBACK */
	"a libcurl function was given a bad argument", /* CURLE_BAD_FUNCTION_ARGUMENT */
	"CURLE_BAD_CALLING_ORDER", /* CURLE_BAD_CALLING_ORDER */
	"failed binding local connection end", /* CURLE_INTERFACE_FAILED */
	"CURLE_BAD_PASSWORD_ENTERED", /* CURLE_BAD_PASSWORD_ENTERED */
	"number of redirects hit maximum amount", /* CURLE_TOO_MANY_REDIRECTS  */
	"User specified an unknown option", /* CURLE_UNKNOWN_TELNET_OPTION */
	"Malformed telnet option", /* CURLE_TELNET_OPTION_SYNTAX  */
	"CURLE_OBSOLETE", /* CURLE_OBSOLETE */
	"SSL peer certificate was not ok", /* CURLE_SSL_PEER_CERTIFICATE */
	"server returned nothing (no headers, no data)", /* CURLE_GOT_NOTHING */
	"SSL crypto engine not found", /* CURLE_SSL_ENGINE_NOTFOUND */
	"can not set SSL crypto engine as default", /* CURLE_SSL_ENGINE_SETFAILED */
	"failed sending data to the peer", /* CURLE_SEND_ERROR */
	"failure when receiving data from the peer", /* CURLE_RECV_ERROR */
	"share is already in use", /* CURLE_SHARE_IN_USE */
	"problem with the local SSL certificate", /* CURLE_SSL_CERTPROBLEM */
	"couldn't use specified SSL cipher", /* CURLE_SSL_CIPHER */
	"problem with the SSL CA cert (path? access rights?)", /* CURLE_SSL_CACERT */
	"Unrecognized HTTP Content-Encoding", /* CURLE_BAD_CONTENT_ENCODING */
	"Invalid LDAP URL", /* CURLE_LDAP_INVALID_URL */
	"Maximum file size exceeded", /* CURLE_FILESIZE_EXCEEDED */
	"Requested FTP SSL level failed", /* CURLE_FTP_SSL_FAILED */
};

static const char *
curl_easy_strerror(CURLcode error)
{
	if (error < 0 || error >= CURL_LAST ||
	    error >= (sizeof(curlErrorTable) / sizeof(const char *)))

		return "unknown error";
	return curlErrorTable[error];
}
#endif
