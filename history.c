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

static int checkHeaderColumns(char **row);
static long getSeconds(char *timestr);

static const char PRIVATE[] = "private auction - bidders' identities protected";

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
	int foundHeader = 0;	/* found header for bid table */
	int ret = 0;		/* 0 = OK, 1 = failed */
	char *pagename;
	int pageType = 0;	/* 0 = bidHistory, 1 = buyer and bid history */
	int extraColumns = 0; /* 1 if Bidding Details or Action column in table */
	int skipTables = 0;	/* number of bid history tables to skip */

	resetAuctionError(aip);

	if (timeToFirstByte)
		*timeToFirstByte = getTimeToFirstByte(mp);

	/*
	 * Auction title
	 */
	pagename = getPageName(mp);
	if (!strcmp(pagename, "PageViewBids")) {
		/* bid history or expired/bad auction number */
		while ((line = getNonTag(mp))) {
			if (!strcmp(line, "Bid History")) {
				log(("parseBidHistory(): got \"Bid History\"\n"));
				break;
			}
			if (!strcmp(line, "Buyer and Bid History")) {
				log(("parseBidHistory(): got \"Buyer and Bid History\"\n"));
				pageType = 1;
				skipTables = 1;
				break;
			}
			if (!strcmp(line, "Unknown Item")) {
				log(("parseBidHistory(): got \"Unknown Item\"\n"));
				return auctionError(aip, ae_baditem, NULL);
			}
		}
		if (!line) {
			log(("parseBidHistory(): No title, place 1\n"));
			return auctionError(aip, ae_notitle, NULL);
		}
	} else if (!strcmp(pagename, "PageViewTransactions")) {
		/* transaction history -- buy it now only */
	} else if (!strcmp(pagename, "PageSignIn")) {
		return auctionError(aip, ae_mustsignin, NULL);
	}

	/*
	 * Auction item
	 */
	while ((line = getNonTag(mp))) {
		if (!strcmp(line, "Item title:")) {
			line = getNonTag(mp);
			break;
		}
	}
	if (!line) {
		log(("parseBidHistory(): No title, place 2\n"));
		return auctionError(aip, ae_notitle, NULL);
	}
	free(aip->title);
	aip->title = myStrdup(line);
	while ((line = getNonTag(mp))) {
		char *tmp;

		if (line[strlen(line) - 1] == ':')
			break;
		tmp = aip->title;
		aip->title = myStrdup2(tmp, line);
		free(tmp);
	}
	printLog(stdout, "Auction %s: %s\n", aip->auction, aip->title);

	/*
	 * Quantity/Current price/Time remaining
	 */
	aip->quantity = 1;	/* If quantity not found, assume 1 */
	for (; line; line = getNonTag(mp)) {
		if (!strcmp("Quantity:", line) ||
		    !strcmp("Quantity left:", line)) {
			line = getNonTag(mp);
			if (!line)
				return auctionError(aip, ae_noquantity, NULL);
			errno = 0;
			aip->quantity = (int)strtol(line, NULL, 10);
			if (aip->quantity < 0 || (aip->quantity == 0 && errno == EINVAL))
				return auctionError(aip, ae_noquantity, NULL);
			log(("quantity: %d", aip->quantity));
		} else if (!strcmp("Currently:", line)) {
			line = getNonTag(mp);
			if (!line)
				return auctionError(aip, ae_noprice, NULL);
			log(("Currently: %s\n", line));
			aip->price = atof(priceFixup(line, aip));
			if (aip->price < 0.01)
				return auctionError(aip, ae_convprice, line);
		} else if (!strcmp("Time left:", line)) {
			/* Use getTableCell() instead of getNonTag(), because
			 * for about a second at the end of an auction the
			 * time is left blank.  getTableCell() will give you
			 * a cell, even if it is empty.  getNonTag() will
			 * happily skip over to the next real text (like
			 * "Reserve not met", or whatever).
			 */
			getTableCell(mp); /* end of "Time left:" cell */
			getTableCell(mp); /* spacer */
			free(aip->remainRaw);
			aip->remainRaw = getNonTagFromString(getTableCell(mp));
			break;
		}
	}
	if (!aip->remainRaw)
		return auctionError(aip, ae_notime, NULL);
	if ((aip->remain = getSeconds(aip->remainRaw)) < 0)
		return auctionError(aip, ae_badtime, aip->remainRaw);
	printLog(stdout, "Time remaining: %s (%ld seconds)\n", line, aip->remain);
	if (aip->remain) {
		struct tm *tmPtr;
		char timestr[20];

		aip->endTime = start + aip->remain;
		/* formated time/date output */
		tmPtr = localtime(&(aip->endTime));
		strftime(timestr , 20, "%d/%m/%Y %H:%M:%S", tmPtr);
		printLog(stdout, "End time: %s\n", timestr);
	} else
		aip->endTime = aip->remain;

	if (!(line = getNonTag(mp)))
		return auctionError(aip, ae_nohighbid, NULL);
	if (!strcmp("Reserve not met", line))
		aip->reserve = 1;
	else if (!strcmp("User IDs will appear as", line))
		;
	else {
		aip->reserve = 0;
		/* start of header?  Probably a purchase */
		if ((foundHeader = !strncmp("Bidder", line, 6)) ||
		    (foundHeader = !strncmp("User ID", line, 7))) {
			int extra;

			log(("ParseBidHistory(): found table with header \"%s\"\n", line));
			/* get other headers to check them */
			row = getTableRow(mp);
			extra = checkHeaderColumns(row);
			if (extra < 0)
				foundHeader = 0;
			else
				extraColumns += extra;
			freeTableRow(row);
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
	 *		"User ID"
	 *		"Bid Amount"
	 *		"Date of bid"
	 *		"Bidding Details" (new, not on all auctions)
	 *		""
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<amount>
	 *			<date>
	 *			<view bidder details> (new, not on all auctions)
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
	 *		"Bid Amount"
	 *		"Qty"
	 *		"Date of Purchase"
	 *		"Bidding Details" (new, not on all auctions)
	 *		"Action" (new, not on all auctions)
	 *		""
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<amount>
	 *			<quantity>
	 *			<date>
	 *			<view bidder details> (new, not on all auctions)
	 *			<action> (new, not on all auctions)
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
	 *		"User ID"
	 *		"Bid Amount"
	 *		"Quantity wanted"
	 *		"Quantity winning"
	 *		"Date of Bid"
	 *		"Bidding Details" (new, not on all auctions)
	 *		""
	 *
	 *	    For each bid:
	 *			""
	 *			<user>
	 *			<amount>
	 *			<quantity wanted>
	 *			<quantity winning>
	 *			<date>
	 *			<view bidder details> (new, not on all auctions)
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
	 *	will be the first entry in the table.
	 */
	/* find header line */
	if (foundHeader && skipTables > 0) {
		log(("ParseBidHistory(): Skipping table"));
		foundHeader = 0;
		--skipTables;
	}
	while (!foundHeader && getTableStart(mp)) {
		while (!foundHeader && (row = getTableRow(mp))) {
			int ncolumns = numColumns(row);
			char *rawHeader = (ncolumns >= 5) ? row[1] : NULL;
			char *header = getNonTagFromString(rawHeader);

			foundHeader = header &&
					(!strncmp(header, "Bidder", 6) ||
					 !strncmp(header, "User ID", 7));
			if (foundHeader) {
				log(("ParseBidHistory(): found table with header \"%s\"\n", line));
				if (skipTables > 0) {
					log(("ParseBidHistory(): Skipping table"));
					foundHeader = 0;
					--skipTables;
				} else {
					int extra = checkHeaderColumns(row);

					if (extra < 0)
						foundHeader = 0;
					else
						extraColumns += extra;
				}
			}
			freeTableRow(row);
			free(header);
		}
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
	switch (numColumns(row) - extraColumns) {
	case 1:
		if (extraColumns != 1) {
			bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "%d columns in bid table, extraColumns = %d", numColumns(row), extraColumns);
			ret = auctionError(aip, ae_nohighbid, NULL);
			freeTableRow(row);
			break;
		}
		/* Fall through */
	case 2:	/* auction with no bids */
	    {
		char *s = getNonTagFromString(row[1]);

		if (!strcmp("No bids have been placed.", s) ||
		    !strcmp("No purchases have been made.", s)) {
			aip->quantityBid = 0;
			aip->bids = 0;
			/* can't determine starting bid on history page */
			aip->price = 0;
			printf("# of bids: 0\n"
			       "Currently: --  (your maximum bid: %s)\n",
			       aip->bidPriceStr);
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
		for (aip->bids = 1; (row = getTableRow(mp)); ) {
			if (numColumns(row) - extraColumns == 5)
				++aip->bids;
			freeTableRow(row);
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
			if (numColumns(row) - extraColumns == 6) {
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
			if (numColumns(row) - extraColumns == 7) {
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
		bugReport("parseBidHistory", __FILE__, __LINE__, aip, mp, "%d columns in bid table, extraColumns = %d", numColumns(row), extraColumns);
		ret = auctionError(aip, ae_nohighbid, NULL);
		freeTableRow(row);
	}

	return ret;
} /* parseBidHistory() */

static int
checkHeaderColumns(char **row)
{
	int n = numColumns(row);
	char *lastHeader = n >= 2 ? getNonTagFromString(row[n-2]) : NULL;
	int ret = 0;

	if (lastHeader == NULL) {
		log(("checkHeaderColumns(): this is not a table header"));
		--ret;
	} else if (!strncmp(lastHeader, "Bidding Details", 15)) {
		log(("checkHeaderColumns(): this table has Bidding Details column"));
		++ret;
	} else if (!strncmp(lastHeader, "Action", 6)) {
		log(("checkHeaderColumns(): this table has Action column"));
		++ret;
	}
	free(lastHeader);
	return ret;
}

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
