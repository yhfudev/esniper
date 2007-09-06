/*
 * Copyright (c) 2002, 2007, Scott Nicol <esniper@users.sf.net>
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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "html.h"
#include "auctioninfo.h"
#include "history.h"
#include "esniper.h"

static long getSeconds(char *timestr);

static const char PRIVATE[] = "private auction - bidders' identities protected";

#define VIEWBIDS 1
#define VIEWTRANSACTIONS 2

#define NOTHING 0
#define PRICE 1
#define QUANTITY 2
#define SHIPPING 4
#define EVERYTHING (PRICE | QUANTITY | SHIPPING)

/*
 * parseBidHistory(): parses bid history page (pageName: PageViewBids)
 *
 * returns:
 *	0 OK
 *	1 error (badly formatted page, etc) - sets auctionError
 */
int
parseBidHistory(memBuf_t *mp, auctionInfo *aip, time_t start, time_t *timeToFirstByte)
{
	char *line;
	char **row;
	int rowCount;
	int ret = 0;		/* 0 = OK, 1 = failed */
	int foundHeader = 0;	/* found bid history table header */
	char *pagename;
	int pageType;
	int got;

	resetAuctionError(aip);

	if (timeToFirstByte)
		*timeToFirstByte = getTimeToFirstByte(mp);

	pagename = getPageName(mp);
	if (!strncmp(pagename, "PageViewBids", 12)) {
		pageType = VIEWBIDS;
		/* bid history or expired/bad auction number */
		while ((line = getNonTag(mp))) {
			if (!strcmp(line, "Bid History")) {
				log(("parseBidHistory(): got \"Bid History\"\n"));
				break;
			}
			if (!strcmp(line, "Unknown Item")) {
				log(("parseBidHistory(): got \"Unknown Item\"\n"));
				return auctionError(aip, ae_baditem, NULL);
			}
		}
	} else if (!strncmp(pagename, "PageViewTransactions", 20)) {
		/* transaction history -- buy it now only */
		pageType = VIEWTRANSACTIONS;
	} else if (!strcmp(pagename, "PageSignIn")) {
		return auctionError(aip, ae_mustsignin, NULL);
	} else {
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "unknown pagename");
		return auctionError(aip, ae_notitle, NULL);
	}

	/* Auction number */
	memReset(mp);
	if (memStr(mp, "\"BHitemNo\"")) {
		memChr(mp, '>');
		memSkip(mp, 1);
		line = getNonTag(mp);	/* Item number: */
		line = getNonTag(mp);	/* number */
		if (!line) {
			log(("parseBidHistory(): No item number"));
			return auctionError(aip, ae_baditem, NULL);
		}
	} else {
		log(("parseBidHistory(): BHitemNo not found"));
		return auctionError(aip, ae_baditem, NULL);
	}
#if DEBUG
	free(aip->auction);
	aip->auction = myStrdup(line);
#else
	if (strcmp(aip->auction, line)) {
		log(("parseBidHistory(): auction number %s does not match given number %s", line, aip->auction));
		return auctionError(aip, ae_baditem, NULL);
	}
