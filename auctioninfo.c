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

#include "auctioninfo.h"
#include "util.h"
#include <stdlib.h>

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

auctionInfo *
newAuctionInfo(char *auction, char *bidPriceStr)
{
	auctionInfo *aip = (auctionInfo *)myMalloc(sizeof(auctionInfo));

	aip->auction = myStrdup(auction);
	aip->bidPriceStr = myStrdup(bidPriceStr);
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

void
freeAuction(auctionInfo *aip)
{
	if (!aip)
		return;
	free(aip->auction);
	free(aip->bidPriceStr);
	free(aip->host);
	free(aip->query);
	free(aip->key);
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
	long r1 = (*((auctionInfo **)p1))->remain;
	long r2 = (*((auctionInfo **)p2))->remain;

	return (r1 == r2) ? 0 : (r1 < r2 ? -1 : 1);
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
 */
int
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
