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

/*
 * This program will "snipe" an auction on eBay, automatically placing
 * your bid a few seconds before the auction ends.
 *
 * For updates, bug reports, etc, please go to esniper.sourceforge.net.
 */

#include "esniper.h"
#include "auction.h"
#include "auctionfile.h"
#include "options.h"
#include "util.h"

static const char version[]="esniper version 2.0.1";
static const char blurb[]="Please visit http://esniper.sourceforge.net/ for updates and bug reports";

#if defined(unix) || defined (__unix) || defined (__MACH__)
#       include <unistd.h>
#endif
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

option_t options = {
	NULL,             /* user */
	NULL,             /* password */
	DEFAULT_BIDTIME,  /* bidtime */
	1,                /* quantity */
	NULL,             /* config file */       
	NULL,             /* auction file */
	1,                /* bid */
	1,                /* reduce quantity */
	0,                /* debug */
	0                 /* usage */
};

const char DEFAULT_CONF_FILE[] = ".esniper";
const char HOSTNAME[] = "cgi.ebay.com";

static void sigAlarm(int sig);
static void sigTerm(int sig);
static int sortAuctions(auctionInfo **auctions, int numAuctions, char *user,
                        int *quantity, const char *progname);
static void cleanup(void);
static void usage(const char *progname, int longhelp);
int main(int argc, char *argv[]);

/* used for option table */
static int CheckSeconds(const void* valueptr, const optionTable_t* tableptr,
                        const char* filename, const char *line);
static int CheckQuantity(const void* valueptr, const optionTable_t* tableptr,
                         const char* filename, const char *line);
static int ReadUsername(const void* valueptr, const optionTable_t* tableptr,
                        const char* filename, const char *line);
static int ReadPassword(const void* valueptr, const optionTable_t* tableptr,
                        const char* filename, const char *line);
static int CheckFilename(const void* valueptr, const optionTable_t* tableptr,
                         const char* filename, const char *line);
static int SetHelp(const void* valueptr, const optionTable_t* tableptr,
                   const char* filename, const char *line);

static void
sigAlarm(int sig)
{
	signal(SIGALRM, sigAlarm);
	log((" SIGALRM"));
}

static void
sigTerm(int sig)
{
	signal(SIGTERM, SIG_DFL);
	log(("SIGTERM...\n"));
	raise(sig);
}

/*
 * Get initial auction info, sort items based on end time.
 */
static int
sortAuctions(auctionInfo **auctions, int numAuctions, char *user, int *quantity, const char *progname)
{
	int i, sawError = 0;

	for (i = 0; i < numAuctions; ++i) {
		int j;

		if (options.debug)
			logOpen(progname, auctions[i]);
		for (j = 0; j < 3; ++j) {
			if (j > 0)
				printLog(stderr, "Retrying...\n");
			if (!getInfo(auctions[i], *quantity, user))
				break;
			printAuctionError(auctions[i], stderr);
		}
		if (j == 3)
			exit(1);
		printLog(stdout, "\n");
	}
	if (numAuctions > 1) {
		printLog(stdout, "Sorting auctions...\n");
		/* sort by end time */
		qsort(auctions, numAuctions, sizeof(auctionInfo *), compareAuctionInfo);
	}

	/* get rid of obvious cases */
	for (i = 0; i < numAuctions; ++i) {
		auctionInfo *aip = auctions[i];

		if (!aip->remain) {
			if (aip->won > 0)
				*quantity -= aip->won;
			auctionError(aip, ae_ended, NULL);
		} else if (!isValidBidPrice(aip))
			auctionError(aip, ae_bidprice, NULL);
		else if (i > 0 && auctions[i-1] &&
			 !strcmp(aip->auction, auctions[i-1]->auction))
			auctionError(aip, ae_duplicate, NULL);
		else
			continue;
		printAuctionError(aip, stderr);
		freeAuction(auctions[i]);
		auctions[i] = NULL;
		++sawError;
	}

	/* eliminate dead auctions */
	if (sawError) {
		int j;

		for (i = j = 0; i < numAuctions; ++i) {
			if (auctions[i])
				auctions[j++] = auctions[i];
		}
		numAuctions -= sawError;
	}
	return numAuctions;
} /* sortAuctions() */

