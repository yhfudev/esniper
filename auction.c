/*
 * Copyright (c) 2002, 2003, 2004, Scott Nicol <esniper@users.sf.net>
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
#include "http.h"
#include "html.h"
#include "history.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#if defined(WIN32)
#	define strcasecmp(s1, s2) stricmp((s1), (s2))
#	define sleep(t) _sleep((t) * 1000)
#	define strncasecmp(s1, s2, len) strnicmp((s1), (s2), (len))
#else
#	include <unistd.h>
#endif

#define newRemain(aip) (aip->endTime - time(NULL) - aip->latency - options.bidtime)

static time_t loginTime = 0;	/* Time of last login */
static time_t defaultLoginInterval = 12 * 60 * 60;	/* ebay login interval */

static int acceptBid(const char *pagename, auctionInfo *aip);
static int bid(auctionInfo *aip);
static int ebayLogin(auctionInfo *aip, int interval);
static char *getIdInternal(char *s, size_t len);
static int getInfoTiming(auctionInfo *aip, time_t *timeToFirstByte);
static int getQuantity(int want, int available);
static int makeBidError(const char *pagename, auctionInfo *aip);
static int match(memBuf_t *mp, const char *str);
static int parseBid(memBuf_t *mp, auctionInfo *aip);
static int preBid(auctionInfo *aip);
static int printMyItemsRow(char **row, int printNewline);
static int watch(auctionInfo *aip);

/*
 * attempt to match some input, neglecting case, ignoring \r and \n.
 * returns 0 on success, -1 on failure
 */