#endif

	/* Auction title */
	memReset(mp);
	if (memStr(mp, "\"BHitemTitle\"")) {
		memChr(mp, '>');
		memSkip(mp, 1);
		line = getNonTag(mp);	/* title */
		if (!line) {
			log(("parseBidHistory(): No item title"));
			return auctionError(aip, ae_baditem, NULL);
		}
	} else if (memStr(mp, "\"BHitemDesc\"")) {
		memChr(mp, '>');
		memSkip(mp, 1);
		line = getNonTag(mp);	/* title */
		if (!line || !strcmp(line, "See item description")) {
			log(("parseBidHistory(): No item title, place 2"));
			return auctionError(aip, ae_baditem, NULL);
		}
	} else {
		log(("parseBidHistory(): BHitemTitle not found"));
		return auctionError(aip, ae_baditem, NULL);
	}
	free(aip->title);
	aip->title = myStrdup(line);
	printLog(stdout, "Auction %s: %s\n", aip->auction, aip->title);

	/* price, shipping, quantity */
	memReset(mp);
	aip->quantity = 1;	/* If quantity not found, assume 1 */
	got = NOTHING;
	while (got != EVERYTHING && memStr(mp, "\"BHCtBid\"")) {
		memChr(mp, '>');
		memSkip(mp, 1);
		line = getNonTag(mp);

		/* Can sometimes get starting bid, but that's not the price
		 * we are looking for.
		 */
		if (!strcasecmp(line, "Current bid:") ||
		    !strcasecmp(line, "Winning bid:") ||
		    !strcasecmp(line, "Your maximum bid:") ||
		    !strcasecmp(line, "price:")) {
			char *saveptr;

			line = getNonTag(mp);
			if (!line)
				return auctionError(aip, ae_noprice, NULL);
			log(("Currently: %s\n", line));
			aip->price = atof(priceFixup(line, aip));
			if (aip->price < 0.01)
				return auctionError(aip, ae_convprice, line);
			got |= PRICE;

			/* reserve not met? */
			saveptr = mp->readptr;
			line = getNonTag(mp);
			aip->reserve = !strcasecmp(line, "Reserve not met");
			if (!aip->reserve)
				mp->readptr = saveptr;
		} else if (!strcasecmp(line, "Quantity:")) {
			line = getNonTag(mp);
			if (!line)
				return auctionError(aip, ae_noquantity, NULL);
			errno = 0;
			aip->quantity = (int)strtol(line, NULL, 10);
			if (aip->quantity < 0 || (aip->quantity == 0 && errno == EINVAL))
				return auctionError(aip, ae_noquantity, NULL);
			log(("quantity: %d", aip->quantity));
			got |= QUANTITY;
		} else if (!strcasecmp(line, "Shipping:")) {
			line = getNonTag(mp);
			if (line) {
				free(aip->shipping);
				aip->shipping = myStrdup(line);
			}
			got |= SHIPPING;
		}
	}

	/* Time Left */
	memReset(mp);
	if (aip->quantity == 0 || memStr(mp, "Time Ended:")) {
		free(aip->remainRaw);
		aip->remainRaw = myStrdup("--");
		aip->remain = 0;
	} else if (memStr(mp, "timeLeft")) {
		memChr(mp, '>');
		memSkip(mp, 1);
		free(aip->remainRaw);
		aip->remainRaw = myStrdup(getNonTag(mp));
		aip->remain = getSeconds(aip->remainRaw);
		if (aip->remain < 0)
			return auctionError(aip, ae_badtime, aip->remainRaw);
	} else
		return auctionError(aip, ae_notime, NULL);
	printLog(stdout, "Time remaining: %s (%ld seconds)\n", aip->remainRaw, aip->remain);
	if (aip->remain) {
		struct tm *tmPtr;
		char timestr[20];

		aip->endTime = start + aip->remain;
		/* formated time/date output */
		tmPtr = localtime(&(aip->endTime));
		strftime(timestr , 20, "%d/%m/%Y %H:%M:%S", tmPtr);
#if !DEBUG
		printLog(stdout, "End time: %s\n", timestr);
#endif
	} else
		aip->endTime = aip->remain;

	/* bid history */
	memReset(mp);
	aip->bids = -1;
	if (memStr(mp, "Total Bids:")) {
		line = getNonTag(mp);	/* Total Bids: */
		line = getNonTag(mp);	/* number */
		log(("bids: %d", line));
		if (line) {
			errno = 0;
			aip->bids = (int)strtol(line, NULL, 10);
			if (aip->bids < 0 || (aip->bids == 0 && errno == EINVAL))
				aip->bids = -1;
			else if (aip->bids == 0) {
				aip->quantityBid = 0;
				aip->price = 0;
				printf("# of bids: %d\n"
				       "Currently: --  (your maximum bid: %s)\n",
				       aip->bids, aip->bidPriceStr);
				if (*options.username)
					printf("High bidder: -- (NOT %s)\n", options.username);
				else
					printf("High bidder: --\n");
				return 0;
			}
		}
	}

	/*
	 * Determine high bidder
	 *
	 * Format of high bidder table is:
	 *
	 *	Single item auction:
	 *	    Header line:
	 *		""
	 *		"Bidder"
	 *		"Bid Amount"
	 *		"Bid Time"
	 *		""
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<amount>
	 *			<date>
	 *			""
	 *	    (plus multiple rows of 1 column between entries)
	 *
	 *	    If there are no bids:
	 *			""
	 *			"No bids have been placed."
	 *
	 *	    If the auction is private, the user names are:
	 *		"private auction - bidders' identities protected"
	 *
	 *	Purchase (buy-it-now only):
	 *	    Header line:
	 *		""
	 *		"User ID"
	 *		"Price"
	 *		"Qty"
	 *		"Date"
	 *		""
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<price>
	 *			<quantity>
	 *			<date>
	 *			""
	 *	    (plus multiple rows of 1 column between entries)
	 *
	 *	    If there are no bids:
	 *			""
	 *			"No purchases have been made."
	 *
	 *	    If the auction is private, the user names are:
	 *		"private auction - bidders' identities protected"
	 *
	 *	Dutch auction:
	 *	    Header line:
	 *		""
	 *		"Bidder"
	 *		"Bid Amount"
	 *		"Qty Wanted"
	 *		"Qty Winning"
	 *		"Bid Time"
	 *		""
	 *
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<price>
	 *			<quantity wanted>
	 *			<quantity winning>
	 *			<date>
	 *			""
	 *	    (plus multiple rows of 1 column between entries)
	 *
	 *	    If there are no bids:
	 *			""
	 *			"No bids have been placed."
	 *
	 *	    Dutch auctions cannot be private.
	 *
	 *	If there are no bids, the text "No bids have been placed."
	 *	will be the first entry in the table.  If there are bids,
	 *	the last bidder might be "Starting Price", which should
	 *	not be counted.
	 */

	/* find bid history table */
	memReset(mp);
	while (!foundHeader && getTableStart(mp)) {
		int ncolumns;
		char *saveptr = mp->readptr;

		row = getTableRow(mp);
		ncolumns = numColumns(row);
		if (ncolumns >= 5) {
			char *rawHeader = row[1];
			char *header = getNonTagFromString(rawHeader);

			foundHeader = header &&
					(!strncmp(header, "Bidder", 6) ||
					 !strncmp(header, "User ID", 7));
		}
		if (!foundHeader)
			mp->readptr = saveptr;
	}
	if (!foundHeader) {
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "Cannot find bid table header");
		return auctionError(aip, ae_nohighbid, NULL);
	}

	/* skip over initial single-column rows */
	while ((row = getTableRow(mp))) {
		if (numColumns(row) == 1) {
			freeTableRow(row);
			continue;
		}
		break;
	}

	/* roll through table */
	switch (numColumns(row)) {
	case 2:	/* auction with no bids */
	    {
		char *s = getNonTagFromString(row[1]);

		if (!strcmp("No bids have been placed.", s) ||
		    !strcmp("No purchases have been made.", s)) {
			aip->quantityBid = 0;
			aip->bids = 0;
			aip->price = 0;
			printf("# of bids: %d\n"
			       "Currently: --  (your maximum bid: %s)\n",
			       aip->bids, aip->bidPriceStr);
			if (*options.username)
				printf("High bidder: -- (NOT %s)\n", options.username);
			else
				printf("High bidder: --\n");
		} else {
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "Unrecognized bid table line");
			ret = auctionError(aip, ae_nohighbid, NULL);
		}
		freeTableRow(row);
		free(s);
		break;
	    }

	case 5: /* single auction with bids */
	    {
		/* blank, user, price, date, blank */
		char *winner = getNonTagFromString(row[1]);
		char *currently = getNonTagFromString(row[2]);

		aip->quantityBid = 1;

		/* current price */
		aip->price = atof(priceFixup(currently, aip));
		if (aip->price < 0.01) {
			free(winner);
			free(currently);
			return auctionError(aip, ae_convprice, currently);
		}
		printLog(stdout, "Currently: %s  (your maximum bid: %s)\n",
			 currently, aip->bidPriceStr);
		free(currently);

		/* winning user */
		if (!strcmp(winner, PRIVATE)) {
			free(winner);
			winner = myStrdup((aip->price <= aip->bidPrice &&
					    (aip->bidResult == 0 ||
					     (aip->bidResult == -1 && aip->endTime - time(NULL) < options.bidtime))) ?  options.username : "[private]");
		}
		freeTableRow(row);

		/* count bids */
		if (aip->bids < 0) {
			int foundStartPrice = 0;
			for (aip->bids = 1; !foundStartPrice && (row = getTableRow(mp)); ) {
				if (numColumns(row) == 5) {
					char *bidder = getNonTagFromString(row[1]);

					foundStartPrice = !strcmp(bidder, "Starting Price");
					if (!foundStartPrice)
						++aip->bids;
					free(bidder);
				}
				freeTableRow(row);
			}
		}
		printLog(stdout, "# of bids: %d\n", aip->bids);

		/* print high bidder */
		if (strcasecmp(winner, options.username)) {
			if (*options.username)
				printLog(stdout, "High bidder: %s (NOT %s)\n",
					 winner, options.username);
			else
				printLog(stdout, "High bidder: %s\n", winner);
			aip->winning = 0;
			if (!aip->remain)
				aip->won = 0;
		} else if (aip->reserve) {
			printLog(stdout, "High bidder: %s (reserve not met)\n",
				 winner);
			aip->winning = 0;
			if (!aip->remain)
				aip->won = 0;
		} else {
			printLog(stdout, "High bidder: %s!!!\n", winner);
			aip->winning = 1;
			if (!aip->remain)
				aip->won = 1;
		}
		free(winner);
		break;
	    }
	case 6:	/* purchase */
	    {
		char *currently = getNonTagFromString(row[2]);

		aip->bids = 0;
		aip->quantityBid = 0;
		aip->won = 0;
		aip->winning = 0;
		/* find your purchase, count number of purchases */
		/* blank, user, price, quantity, date, blank */
		for (; row; row = getTableRow(mp)) {
			if (numColumns(row) == 6) {
				int quantity = getIntFromString(row[3]);
				char *bidder;

				++aip->bids;
				aip->quantityBid += quantity;
				bidder = getNonTagFromString(row[1]);
				if (!strcasecmp(bidder, options.username))
					aip->won = aip->winning = quantity;
				free(bidder);
			}
			freeTableRow(row);
		}
		printf("# of bids: %d\n", aip->bids);
		printf("Currently: %s  (your maximum bid: %s)\n",
		       currently, aip->bidPriceStr);
		free(currently);
		switch (aip->winning) {
		case 0:
			if (*options.username)
				printLog(stdout, "High bidder: various purchasers (NOT %s)\n", options.username);
			else
				printLog(stdout, "High bidder: various purchasers\n");
			break;
		case 1:
			printLog(stdout, "High bidder: %s!!!\n", options.username);
			break;
		default:
			printLog(stdout, "High bidder: %s!!! (%d items)\n", options.username, aip->winning);
			break;
		}
		break;
	    }
	case 7: /* dutch with bids */
	    {
		int wanted = 0;
		char *currently = NULL;

		aip->bids = 0;
		aip->quantityBid = 0;
		aip->won = 0;
		aip->winning = 0;
		/* find your bid, count number of bids */
		/* blank, user, price, wanted, winning, date, blank */
		for (; row; row = getTableRow(mp)) {
			if (numColumns(row) == 7) {
				int bidderWinning = getIntFromString(row[4]);

				++aip->bids;
				if (bidderWinning > 0) {
					char *bidder;

					aip->quantityBid += bidderWinning;
					free(currently);
					bidder = getNonTagFromString(row[1]);
					currently = getNonTagFromString(row[2]);
					if (!strcasecmp(bidder, options.username)) {
						wanted = getIntFromString(row[3]);
						aip->winning = bidderWinning;
					}
					free(bidder);
				}
			}
			freeTableRow(row);
		}
		if (!aip->remain)
			aip->won = aip->winning;
		printf("# of bids: %d\n", aip->bids);
		printf("Currently: %s  (your maximum bid: %s)\n",
		       currently, aip->bidPriceStr);
		free(currently);
		if (aip->winning > 0) {
			if (aip->winning == wanted)
				printLog(stdout, "High bidder: %s!!!\n", options.username);
			else
				printLog(stdout, "High bidder: %s!!! (%d out of %d items)\n", options.username, aip->winning, wanted);
		} else {
			if (*options.username)
				printLog(stdout, "High bidder: various dutch bidders (NOT %s)\n", options.username);
			else
				printLog(stdout, "High bidder: various dutch bidders\n");
		}
		break;
	    }
	default:
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "%d columns in bid table", numColumns(row));
		ret = auctionError(aip, ae_nohighbid, NULL);
		freeTableRow(row);
	}

	return ret;
} /* parseBidHistory() */