/* cleanup open files */
static void
cleanup()
{
	logClose();
}

/* specific check functions would reside in main module */

/*
 * CheckSeconds(): convert integer value or "now", check minimum value
 *
 * returns: 0 = OK, else error
 */
static int CheckSeconds(const void* valueptr, const optionTable_t* tableptr,
                        const char* filename, const char *line)
{
   int intval;
   char *endptr;

   /* value specified? */
   if(!valueptr) {
      if(filename)
         printLog(stderr,
       "Config entry \"%s\" in file %s needs an integer value or \"now\"\n",
                  line, filename);
      else
         printLog(stderr,
                  "Option -%s needs an integer value or \"now\"\n",
                  line);
   }
   /* specific string value "now" */
   if(!strcmp((char*)valueptr, "now")) {
      /* copy value to target variable */
      *(int*)(tableptr->value)=0;
      log(("seconds value is %d (now)", *(int*)(tableptr->value)));
      return 0;
   }

   /* else must be integer value */
   intval = strtol((char*)valueptr, &endptr, 10);
   if(*endptr != '\0') {
      if(filename)
         printLog(stderr, "Config entry \"%s\" in file %s", line, filename);
      else
         printLog(stderr, "Option -%s", line);
      printLog(stderr, "accepts integer values greater than 4 or \"now\"\n");
      return 1;
   }
   /* check minimum */
   if(intval < MIN_BIDTIME) {
      if(filename)
         printLog(stderr, "Value at config entry \"%s\" in file %s",
                  line, filename);
      else
         printLog(stderr, "Value %d at option -%s", intval, line);
      printLog(stderr, "too small, using minimum value of %d seconds\n",
               MIN_BIDTIME);
      intval = MIN_BIDTIME;
   }

   /* copy value to target variable */
   *(int*)(tableptr->value) = intval;
   log(("seconds value is %d\n", *(int*)(tableptr->value)));
   return 0;
}

/*
 * CheckQuantity(): convert integer value, check for positive value
 *
 * returns: 0 = OK, else error
 */
static int CheckQuantity(const void* valueptr, const optionTable_t* tableptr,
                         const char* filename, const char *line)
{
   if(*(int*)valueptr <= 0) {
      if(filename)
         printLog(stderr,
                  "Quantity must be positive at \"%s\" in file %s\n",
                  line, filename);
      else
         printLog(stderr,
                  "Quantity must be positive at option -%s\n",
                  line);
      return 1;
   }
   /* copy value to target variable */
   *(int*)(tableptr->value) = *(int*)valueptr;
   return 0;
}

/*
 * ReadUsername(): read username from console
 *
 * note: not called by option processing code.  Called directly from main()
 *	if esniper has not been given username.
 *
 * returns: 0 = OK, else error
 */
static int ReadUsername(const void* valueptr, const optionTable_t* tableptr,
                        const char* filename, const char *line)
{
	char *username = prompt("Enter eBay username: ", 0);

	if (!username) {
		printLog(stderr, "Username entry failed!\n");
		return 1;
	}

	*(char**)(tableptr->value) = username;
	log(("username is %s\n", *(char**)(tableptr->value)));
	return 0;
}

/*
 * ReadPassword(): read password from console
 *
 * returns: 0 = OK, else error
 */
static int ReadPassword(const void* valueptr, const optionTable_t* tableptr,
                        const char* filename, const char *line)
{
	char *passwd = prompt("Enter eBay password: ", 1);

	if (!passwd) {
		printLog(stderr, "Password entry failed!\n");
		return 1;
	}
	putchar('\n');

	/* don't log password! */
	*(char**)(tableptr->value) = passwd;
	return 0;
}

/*
 * CheckFilename(): accept accessible files only
 *
 * returns: 0 = OK, else error
 */
