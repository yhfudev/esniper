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

static memBuf_t *httpRequest(auctionInfo *, const char *url, const char *data, const char *logData, enum requestType);
static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data);

/* returns open socket, or NULL on error */
memBuf_t *
httpGet(auctionInfo *aip, const char *url)
{
	return httpRequest(aip, url, "", NULL, GET);
}

/* returns open socket, or NULL on error */
memBuf_t *
httpPost(auctionInfo *aip, const char *url, const char *data, const char *logData)
{
	return httpRequest(aip, url, data, logData, POST);
}

static const char UNAVAILABLE[] = "unavailable/";

static CURL *easyhandle=NULL;
char globalErrorbuf[CURL_ERROR_SIZE];

static int curlInitDone=0;

static memBuf_t *
httpRequest(auctionInfo *aip, const char *url, const char *data, const char *logData, enum requestType rt)
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

   log((url));
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

void resetCurlStuff(void)
{
   curl_easy_reset(easyhandle);
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