static long
getSeconds(char *timestr)
{
	static char second[] = "sec";
	static char minute[] = "min";
	static char hour[] = "hour";
	static char day[] = "day";
	static char ended[] = "ended";
	long accum = 0;
	long num;

	/* Time is blank in transition between "Time left: 1 seconds" and
	 * "Time left: auction has ended".  I don't know if blank means
	 * the auction is over, or it still running with less than 1 second.
	 * I'll make the safer assumption and say that there is 1 second
	 * remaining.
         * bomm: The transition seems to have changed to "--". I will accept
         * any string starting with "--".
	 */
	if (!*timestr || !strncmp(timestr, "--", 2))
		return 1;
	if (strstr(timestr, ended))
		return 0;
	while (*timestr) {
		num = strtol(timestr, &timestr, 10);
		while (isspace((int)*timestr))
			++timestr;
		if (!strncmp(timestr, second, sizeof(second) - 1))
			return(accum + num);
		else if (!strncmp(timestr, minute, sizeof(minute) - 1))
			accum += num * 60;
		else if (!strncmp(timestr, hour, sizeof(hour) - 1))
			accum += num * 3600;
		else if (!strncmp(timestr, day, sizeof(day) - 1))
			accum += num * 86400;
		else
			return -1;
		while (*timestr && !isdigit((int)*timestr))
			++timestr;
	}

	return accum;
}