static int CheckFilename(const void* valueptr, const optionTable_t* tableptr,
                         const char* filename, const char *line)
{
   if(access((char*)valueptr, R_OK)) {
      if(filename)
         printLog(stderr,
                "File specified in at \"%s\" in file %s is not readable: %s\n",
                  line, filename, strerror(errno));
      else
         printLog(stderr,
                  "File \"%s\" specified at option -%s is not readable: %s\n",
                  (char*)valueptr, line, strerror(errno));
      return 1;
   }
   /* free old value if present */
   if (tableptr->set && *(char**)(tableptr->value))
      free(*(char**)(tableptr->value));

   /* copy value to target variable */
   *(char**)(tableptr->value) = myStrdup(valueptr);
   return 0;
}

/*
 * SetHelp(): set usage to more than 1 to activate long help
 *
 * returns: 0 = OK
 */
static int SetHelp(const void* valueptr, const optionTable_t* tableptr,
                        const char* filename, const char *line)
{
   /* copy value to target variable */
   *(int*)(tableptr->value) = 2;
   return 0;
}

static const char usageSummary[] =
  "usage: %s [-dnv] [-p] [-u user] [-s secs|now] [-q quantity]\n"
  "       [-f auction_file] [-c conf_file] [-q quantity] [auction price ...]\n"
  "\n";

static const char usageLong[] =
 "where:\n"
 "-d: write debug output to file\n"
 "-n: do not place bid\n"
 "-v: print version and exit\n"
 "-p: prompt for password\n"
 "-u: ebay username\n"
 "-s: time to place bid which may be \"now\" or seconds before end of auction\n"
 "    (default is %d seconds before end of auction)\n"
 "-q: quantity to buy (default is 1)\n"
 "-f: read auction data from file\n"
 "-c: read config from specified file instead of \".esniper\"\n"
 "\n"
 "If you don't specify an auction data file with option -f you must provide\n"
 "<auction> <price> pair[s] on command line.\n"
 "You have to specify username and password either in one of the config files\n"
 "or using options -u or -p.\n";

static void
usage(const char *progname, int longhelp)
{
	fprintf(stderr, usageSummary, progname);
	if (longhelp)
		fprintf(stderr, usageLong, DEFAULT_BIDTIME);
	else
		fprintf(stderr, "use \"%s -h\" for more help.\n", progname);
	fprintf(stderr,"\n%s\n", blurb);
}

