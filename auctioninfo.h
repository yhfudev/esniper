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

#ifndef AUCTIONINFO_H_INCLUDED
#define AUCTIONINFO_H_INCLUDED

#include <stdio.h>

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
	ae_badstatus,
	ae_bidprice,
	ae_bidkey,
	ae_badpass,
	ae_outbid,
	ae_reservenotmet,
	ae_ended,
	ae_duplicate,
	ae_toomany,
	ae_unavailable,
	/* ae_unknown must be last error */
	ae_unknown
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

extern auctionInfo *newAuctionInfo(char *auction, char *bidPriceStr);
extern void freeAuction(auctionInfo *aip);
extern int compareAuctionInfo(const void *p1, const void *p2);
extern void printAuctionError(auctionInfo *aip, FILE *fp);
extern void resetAuctionError(auctionInfo *aip);
extern int auctionError(auctionInfo *aip, enum auctionErrorCode pe,
                        const char *details);
extern int isValidBidPrice(const auctionInfo *aip);

#endif /* AUCTIONINFO_H_INCLUDED */
