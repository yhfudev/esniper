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

#include "auctioninfo.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

static double *getIncrements(const auctionInfo *aip);

/*
 * Bidding increments
 *
 * first number is threshold for next increment range, second is increment.
 * For example, 1.00, 0.05 means that under $1.00 the increment is $0.05.
 *
 * Increments obtained from:
 *	http://pages.ebay.com/help/buy/bid-increments.html
 * (and similar pages on international sites)
 */

/*
 * Auction items not available from ebay.com:
 *
 * Argentina: http://www.mercadolibre.com.ar/
 * Brazil: http://www.mercadolivre.com.br/
 * India: http://www.baazee.com/ (seller can set increments)
 * Korea: http://www.auction.co.kr/
 * Mexico: http://www.mercadolibre.com.mx/
 */

/*
 * Australia: http://pages.ebay.com.au/help/buy/bid-increments.html
 * AU
 */
static double AUIncrements[] = {
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
 * Austria: http://pages.ebay.at/help/buy/bid-increments.html
 * Belgium: http://pages.befr.ebay.be/help/buy/bid-increments.html
 * France: http://pages.ebay.fr/help/buy/bid-increments.html
 * Germany: http://pages.ebay.de/help/buy/bid-increments.html
 * Italy: http://pages.ebay.it/help/buy/bid-increments.html
 * Netherlands: http://pages.ebay.nl/help/buy/bid-increments.html
 * Spain: http://pages.es.ebay.com/help/buy/bid-increments.html
 * EUR
 */
static double EURIncrements[] = {
	50.00, 0.50,
	500.00, 1.00,
	1000.00, 5.00,
	5000.00, 10.00,
	-1.00, 50.00
};

/*
 * Canada: http://pages.ebay.ca/help/buy/bid-increments.html
 * C
 */
static double CADIncrements[] = {
	1.00, 0.05,
	5.00, 0.25,
	25.00, 0.50,
	100.00, 1.00,
	-1.00, 2.50
};

/*
 * China: http://pages.ebay.com.cn/help/buy/bid-increments.html
 * RMB
 */
static double RMBIncrements[] = {
	1.01, 0.05,
	5.01, 0.20,
	15.01, 0.50,
	60.01, 1.00,
	150.01, 2.00,
	300.01, 5.00,
	600.01, 10.00,
	1500.01, 20.00,
	3000.01, 50.00,
	-1.00, 100.00
};

/*
 * Hong Kong: http://www.ebay.com.hk/
 * HKD
 *
 * Note: Cannot find bid-increments page.  Will use 0.01 to be safe.
 */
static double HKDIncrements[] = {
	-1.00, 0.01
};

/*
 * Singapore: http://www.ebay.com.sg/
 * SGD
 *
 * Note: Cannot find bid-increments page.  Will use 0.01 to be safe.
 *       From looking at auctions, it appears to be similar to US
 *       increments.
 */
static double SGDIncrements[] = {
	-1.00, 0.01
};

/*
 * Switzerland: http://pages.ebay.ch/help/buy/bid-increments.html
 * CHF
 */
static double CHFIncrements[] = {
	50.00, 0.50,
	500.00, 1.00,
	1000.00, 5.00,
	5000.00, 10.00,
	-1.00, 50.00
};

/*
 * Taiwan: http://pages.tw.ebay.com/help/buy/bid-increments.html
 * NT
 */
static double NTIncrements[] = {
	501.00, 15.00,
	2501.00, 30.00,
	5001.00, 50.00,
	25001.00, 100.00,
	-1.00, 200.00
};

/*
 * Ireland: http://pages.ebay.co.uk/help/buy/bid-increments.html
 * Sweden: http://pages.ebay.co.uk/help/buy/bid-increments.html
 * UK: http://pages.ebay.co.uk/help/buy/bid-increments.html
 * Note: Sweden & Ireland use GBP or EUR.  English help pages redirect
 *	to UK site.
 * GBP
 */
static double GBPIncrements[] = {
	1.01, 0.05,
	5.01, 0.20,
	15.01, 0.50,
	60.01, 1.00,
	150.01, 2.00,
	300.01, 5.00,
	600.01, 10.00,
	1500.01, 20.00,
	3000.01, 50.00,
	-1.00, 100.00
};

/*
 * New Zealand: http://pages.ebay.com/help/buy/bid-increments.html
 * US: http://pages.ebay.com/help/buy/bid-increments.html
 * Note: New Zealand site uses US or NT.
 * US
 */
static double USIncrements[] = {
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
 * Unknown currency.  Increment 0.01, just to be on the safe side.
 */
static double defaultIncrements[] = {
	-1.00, 0.01
};

static const char *auctionErrorString[] = {
	"",
	"Auction %s: Unknown item\n",
	"Auction %s: Title not found\n",
	"Auction %s: Current price not found\n",
	"Auction %s: Cannot convert price \"%s\"\n",
	"Auction %s: Quantity not found\n",
	"Auction %s: Time remaining not found\n",
	"Auction %s: Unknown time interval \"%s\"\n",
	"Auction %s: High bidder not found\n",
	"Auction %s: Connect failed\n",
	"Auction %s: Redirect failed\n",
	"Auction %s: Unexpected HTTP status: %s\n",
	"Auction %s: Bid price less than minimum bid price\n",
	"Auction %s: Bid key not found\n",
	"Auction %s: Bad username or password\n",
	"Auction %s: You have been outbid\n",
	"Auction %s: Reserve not met\n",
	"Auction %s: Auction has ended\n",
	"Auction %s: Duplicate auction\n",
	"Auction %s: Too many errors, quitting\n",
	"Auction %s: eBay temporarily unavailable\n",
	"Auction %s: Login failed\n",
	/* ae_unknown must be last error */
	"Auction %s: Unknown error code %d\n",
};

auctionInfo *
newAuctionInfo(const char *auction, const char *bidPriceStr)
{
	auctionInfo *aip = (auctionInfo *)myMalloc(sizeof(auctionInfo));

	aip->auction = myStrdup(auction);
	aip->bidPriceStr = priceFixup(myStrdup(bidPriceStr), NULL);
	aip->bidPrice = atof(aip->bidPriceStr);
	aip->endTime = 0;
	aip->latency = 0;
	aip->query = NULL;
	aip->bidkey = NULL;
	aip->bidpass = NULL;
	aip->quantity = 1;
	aip->bids = 0;
	aip->price = 0;
	aip->currency = NULL;
	aip->bidResult = -1;
	aip->won = -1;
	aip->winning = -1;
	aip->auctionError = ae_none;
	aip->auctionErrorDetail = NULL;
	aip->loginTime = 0;
	return aip;
}

void
freeAuction(auctionInfo *aip)
{
	if (!aip)
		return;
	free(aip->auction);
	free(aip->bidPriceStr);
	free(aip->query);
	free(aip->bidkey);
	free(aip->bidpass);
	free(aip->currency);
	free(aip->auctionErrorDetail);
	free(aip);
}

/*
 * compareAuctionInfo(): used to sort auctionInfo table
 *
 * returns (-1, 0, 1) if time remaining in p1 is (less than, equal to, greater
 * than) p2
 */
int
compareAuctionInfo(const void *p1, const void *p2)
{
	const auctionInfo * a1, * a2;

	a1 = *((const auctionInfo * const *)p1);
	a2 = *((const auctionInfo * const *)p2);

	/* Currently winning bids go first */
	if (a1->winning != a2->winning)
		return a1->winning > a2->winning ? -1 : 1;
	/* if end time is the same we compare the current price
	 * and use the lower price first
	 */
	if (a1->endTime == a2->endTime)
		/* comparison function must return an integer so we
		 * convert the price to cent or whatever it's called
		 */
		return (int)((a1->price * 100.0) - (a2->price * 100.0));
	return (int)(a1->endTime - a2->endTime);
}

/*
 * printAuctionError()
 */
void
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
void
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
int
auctionError(auctionInfo *aip, enum auctionErrorCode pe, const char *details)
{
	resetAuctionError(aip);
	aip->auctionError = pe;
	if (details)
		aip->auctionErrorDetail = myStrdup(details);
	return 1;
}

/*
 * isValidBidPrice(): Determine if the bid price is valid.
 *
 * If there are no bids, or you are the current high bidder, there is no
 * increment, just the minimum price.
 * If there are bids, then the increment depends on the current price.
 * See http://pages.ebay.com/help/basics/g-bid-increment.html for details.
 */
int
isValidBidPrice(const auctionInfo *aip)
{
	double increment = 0.0;

	if (aip->bids && !aip->winning) {
		int i;
		double *increments = getIncrements(aip);

		for (i = 0; increments[i] > 0; i += 2) {
			if (aip->price < increments[i])
				break;
		}
		increment = increments[i+1];
	}
	return aip->bidPrice >= (aip->price + increment);
}

static double *
getIncrements(const auctionInfo *aip)
{
	if (!aip->currency)
		return USIncrements;
	switch (aip->currency[0]) {
	case 'A':
		if (!strcmp(aip->currency, "AU"))
			return AUIncrements;
		break;
	case 'C':
		if (!strcmp(aip->currency, "C"))
			return CADIncrements;
		if (!strcmp(aip->currency, "CHF"))
			return CHFIncrements;
		break;
	case 'E':
		if (!strcmp(aip->currency, "EUR"))
			return EURIncrements;
		break;
	case 'G':
		if (!strcmp(aip->currency, "GBP"))
			return GBPIncrements;
		break;
	case 'H':
		if (!strcmp(aip->currency, "HKD"))
			return HKDIncrements;
		break;
	case 'N':
		if (!strcmp(aip->currency, "NT"))
			return NTIncrements;
		break;
	case 'R':
		if (!strcmp(aip->currency, "RMB"))
			return RMBIncrements;
		break;
	case 'S':
		if (!strcmp(aip->currency, "SGD"))
			return SGDIncrements;
		break;
	case 'U':
		if (!strcmp(aip->currency, "US"))
			return USIncrements;
		break;
	}
	return defaultIncrements;
}