int
main(int argc, char *argv[])
{
	int ret = 1;	/* assume failure, change if successful */
	const char *progname = basename(argv[0]);
	auctionInfo **auctions = NULL;
	int c, i;
	int argcmin = 2;
	int numAuctions = 0, numAuctionsOrig = 0;

   /* this table describes options and config entries */
   static optionTable_t optiontab[] = {
   /* username and password must be the first two table entries */
   {"username", "u", (void*)&options.user,         OPTION_STRING,   0, NULL},
   {"password",NULL, (void*)&options.password,     OPTION_STRING,   0, NULL},
   /* ^^^ username and password must be the first two table entries ^^^ */

   {"seconds",  "s", (void*)&options.bidtime,      OPTION_SPECIAL,  0,
                                                                &CheckSeconds},
   {"quantity", "q", (void*)&options.quantity,     OPTION_INT,      0,
                                                               &CheckQuantity},
   {NULL,       "p", (void*)&options.password,     OPTION_SPECIAL,  0,
                                                                &ReadPassword},
   {NULL,       "c", (void*)&options.conffilename, OPTION_STRING,   0,
                                                               &CheckFilename},
   {NULL,       "f", (void*)&options.auctfilename, OPTION_STRING,   0,
                                                               &CheckFilename},
   {"reduce",   "r", (void*)&options.reduce,       OPTION_BOOL,     0, NULL},
   {"dontreduce","R",(void*)&options.reduce,       OPTION_BOOL_NEG, 0, NULL},
   {"dontbid",  "n", (void*)&options.bid,          OPTION_BOOL_NEG, 0, NULL},
   {"bid",     NULL, (void*)&options.bid,          OPTION_BOOL,     0, NULL},
   {"debug",    "d", (void*)&options.debug,        OPTION_BOOL,     0, NULL},
   {NULL,       "?", (void*)&options.usage,        OPTION_BOOL,     0, NULL},
   {NULL,       "h", (void*)&options.usage,        OPTION_SPECIAL,  0,
                                                                     &SetHelp},
   {NULL, NULL, NULL, 0, 0, NULL}
   };

	/* all known options */
	static const char optionstring[]="c:df:hnpq:rRs:u:vX";

	atexit(cleanup);

	/* first, check for debug, no-bid, config file and auction file
	 * options but accept all other options to avoid error messages
	 */
	while ((c = getopt(argc, argv, optionstring)) != EOF) {
		int olddebug = options.debug;
		switch (c) {
		case 'd': /* debug */
		case 'h': /* long help */
		case 'n': /* don't bid */
		case '?': /* unknown -> help */
			parseGetoptValue(c, NULL, optiontab);
			if (!olddebug && options.debug)
				logOpen(progname, NULL);
			break;
		case 'c': /* config file */
		case 'f': /* auction file */
			parseGetoptValue(c, optarg, optiontab);
			break;
		case 'X': /* secret option - for testing page parsing */
			testParser(argc, argv);
			exit(0);
			break;
		case 'v': /* version */
			fprintf(stderr, "%s\n%s\n", version, blurb);
			exit(0);
			break;
		default:
			/* ignore other options, these will be parsed
			 * after config files
			 */
			break;
		}
	}

	if (options.usage) {
		usage(progname, options.usage > 1);
		exit(1);
	}

	/* if config file specified assume one specific file
	 * including directory
	 */
	if (options.conffilename) {
		readConfigFile(options.conffilename, optiontab);
	} else {
		char *homedir = getenv("HOME");

		/* TODO: on UNIX we could use getpwuid() to find out home directory */
		if (homedir && *homedir) {
			/* parse $HOME/.esniper */
			char *cfname = myStrdup3(homedir,"/",DEFAULT_CONF_FILE);

			readConfigFile(cfname, optiontab);
			free(cfname);
		} else
			printLog(stderr, "Warning: environment variable HOME not set. Cannot parse $HOME/%s.\n", DEFAULT_CONF_FILE);

		if (options.auctfilename) {
			/* parse .esniper in auction file's directory */
			char *auctfilename = myStrdup(options.auctfilename);
			char *cfname = myStrdup3(dirname(auctfilename), "/", DEFAULT_CONF_FILE);

			readConfigFile(cfname, optiontab);
			free(auctfilename);
			free(cfname);
		}
	}

	/* parse auction file */
	if (options.auctfilename)
		readConfigFile(options.auctfilename, optiontab);

	/* skip back to first arg */
	optind = 1;
	/* check options which may overwrite settings from config file */
	while ((c = getopt(argc, argv, optionstring)) != EOF) {
		switch (c) {
		case 'q': /* quantity */
		case 's': /* seconds */
		case 'u': /* user */
			parseGetoptValue(c, optarg, optiontab);
			break;
		case 'p': /* read password */
		case 'r': /* reduce */
		case 'R': /* don't reduce */
			parseGetoptValue(c, NULL, optiontab);
			break;
		default:
			/* ignore other options, these have been parsed
			 * before config files
			 */
			break;
		}
	}

	argc -= optind;
	argv += optind;

	log(("options.user=%s\n", nullStr(options.user)));
	/* Do not log password! */
	/*log(("options.password=%s\n", nullStr(options.password)));*/
	log(("options.bidtime=%d\n", options.bidtime));
	log(("options.quantity=%d\n", options.quantity));
	log(("options.conffilename=%s\n", nullStr(options.conffilename)));
	log(("options.auctfilename=%s\n", nullStr(options.auctfilename)));
	log(("options.bid=%d\n", options.bid));
	log(("options.reduce=%d\n", options.reduce));
	log(("options.debug=%d\n", options.debug));
	log(("options.usage=%d\n", options.usage));

	/* no args needed if auction file specified */
	if (options.auctfilename)
		argcmin=0;

	if (!options.usage) {
		if (argc < argcmin) {
			fprintf(stderr, "Error: no auctions specified.\n");
			options.usage = 1;
		}
		if (argc % 2) {
			fprintf(stderr, "Error: auctions and prices must be specified in pairs.\n");
			options.usage = 1;
		}
	}

	if (!options.usage) {
		if (!options.user)
			ReadUsername(NULL, &optiontab[0], NULL, "u");
		if (!options.password)
			ReadPassword(NULL, &optiontab[1], NULL, "p");
	}

	if (options.usage) {
		usage(progname, options.usage > 1);
		exit(1);
	}

	/* init variables */
	if (options.auctfilename) {
		numAuctions = readAuctionFile(options.auctfilename, &auctions);
	} else {
		numAuctions = argc / 2;
		auctions = (auctionInfo **)malloc(numAuctions * sizeof(auctionInfo *));
		for (i = 0; i < argc/2; i++)
			auctions[i] = newAuctionInfo(argv[2*i], argv[2*i+1]);
	}

	signal(SIGALRM, sigAlarm);
	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, sigTerm);

	numAuctionsOrig = numAuctions;
	{
		int quantity = options.quantity;
		numAuctions = sortAuctions(auctions, numAuctions, options.user,
					   &quantity, progname);

		if (quantity < options.quantity) {
			printLog(stdout, "\nYou have already won %d item(s).\n",
				 options.quantity - quantity);
			if (options.reduce) {
				options.quantity = quantity;
				printLog(stdout,
					 "Quantity reduced to %d item(s).\n",
					 options.quantity);
			}
		}
	}

	for (i = 0; i < numAuctions && options.quantity > 0; ++i) {
		int retryCount, bidRet;

		if (!auctions[i])
			continue;

		if (options.debug)
			logOpen(progname, auctions[i]);

		log(("auction %s price %s quantity %d user %s bidtime %ld\n",
		     auctions[i]->auction, auctions[i]->bidPriceStr,
		     options.quantity, options.user, options.bidtime));

		if (numAuctionsOrig > 1)
			printLog(stdout, "\nNeed to win %d item(s), %d auction(s) remain\n\n", options.quantity, numAuctions - i);

		/* 0 means "now" */
		if (options.bidtime == 0) {
			if (preBid(auctions[i])) {
				printAuctionError(auctions[i], stderr);
				continue;
			}
		} else {
			if (watch(auctions[i], options.quantity,
				  options.user, options.bidtime)) {
				printAuctionError(auctions[i], stderr);
				continue;
			}
		}

		/* ran out of time! */
		if (!auctions[i]->remain) {
			auctionError(auctions[i], ae_ended, NULL);
			printAuctionError(auctions[i], stderr);
			continue;
		}

		if (options.bid)
			printLog(stdout, "\nAuction %s: Bidding...\n",
				 auctions[i]->auction);

		if (!auctions[i]->key && options.bid) {
			printLog(stderr, "Auction %s: Problem with bid.  No bid placed.\n", auctions[i]->auction);
			continue;
		}

		log(("*** BIDDING!!! auction %s price %s quantity %d user %s\n",
			auctions[i]->auction, auctions[i]->bidPriceStr,
			options.quantity, options.user));

		for (retryCount = 0; retryCount < 3; retryCount++) {
			bidRet = bid(options.bid, auctions[i], options.quantity,
				     options.user, options.password);

			if (!bidRet || auctions[i]->auctionError != ae_connect)
				break;
			printLog(stderr, "Auction %s: retrying...\n",
				 auctions[i]->auction);
		}

		/* failed bid */
		if (bidRet) {
			printAuctionError(auctions[i], stderr);
			continue;
		}

		/* view auction after bid */
		if (options.bidtime > 0 && options.bidtime < 60) {
			printLog(stdout, "Auction %s: Waiting %d seconds for auction to complete...\n", auctions[i]->auction, options.bidtime);
			/* make sure it really is over */
			sleep(options.bidtime + 1);
		}

		printLog(stdout, "\nAuction %s: Post-bid info:\n",
			 auctions[i]->auction);
		if (getInfo(auctions[i], options.quantity, options.user))
			printAuctionError(auctions[i], stderr);

		if (auctions[i]->won == -1) {
			int won = auctions[i]->quantity;

			if (options.quantity < won)
				won = options.quantity;
			options.quantity -= won;
			printLog(stdout, "\nunknown outcome, assume that you have won %d items\n", won);
		} else {
			options.quantity -= auctions[i]->won;
			printLog(stdout, "\nwon %d items\n", auctions[i]->won);
			ret = 0;
		}
	}

	exit(ret);
}