static int
match(memBuf_t *mp, const char *str)
{
	const char *cursor;
	int c;

	log(("\n\nmatch(\"%s\")\n\n", str));

	cursor = str;
	while ((c = memGetc(mp)) != EOF) {
		if (options.debug)
			logChar(c);
		if (tolower(c) == (int)*cursor) {
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

static const char PAGEID[] = "Page id: ";
static const char SRCID[] = "srcId: ";

/*
 * Get page info, including pagename variable, page id and srcid comments.
 */
pageInfo_t *
getPageInfo(memBuf_t *mp)
{
	const char *line;
	pageInfo_t p = {NULL, NULL, NULL}, *pp;
	int needPageName = 1;
	int needPageId = 1;
	int needSrcId = 1;
	int needMore = 3;

	log(("getPageInfo():\n"));
	while (needMore && (line = getTag(mp))) {
		char *tmp;

		if (strncmp(line, "!--", 3))
			continue;
		if (needPageName && (tmp = strstr(line, PAGENAME))) {
			if ((tmp = getPageNameInternal(tmp))) {
				--needMore;
				--needPageName;
				p.pageName = myStrdup(tmp);
			}
		} else if (needPageId && (tmp = strstr(line, PAGEID))) {
			if ((tmp = getIdInternal(tmp, sizeof(PAGEID)))) {
				--needMore;
				--needPageId;
				p.pageId = myStrdup(tmp);
			}
		} else if (needSrcId && (tmp = strstr(line, SRCID))) {
			if ((tmp = getIdInternal(tmp, sizeof(SRCID)))) {
				--needMore;
				--needSrcId;
				p.srcId = myStrdup(tmp);
			}
		}
	}
	log(("getPageInfo(): pageName = %s, pageId = %s, srcId = %s\n", nullStr(p.pageName), nullStr(p.pageId), nullStr(p.srcId)));
	if (needMore == 3)
		return NULL;
	pp = (pageInfo_t *)myMalloc(sizeof(pageInfo_t));
	pp->pageName = p.pageName;
	pp->pageId = p.pageId;
	pp->srcId = p.srcId;
	return pp;
}

static char *
getIdInternal(char *s, size_t len)
{
	char *id = s + len - 1;
	char *dash = strchr(id, '-');

	if (!*dash) {
		log(("getIdInternal(): Cannot find trailing dash: %s\n", id));
		return NULL;
	}
	*dash = '\0';
	log(("getIdInternal(): id = %s\n", id));
	return id;
}

/*
 * Free a pageInfo_t and it's internal members.
 */
void
freePageInfo(pageInfo_t *pp)
{
	if (pp) {
		free(pp->pageName);
		free(pp->pageId);
		free(pp->srcId);
		free(pp);
	}
}

/*
 * Calculate quantity to bid on.  If it is a dutch auction, never
 * bid on more than 1 less item than what is available.
 */
static int
getQuantity(int want, int available)
{
	if (want == 1 || available == 1)
		return 1;
	if (available > want)
		return want;
	return available - 1;
}

static const char GETINFO[] = "http://offer.ebay.com/ws/eBayISAPI.dll?ViewBids&item=";

/*
 * getInfo(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
int
getInfo(auctionInfo *aip)
{
	return getInfoTiming(aip, NULL);
}

/*
 * getInfoTiming(): Get info on auction from bid history page.
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) set auctionError
 */
static int
getInfoTiming(auctionInfo *aip, time_t *timeToFirstByte)
{
	int i, ret;
	time_t start;

	log(("\n\n*** getInfo auction %s price %s user %s\n", aip->auction, aip->bidPriceStr, options.username));
	if (ebayLogin(aip, 0))
		return 1;

	for (i = 0; i < 3; ++i) {
		memBuf_t *mp = NULL;

		if (!aip->query)
			aip->query = myStrdup2(GETINFO, aip->auction);
		start = time(NULL);
		if (!(mp = httpGet(aip->query, NULL))) {
			freeMembuf(mp);
			return httpError(aip);
		}
		ret = parseBidHistory(mp, aip, start, timeToFirstByte);
		freeMembuf(mp);
		if (i == 0 && ret == 1 && aip->auctionError == ae_mustsignin) {
			if (ebayLogin(aip, -1))
				break;
		} else if (aip->auctionError == ae_notime)
			/* Blank time remaining -- give it another chance */
			sleep(2);
		else
			break;
	}
	return ret;
}

/*
 * Note: quant=1 is just to dupe eBay into allowing the pre-bid to get
 *	 through.  Actual quantity will be sent with bid.
 */
static const char PRE_BID_URL[] = "http://offer.ebay.com/ws/eBayISAPI.dll?MfcISAPICommand=MakeBid&fb=2&co_partner_id=&item=%s&maxbid=%s&quant=%s";

/*
 * Get bid key
 *
 * returns 0 on success, 1 on failure.
 */
static int
preBid(auctionInfo *aip)
{
	memBuf_t *mp = NULL;
	int quantity = getQuantity(options.quantity, aip->quantity);
	char quantityStr[12];	/* must hold an int */
	size_t urlLen;
	char *url;
	int ret = 0;
	int found = 0;

	if (ebayLogin(aip, 0))
		return 1;
	sprintf(quantityStr, "%d", quantity);
	urlLen = sizeof(PRE_BID_URL) + strlen(aip->auction) + strlen(aip->bidPriceStr) + strlen(quantityStr) - 6;
	url = (char *)myMalloc(urlLen);
	sprintf(url, PRE_BID_URL, aip->auction, aip->bidPriceStr, quantityStr);
	log(("\n\n*** preBid(): url is %s\n", url));
	mp = httpGet(url, NULL);
	free(url);
	if (!mp)
		return httpError(aip);

	/* pagename should be PageReviewBidBottomButton, but don't check it */
	while (found < 7 && !match(mp, "<input type=\"hidden\" name=\"")) {
		if (!strncmp(mp->readptr, "key\"", 4)) {
			char *cp, *tmpkey;

			found |= 0x01;
			match(mp, "value=\"");
			tmpkey = getUntil(mp, '\"');
			log(("  reported key is: %s\n", tmpkey));

			/* translate key for URL */
			free(aip->bidkey);
			aip->bidkey = (char *)myMalloc(strlen(tmpkey)*3 + 1);
			for (cp = aip->bidkey; *tmpkey; ++tmpkey) {
				if (*tmpkey == '$') {
					*cp++ = '%';
					*cp++ = '2';
					*cp++ = '4';
				} else
					*cp++ = *tmpkey;
			}
			*cp = '\0';

			log(("\n\ntranslated key is: %s\n\n", aip->bidkey));
		} else if (!strncmp(mp->readptr, "uiid\"", 5)) {
			found |= 0x02;
			match(mp, "value=\"");
			free(aip->biduiid);
			aip->biduiid = myStrdup(getUntil(mp, '\"'));
			log(("preBid(): biduiid is \"%s\"", aip->biduiid));
		} else if (!strncmp(mp->readptr, "pass\"", 5)) {
			found |= 0x04;
			match(mp, "value=\"");
			free(aip->bidpass);
			aip->bidpass = myStrdup(getUntil(mp, '\"'));
			log(("preBid(): bidpass is \"%s\"", aip->bidpass));
		}
	}
	if (found < 7) {
		char *pagename;

		mp->readptr = mp->memory;
		pagename = getPageName(mp);
		if ((ret = makeBidError(pagename, aip)) < 0) {
			ret = auctionError(aip, ae_bidkey, NULL);
			bugReport("preBid", __FILE__, __LINE__, aip, mp, "cannot find bid key, uiid or password, found = %d", found);
		}
	}
	freeMembuf(mp);
	return ret;
}

static const char LOGIN_1_URL[] = "https://signin.ebay.com/ws/eBayISAPI.dll?SignIn";
static const char LOGIN_2_URL[] = "https://signin.ebay.com/ws/eBayISAPI.dll?SignInWelcome&userid=%s&pass=%s&keepMeSignInOption=1";

/*
 * Ebay login.  Make sure loging has been done with the given interval.
 *
 * Returns 0 on success, 1 on failure.
 */
static int
ebayLogin(auctionInfo *aip, int interval)
{
	memBuf_t *mp = NULL;
	size_t urlLen;
	char *url, *logUrl;
	pageInfo_t *pp;
	int ret = 0;
	char *password;

	/* negative value forces login */
	if (interval >= 0) {
		if (interval == 0)
			interval = defaultLoginInterval;	/* default: 12 hours */
		if ((time(NULL) - loginTime) <= interval)
			return 0;
	}

	cleanupCurlStuff();
	if (initCurlStuff())
		return auctionError(aip, ae_unknown, NULL);

	if (!(mp = httpGet(LOGIN_1_URL, NULL))) {
		freeMembuf(mp);
		return httpError(aip);
	}

	freeMembuf(mp);
	mp = NULL;

	urlLen = sizeof(LOGIN_2_URL) + strlen(options.usernameEscape);
	password = getPassword();
	url = malloc(urlLen + strlen(password));
	logUrl = malloc(urlLen + 5);

	sprintf(url, LOGIN_2_URL, options.usernameEscape, password);
	freePassword(password);
	sprintf(logUrl, LOGIN_2_URL, options.usernameEscape, "*****");

	mp = httpGet(url, logUrl);
	free(url);
	free(logUrl);
	if (!mp)
		return httpError(aip);

	if ((pp = getPageInfo(mp))) {
		log(("ebayLogin(): pagename = \"%s\", pageid = \"%s\", srcid = \"%s\"", nullStr(pp->pageName), nullStr(pp->pageId), nullStr(pp->srcId)));
		/*
		 * Pagename is usually MyeBaySummary, but it seems as though
		 * it can be any MyeBay page, and eBay is not consistent with
		 * naming of MyeBay pages (MyeBay, MyEbay, myebay, ...) so
		 * esniper must use strncasecmp().
		 */
		if ((pp->srcId && !strcmp(pp->srcId, "SignInAlertSupressor"))||
		    (pp->pageName && !strncasecmp(pp->pageName, "MyeBay", 6)))
			loginTime = time(NULL);
		else if (pp->pageName && !strcmp(pp->pageName, "PageSignIn"))
			ret = auctionError(aip, ae_login, NULL);
		else if (pp->srcId && !strcmp(pp->srcId, "Captcha.xsl"))
			ret = auctionError(aip, ae_captcha, NULL);
		else {
			ret = auctionError(aip, ae_login, NULL);
			bugReport("ebayLogin", __FILE__, __LINE__, aip, mp, "unknown pageinfo");
		}
	} else {
		log(("ebayLogin(): pageinfo is NULL\n"));
		ret = auctionError(aip, ae_login, NULL);
		bugReport("ebayLogin", __FILE__, __LINE__, aip, mp, "pageinfo is NULL");
	}
	freeMembuf(mp);
	freePageInfo(pp);
	return ret;
}

/*
 * acceptBid: handle all known AcceptBid pages.
 *
 * Returns -1 if page not recognized, 0 if bid accepted, 1 if bid not accepted.
 */
static int
acceptBid(const char *pagename, auctionInfo *aip)
{
	static const char ACCEPTBID[] = "AcceptBid_";
	static const char HIGHBID[] = "HighBidder";
	static const char OUTBID[] = "Outbid";
	static const char RESERVENOTMET[] = "ReserveNotMet";

	if (!pagename ||
	    strncmp(pagename, ACCEPTBID, sizeof(ACCEPTBID) - 1))
		return -1;
	pagename += sizeof(ACCEPTBID) - 1;
	/*
	 * valid pagenames include AcceptBid_HighBidder,
	 * AcceptBid_HighBidder_rebid, possibly others.
	 */
	if (!strncmp(pagename, HIGHBID, sizeof(HIGHBID) - 1))
		return aip->bidResult = 0;
	/*
	 * valid pagenames include AcceptBid_Outbid, AcceptBid_Outbid_rebid,
	 * possibly others.
	 */
	if (!strncmp(pagename, OUTBID, sizeof(OUTBID) - 1))
		return aip->bidResult = auctionError(aip, ae_outbid, NULL);
	/*
	 * valid pagenames include AcceptBid_ReserveNotMet,
	 * AcceptBid_ReserveNotMet_rebid, possibly others.
	 */
	if (!strncmp(pagename, RESERVENOTMET, sizeof(RESERVENOTMET) - 1))
		return aip->bidResult = auctionError(aip, ae_reservenotmet, NULL);
	/* unknown AcceptBid page */
	return -1;
}

/*
 * makeBidError: handle all known MakeBidError pages.
 *
 * Returns -1 if page not recognized, 0 if bid accepted, 1 if bid not accepted.
 */
static int
makeBidError(const char *pagename, auctionInfo *aip)
{
	static const char MAKEBIDERROR[] = "MakeBidError";

	if (!pagename ||
	    strncmp(pagename, MAKEBIDERROR, sizeof(MAKEBIDERROR) - 1))
		return -1;
	pagename += sizeof(MAKEBIDERROR) - 1;
	if (!*pagename ||
	    !strcmp(pagename, "AuctionEnded"))
		return aip->bidResult = auctionError(aip, ae_ended, NULL);
	if (!strcmp(pagename, "AuctionEnded_BINblock") ||
	    !strcmp(pagename, "AuctionEnded_BINblock "))
		return aip->bidResult = auctionError(aip, ae_cancelled, NULL);
	if (!strcmp(pagename, "Password"))
		return aip->bidResult = auctionError(aip, ae_badpass, NULL);
	if (!strcmp(pagename, "MinBid"))
		return aip->bidResult = auctionError(aip, ae_bidprice, NULL);
	if (!strcmp(pagename, "BuyerBlockPref"))
		return aip->bidResult = auctionError(aip, ae_buyerblockpref, NULL);
	if (!strcmp(pagename, "BuyerBlockPrefDoesNotShipToLocation"))
		return aip->bidResult = auctionError(aip, ae_buyerblockprefdoesnotshiptolocation, NULL);
	if (!strcmp(pagename, "BuyerBlockPrefNoLinkedPaypalAccount"))
		return aip->bidResult = auctionError(aip, ae_buyerblockprefnolinkedpaypalaccount, NULL);
	if (!strcmp(pagename, "HighBidder"))
		return aip->bidResult = auctionError(aip, ae_highbidder, NULL);
	if (!strcmp(pagename, "CannotBidOnItem"))
		return aip->bidResult = auctionError(aip, ae_cannotbid, NULL);
	if (!strcmp(pagename, "DutchSameBidQuantity"))
		return aip->bidResult = auctionError(aip, ae_dutchsamebidquantity, NULL);
	/* unknown MakeBidError page */
	return -1;
}

/*
 * Parse bid result.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
static int
parseBid(memBuf_t *mp, auctionInfo *aip)
{
	/*
	 * The following sometimes have more characters after them, for
	 * example AcceptBid_HighBidder_rebid (you were already the high
	 * bidder and placed another bid).
	 */
	char *pagename = getPageName(mp);
	int ret;

	aip->bidResult = -1;
	log(("parseBid(): pagename = %s\n", pagename));
	if ((ret = acceptBid(pagename, aip)) >= 0 ||
	    (ret = makeBidError(pagename, aip)) >= 0)
		return ret;
	bugReport("parseBid", __FILE__, __LINE__, aip, mp, "unknown pagename");
	printLog(stdout, "Cannot determine result of bid\n");
	return 0;	/* prevent another bid */
} /* parseBid() */

static const char BID_URL[] = "http://offer.ebay.com/ws/eBayISAPI.dll?MfcISAPICommand=MakeBid&BIN_button=Confirm%%20Bid&item=%s&key=%s&maxbid=%s&quant=%s&user=%s&pass=%s&uiid=%s&javascriptenabled=0&mode=1";

/*
 * Place bid.
 *
 * Returns:
 * 0: OK
 * 1: error
 */
static int
bid(auctionInfo *aip)
{
	memBuf_t *mp = NULL;
	size_t urlLen;
	char *url, *logUrl, *tmpUsername, *tmpPassword, *tmpUiid;
	int ret;
	int quantity = getQuantity(options.quantity, aip->quantity);
	char quantityStr[12];	/* must hold an int */

	if (!aip->bidkey || !aip->bidpass || !aip->biduiid)
		return auctionError(aip, ae_bidkey, NULL);

	if (ebayLogin(aip, 0))
		return 1;
	sprintf(quantityStr, "%d", quantity);

	/* create url */
	urlLen = sizeof(BID_URL) + strlen(aip->auction) + strlen(aip->bidkey) + strlen(aip->bidPriceStr) + strlen(quantityStr) + strlen(options.usernameEscape) + strlen(aip->bidpass) + strlen(aip->biduiid) - 12;
	url = (char *)myMalloc(urlLen);
	sprintf(url, BID_URL, aip->auction, aip->bidkey, aip->bidPriceStr, quantityStr, options.usernameEscape, aip->bidpass, aip->biduiid);

	logUrl = (char *)myMalloc(urlLen);
	tmpUsername = stars(strlen(options.usernameEscape));
	tmpPassword = stars(strlen(aip->bidpass));
	tmpUiid = stars(strlen(aip->biduiid));
	sprintf(logUrl, BID_URL, aip->auction, aip->bidkey, aip->bidPriceStr, quantityStr, tmpUsername, tmpPassword, tmpUiid);
	free(tmpUsername);
	free(tmpPassword);
	free(tmpUiid);

	if (!options.bid) {
		printLog(stdout, "Bidding disabled\n");
		log(("\n\nbid(): query url:\n%s\n", logUrl));
		ret = aip->bidResult = 0;
	} else if (!(mp = httpGet(url, logUrl))) {
		ret = httpError(aip);
	} else {
		ret = parseBid(mp, aip);
	}
	free(url);
	free(logUrl);
	freeMembuf(mp);
	return ret;
} /* bid() */

/*
 * watch(): watch auction until it is time to bid
 *
 * returns:
 *	0 OK
 *	1 Error
 */
static int
watch(auctionInfo *aip)
{
	int errorCount = 0;
	long remain = LONG_MIN;
	unsigned int sleepTime = 0;

	log(("*** WATCHING auction %s price-each %s quantity %d bidtime %ld\n", aip->auction, aip->bidPriceStr, options.quantity, options.bidtime));

	for (;;) {
		time_t tmpLatency;
		time_t start = time(NULL);
		time_t timeToFirstByte = 0;
		int ret = getInfoTiming(aip, &timeToFirstByte);
		time_t end = time(NULL);

		if (timeToFirstByte == 0)
			timeToFirstByte = end;
		tmpLatency = (timeToFirstByte - start);
		if ((tmpLatency >= 0) && (tmpLatency < 600))
			aip->latency = tmpLatency;
		printLog(stdout, "Latency: %d seconds\n", aip->latency);

		if (ret) {
			printAuctionError(aip, stderr);

			/*
			 * Fatal error?  We allow up to 50 errors, then quit.
			 * eBay "unavailable" doesn't count towards the total.
			 */
			if (aip->auctionError == ae_unavailable) {
				if (remain >= 0)
					remain = newRemain(aip);
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
			} else if (remain == LONG_MIN) {
				/* first time through?  Give it 3 chances then
				 * make the error fatal.
				 */
				int j;

				for (j = 0; ret && j < 3 && aip->auctionError == ae_notitle; ++j)
					ret = getInfo(aip);
				if (ret)
					return 1;
				remain = newRemain(aip);
			} else {
				/* non-fatal error */
				log(("ERROR %d!!!\n", ++errorCount));
				if (errorCount > 50)
					return auctionError(aip, ae_toomany, NULL);
				printLog(stdout, "Cannot find auction - internet or eBay problem?\nWill try again after sleep.\n");
				remain = newRemain(aip);
			}
		} else if (!isValidBidPrice(aip))
			return auctionError(aip, ae_bidprice, NULL);
		else
			remain = newRemain(aip);

		/*
		 * Check login when we are close to bidding.
		 */
		if (remain <= 300) {
			if (ebayLogin(aip, defaultLoginInterval - 600))
				return 1;
			remain = newRemain(aip);
		}

		/*
		 * if we're less than two minutes away, get bid key
		 */
		if (remain <= 150 && !aip->bidkey && aip->auctionError == ae_none) {
			int i;

			printf("\n");
			for (i = 0; i < 5; ++i) {
				/* ae_bidkey is used when the page loaded
				 * but failed for some unknown reason.
				 * Do not try again in this situation.
				 */
				if (!preBid(aip) ||
				    aip->auctionError == ae_bidkey)
					break;
			}
			if (aip->auctionError != ae_none &&
			    aip->auctionError != ae_highbidder) {
				printLog(stderr, "Cannot get bid key\n");
				return 1;
			}
		}

		remain = newRemain(aip);

		/* it's time!!! */
		if (remain <= 0)
			break;

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

		if ((remain=newRemain(aip)) <= 0)
			break;
	}
	return 0;
} /* watch() */

/*
 * parameters:
 * aip	auction to bid on
 *
 * return number of items won
 */
int
snipeAuction(auctionInfo *aip)
{
	int won = 0;
	char *tmpUsername;

	if (!aip)
		return 0;

	if (options.debug)
		logOpen(aip, options.logdir);

	tmpUsername = stars(strlen(options.username));
	log(("auction %s price %s quantity %d user %s bidtime %ld\n",
	     aip->auction, aip->bidPriceStr,
	     options.quantity, tmpUsername, options.bidtime));
	free(tmpUsername);

	if (ebayLogin(aip, 0)) {
		printAuctionError(aip, stderr);
		return 0;
	}

	/* 0 means "now" */
	if ((options.bidtime == 0) ? preBid(aip) : watch(aip)) {
		printAuctionError(aip, stderr);
		if (aip->auctionError != ae_highbidder)
			return 0;
	}

	/* ran out of time! */
	if (aip->endTime <= time(NULL)) {
		(void)auctionError(aip, ae_ended, NULL);
		printAuctionError(aip, stderr);
		return 0;
	}

	if (aip->auctionError != ae_highbidder) {
		printLog(stdout, "\nAuction %s: Bidding...\n", aip->auction);
		if (bid(aip)) {
			/* failed bid */
			printAuctionError(aip, stderr);
			return 0;
		}
	}

	/* view auction after bid.
	 * Stick it in a loop in case our timing is a bit off (due
	 * to wild swings in latency, for instance).
	 */
	for (;;) {
		if (options.bidtime > 0 && options.bidtime < 60) {
			time_t seconds = aip->endTime - time(NULL);

			if (seconds < 0)
				seconds = 0;
			/* extra 2 seconds to make sure auction is over */
			seconds += 2;
			printLog(stdout, "Auction %s: Waiting %d seconds for auction to complete...\n", aip->auction, seconds);
			sleep((unsigned int)seconds);
		}

		printLog(stdout, "\nAuction %s: Post-bid info:\n",
			 aip->auction);
		if (getInfo(aip))
			printAuctionError(aip, stderr);
		if (aip->remain > 0 && aip->remain < 60 &&
		    options.bidtime > 0 && options.bidtime < 60)
			continue;
		break;
	}

	if (aip->won == -1) {
		won = options.quantity < aip->quantity ?
			options.quantity : aip->quantity;
		printLog(stdout, "\nunknown outcome, assume that you have won %d items\n", won);
	} else {
		won = aip->won;
		printLog(stdout, "\nwon %d item(s)\n", won);
	}
	options.quantity -= won;
	return won;
}

static const char* MYITEMS_URL = "http://my.ebay.com/ws/eBayISAPI.dll?MyeBay&CurrentPage=MyeBayWatching";

#define MAX_TDS 6

/*
 * On first call, use printNewline to 0.  On subsequent calls, use return
 * value from previous call.
 */
static int
printMyItemsRow(char **row, int printNewline)
{
	const char *myitems_description[7][MAX_TDS] = {
		{0, 0, 0, 0, 0, 0},
		{"Note:\t\t%s\n", 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, 0, "Description:\t%s\n", 0, 0, 0},
		{0, 0, 0, 0, 0, 0},
		{0, "Price:\t\t%s\n", "Shipping:\t%s\n", "Bids:\t\t%s\n",
		 "Seller:\t\t%s\n", "Time:\t\t%s\n"}
	};
	int nColumns = numColumns(row);
	int column = 0;
	int ret = printNewline;

	if (nColumns == 4) {
		if (printNewline)
			printf("\n");
		else
			ret = 1;
	}
	for (; row[column]; ++column) {
		memBuf_t buf;
		char *value = NULL;

		if (column >= MAX_TDS || !myitems_description[nColumns][column])
			continue;
		strToMemBuf(row[column], &buf);
		/* special case: ItemNr encoded in item URL */
		if (nColumns == 4 && column == 2) {
			static const char search[] = "item=";
			char *tmp = strstr(row[column], search);

			if (tmp) {
				int i;

				tmp += sizeof(search) - 1;
				for (; !isdigit(*tmp); ++tmp)
					;
				for (i = 1; isdigit(tmp[i]); ++i)
					;
				value = myStrndup(tmp, (size_t)(i));
				printLog(stdout, "ItemNr:\t\t%s\n", value);
				free(value);
			}
		}
		value = getNonTag(&buf);
		/* Note text is 2nd non-tag */
		if (nColumns == 1 && column == 0)
			value = getNonTag(&buf);
		printLog(stdout, myitems_description[nColumns][column], value ? value : "");
	}
	return ret;
}

/*
 * TODO: allow user configuration of myItems.
 */
int
printMyItems(void)
{
	memBuf_t *mp = NULL;
	const char *table;
	char **row;
	auctionInfo *dummy = newAuctionInfo("0", "0");

	if (ebayLogin(dummy, 0)) {
		printAuctionError(dummy, stderr);
		freeAuction(dummy);
		return 1;
	}
	if (!(mp = httpGet(MYITEMS_URL, NULL))) {
		httpError(dummy);
		printAuctionError(dummy, stderr);
		freeAuction(dummy);
		freeMembuf(mp);
		return 1;
	}
	while ((table = getTableStart(mp))) {
		int printNewline = 0;

		/* search for table containing my itmes */
		if (!strstr(table, "id=\"Watching\""))
			continue;
		/* skip first descriptive table row */
		if ((row = getTableRow(mp)))
			freeTableRow(row);
		else {
			freeAuction(dummy);
			return 0; /* error? */
		}
		while ((row = getTableRow(mp))) {
			printNewline = printMyItemsRow(row, printNewline);
			freeTableRow(row);
		}
	}
	freeAuction(dummy);
	freeMembuf(mp);
	return 0;
}

#if DEBUG
/* secret option - test parser */
void
testParser(int flag)
{
	memBuf_t *mp = readFile(stdin);

	switch (flag) {
	case 1:
	    {
		/* print pagename */
		char *line;

		/* dump non-tag data */
		while ((line = getNonTag(mp)))
			printf("\"%s\"\n", line);

		/* pagename? */
		mp->readptr = mp->memory;
		if ((line = getPageName(mp)))
			printf("\nPAGENAME is \"%s\"\n", line);
		else
			printf("\nPAGENAME is NULL\n");
		break;
	    }
	case 2:
	    {
		/* run through bid history parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		time_t start = time(NULL), end;
		int ret = parseBidHistory(mp, aip, start, &end);

		printf("ret = %d\n", ret);
		printAuctionError(aip, stdout);
		break;
	    }
	case 3:
	    {
		/* run through bid result parser */
		auctionInfo *aip = newAuctionInfo("1", "2");
		int ret = parseBid(mp, aip);

		printf("ret = %d\n", ret);
		printAuctionError(aip, stdout);
		break;
	    }
	case 4:
	    {
		/* print bid history table */
		const char *table;
		char **row;
		char *cp;
		int rowNum = 0;

		while ((cp = getNonTag(mp))) {
			if (!strcmp(cp, "Time left:"))
				break;
		}
		if (!cp) {
			printf("time left not found!\n");
			break;
		}
		(void)getTableStart(mp); /* skip one table */
		table = getTableStart(mp);
		if (!table) {
			printf("no table found!\n");
			break;
		}

		printf("table: %s\n", table);
		while ((row = getTableRow(mp))) {
			int columnNum = 0;

			printf("\trow %d:\n", rowNum++);
			for (; row[columnNum]; ++columnNum) {
				memBuf_t buf;

				strToMemBuf(row[columnNum], &buf);
				printf("\t\tcolumn %d: %s\n", columnNum, getNonTag(mp));
				free(row[columnNum]);
			}
		}
		break;
	    }
	}
}
#endif
