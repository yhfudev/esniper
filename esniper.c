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

/*
 * This program will "snipe" an auction on eBay, automatically placing
 * your bid a few seconds before the auction ends.
 *
 * For updates, bug reports, etc, please go to http://esniper.sf.net/.
 */

#include "esniper.h"
#include "auction.h"
#include "auctionfile.h"
#include "options.h"
#include "util.h"

static const char version[]="esniper version 2.5.4";
static const char blurb[]="Please visit http://esniper.sf.net/ for updates and bug reports.";

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(WIN32)
#	include <io.h>
#	define access(name, mode) _access((name), (mode))
#	define sleep(t)	_sleep((t) * 1000)
#	define R_OK 0x04
	extern int getopt(int, char *const *, const char *);
	extern int opterr, optind, optopt;
	extern char *optarg;
#else
#       include <unistd.h>
#endif

option_t options = {
	NULL,             /* user */
	NULL,             /* password */
	DEFAULT_BIDTIME,  /* bidtime */
	1,                /* quantity */
	NULL,             /* configuration file */
	NULL,             /* auction file */
	1,                /* bid */
	1,                /* reduce quantity */
	0,                /* debug */
	0,                /* usage */
	0,                /* batch */
	0,                /* password encrypted? */
	{ NULL, 0 },      /* proxy host & port */
	NULL,             /* log directory */
};

const char DEFAULT_CONF_FILE[] = ".esniper";
const char HOSTNAME[] = "cgi.ebay.com";
const char BID_HOSTNAME[] = "offer.ebay.com";

static const char *progname = NULL;

/* support functions */
#if !defined(WIN32)
static void sigAlarm(int sig);
#endif
static void sigTerm(int sig);
static int sortAuctions(auctionInfo **auctions, int numAuctions, char *user,
                        int *quantity);
static void cleanup(void);
static int usage(int helptype);
#define USAGE_SUMMARY	0x01
#define USAGE_LONG	0x02
#define USAGE_CONFIG	0x04
int main(int argc, char *argv[]);

/* used for option table */
static int CheckDebug(const void* valueptr, const optionTable_t* tableptr,
                      const char* filename, const char *line);
static int CheckSecs(const void* valueptr, const optionTable_t* tableptr,
                     const char* filename, const char *line);
static int CheckQuantity(const void* valueptr, const optionTable_t* tableptr,
                         const char* filename, const char *line);
static int ReadUser(const void* valueptr, const optionTable_t* tableptr,
                    const char* filename, const char *line);
static int ReadPass(const void* valueptr, const optionTable_t* tableptr,
                    const char* filename, const char *line);
static int CheckAuctionFile(const void* valueptr, const optionTable_t* tableptr,
                            const char* filename, const char *line);
static int CheckConfigFile(const void* valueptr, const optionTable_t* tableptr,
                           const char* filename, const char *line);
static int CheckProxy(const void* valueptr, const optionTable_t* tableptr,
                     const char* filename, const char *line);
static int SetLongHelp(const void* valueptr, const optionTable_t* tableptr,
                       const char* filename, const char *line);
static int SetConfigHelp(const void* valueptr, const optionTable_t* tableptr,
                         const char* filename, const char *line);

/* called by CheckAuctionFile, CheckConfigFile */
static int CheckFile(const void* valueptr, const optionTable_t* tableptr,
                     const char* filename, const char *line,
                     const char *fileType);


#if !defined(WIN32)
static void
sigAlarm(int sig)
{
	signal(SIGALRM, sigAlarm);
	log((" SIGALRM"));
}
#endif

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
sortAuctions(auctionInfo **auctions, int numAuctions, char *user, int *quantity)
{
	int i, sawError = 0;

	for (i = 0; i < numAuctions; ++i) {
		int j;

		if (options.debug)
			logOpen(progname, auctions[i], options.logdir);
		for (j = 0; j < 3; ++j) {
			if (j > 0)
				printLog(stderr, "Retrying...\n");
			if (!getInfo(auctions[i], *quantity, user))
				break;
			printAuctionError(auctions[i], stderr);
			if (auctions[i]->auctionError == ae_unavailable) {
				--j;	/* doesn't count as an attempt */
				printLog(stderr, "%s: Will retry, sleeping for an hour\n", timestamp());
				sleep(3600);
			}
		}
		printLog(stdout, "\n");
	}
	if (numAuctions > 1) {
		printLog(stdout, "Sorting auctions...\n");
		/* sort by end time */
		qsort(auctions, (size_t)numAuctions,
		      sizeof(auctionInfo *), compareAuctionInfo);
	}

	/* get rid of obvious cases */
	for (i = 0; i < numAuctions; ++i) {
		auctionInfo *aip = auctions[i];

		if (aip->won > 0)
			*quantity -= aip->won;
		else if (aip->auctionError != ae_none)
			;
		else if (aip->remain == 0)
			;
		else if (!isValidBidPrice(aip))
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
 * CheckDebug(): convert boolean value, open of close log file
 *
 * returns: 0 = OK, else error
 */
static int
CheckDebug(const void* valueptr, const optionTable_t* tableptr,
           const char* filename, const char *line)
{
	int val = *((const int*)valueptr);

	val ? logOpen(progname, NULL, options.logdir) : logClose();
	*(int*)(tableptr->value) = val;
	log(("Debug mode is %s\n", val ? "on" : "off"));
	return 0;
}

/*
 * CheckSecs(): convert integer value or "now", check minimum value
 *
 * returns: 0 = OK, else error
 */
static int
CheckSecs(const void* valueptr, const optionTable_t* tableptr,
          const char* filename, const char *line)
{
   int intval;
   char *endptr;

   /* value specified? */
   if(!valueptr) {
      if(filename)
         printLog(stderr,
       "Configuration option \"%s\" in file %s needs an integer value or \"now\"\n",
                  line, filename);
      else
         printLog(stderr,
                  "Option -%s needs an integer value or \"now\"\n",
                  line);
   }
   /* specific string value "now" */
   if(!strcmp((const char*)valueptr, "now")) {
      /* copy value to target option */
      *(int*)(tableptr->value)=0;
      log(("seconds value is %d (now)", *(int*)(tableptr->value)));
      return 0;
   }

   /* else must be integer value */
   intval = strtol((const char*)valueptr, &endptr, 10);
   if(*endptr != '\0') {
      if(filename)
         printLog(stderr, "Configuration option \"%s\" in file %s", line, filename);
      else
         printLog(stderr, "Option -%s", line);
      printLog(stderr, "accepts integer values greater than %d or \"now\"\n",
               MIN_BIDTIME - 1);
      return 1;
   }
   /* check minimum */
   if(intval < MIN_BIDTIME) {
      if(filename)
         printLog(stderr, "Value at configuration option \"%s\" in file %s",
                  line, filename);
      else
         printLog(stderr, "Value %d at option -%s", intval, line);
      printLog(stderr, "too small, using minimum value of %d seconds\n",
               MIN_BIDTIME);
      intval = MIN_BIDTIME;
   }

   /* copy value to target option */
   *(int*)(tableptr->value) = intval;
   log(("seconds value is %d\n", *(const int*)(tableptr->value)));
   return 0;
}

/*
 * CheckPass(): set password
 *
 * returns: 0 = OK, else error
 */
static int
CheckPass(const void* valueptr, const optionTable_t* tableptr,
          const char* filename, const char *line)
{
   if(!valueptr) {
      if(filename)
         printLog(stderr,
                  "Invalid password at \"%s\" in file %s\n",
                  line, filename);
      else
         printLog(stderr,
                  "Invalid password at option -%s\n",
                  line);
      return 1;
   }
   setPassword(myStrdup((const char *)valueptr));
   log(("password has been set\n"));
   return 0;
}

/*
 * CheckQuantity(): convert integer value, check for positive value
 *
 * returns: 0 = OK, else error
 */
static int
CheckQuantity(const void* valueptr, const optionTable_t* tableptr,
              const char* filename, const char *line)
{
   if(*(const int*)valueptr <= 0) {
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
   /* copy value to target option */
   *(int*)(tableptr->value) = *(const int*)valueptr;
   log(("quantity is %d\n", *(const int*)(tableptr->value)));
   return 0;
}

/*
 * CheckUser(): set user
 *
 * returns: 0 = OK, else error
 */
static int
CheckUser(const void* valueptr, const optionTable_t* tableptr,
          const char* filename, const char *line)
{
   if(!valueptr) {
      if(filename)
         printLog(stderr,
                  "Invalid user at \"%s\" in file %s\n",
                  line, filename);
      else
         printLog(stderr,
                  "Invalid user at option -%s\n",
                  line);
      return 1;
   }
   setUsername(myStrdup((const char *)valueptr));
   log(("user has been set\n"));
   return 0;
}

/*
 * ReadUser(): read username from console
 *
 * note: not called by option processing code.  Called directly from main()
 *	if esniper has not been given username.
 *
 * returns: 0 = OK, else error
 */
static int
ReadUser(const void* valueptr, const optionTable_t* tableptr,
         const char* filename, const char *line)
{
	char *username = prompt("Enter eBay username: ", 0);

	if (!username) {
		printLog(stderr, "Username entry failed!\n");
		return 1;
	}

	setUsername(myStrdup(username));
	log(("username is %s\n", *(char**)(tableptr->value)));
	return 0;
}

/*
 * ReadPass(): read password from console
 *
 * returns: 0 = OK, else error
 */
static int
ReadPass(const void* valueptr, const optionTable_t* tableptr,
         const char* filename, const char *line)
{
	char *passwd = prompt("Enter eBay password: ", 1);

	if (!passwd) {
		printLog(stderr, "Password entry failed!\n");
		return 1;
	}
	putchar('\n');

	setPassword(passwd);
	/* don't log password! */
	return 0;
}

/*
 * CheckAuctionFile(): accept accessible files only
 *
 * returns: 0 = OK, else error
 */
static int CheckAuctionFile(const void* valueptr, const optionTable_t* tableptr,
                            const char* filename, const char *line)
{
   return CheckFile(valueptr, tableptr, filename, line, "Auction");
}

/*
 * CheckConfigFile(): accept accessible files only
 *
 * returns: 0 = OK, else error
 */
static int CheckConfigFile(const void* valueptr, const optionTable_t* tableptr,
                           const char* filename, const char *line)
{
   return CheckFile(valueptr, tableptr, filename, line, "Config");
}

/*
 * CheckFile(): accept accessible files only
 *
 * returns: 0 = OK, else error
 */
static int CheckFile(const void* valueptr, const optionTable_t* tableptr,
                     const char* filename, const char *line,
                     const char *filetype)
{
   if(access((const char*)valueptr, R_OK)) {
      printLog(stderr, "%s file \"%s\" is not readable: %s\n",
               filetype, nullStr((const char*)valueptr), strerror(errno));
      return 1;
   }
   free(*(char**)(tableptr->value));
   *(char**)(tableptr->value) = myStrdup(valueptr);
   return 0;
}

/*
 * CheckProxy(): accept accessible files only
 *
 * returns: 0 = OK, else error
 */
static int CheckProxy(const void* valueptr, const optionTable_t* tableptr,
                      const char* filename, const char *line)
{
   if (parseProxy((const char *)valueptr, (proxy_t *)(tableptr->value))) {
      if(filename)
         printLog(stderr,
                "Proxy specified in \"%s\" in file %s is not valid\n",
                  line, filename);
      else
         printLog(stderr,
                  "Proxy \"%s\" specified at option -%s is not valid\n",
                  nullStr((const char*)valueptr), line);
      return 1;
   }
   return 0;
}

/*
 * SetLongHelp(): set usage to 2 to activate long help
 *
 * returns: 0 = OK
 */
static int SetLongHelp(const void* valueptr, const optionTable_t* tableptr,
                       const char* filename, const char *line)
{
   /* copy value to target option */
   *(int*)(tableptr->value) |= USAGE_SUMMARY | USAGE_LONG;
   return 0;
}

/*
 * SetConfigHelp(): set usage to 3 to activate config file help
 *
 * returns: 0 = OK
 */
static int SetConfigHelp(const void* valueptr, const optionTable_t* tableptr,
                         const char* filename, const char *line)
{
   /* copy value to target option */
   *(int*)(tableptr->value) = USAGE_CONFIG;
   return 0;
}

static const char usageSummary[] =
  "usage: %s [-bdhHnPrUv] [-c conf_file] [-l logdir] [-p proxy] [-q quantity]\n"  "       [-s secs|now] [-u user] (auction_file | [auction price ...])\n"
  "\n";

/* split in two to prevent gcc portability warning.  maximum length is 509 */
static const char usageLong1[] =
 "where:\n"
 "-b: batch mode, don't prompt for password or username if not specified\n"
#if defined(WIN32)
 "-c: configuration file (default is \"My Documents/.esniper\" and, if auction\n"
#else
 "-c: configuration file (default is \"$HOME/.esniper\" and, if auction\n"
#endif
 "    file is specified, .esniper in auction file's directory)\n"
 "-d: write debug output to file\n"
 "-h: command line options help\n"
 "-H: configuration and auction file help\n"
 "-l: log directory (default: ., or directory of auction file, if specified)\n"
 "-n: do not place bid\n"
 "-p: http proxy (default: http_proxy environment variable, format is\n"
 "    http://host:port/)\n";
static const char usageLong2[] =
 "-P: prompt for password\n"
 "-q: quantity to buy (default is 1)\n"
 "-r: do not reduce quantity on startup if already won item(s)\n"
 "-s: time to place bid which may be \"now\" or seconds before end of auction\n"
 "    (default is %d seconds before end of auction)\n"
 "-u: ebay username\n"
 "-U: prompt for ebay username\n"
 "-v: print version and exit\n"
 "\n"
 "You must specify an auction file or <auction> <price> pair[s].  Options\n"
 "on the command line override settings in auction and configuration files.\n";

/* split in two to prevent gcc portability warning.  maximum length is 509 */
static const char usageConfig1[] =
 "Configuration options (values shown are default):\n"
 "  Boolean: (valid values: true,y,yes,on,1,enabled  false,n,no,off,0,disabled)\n"
 "    batch = false\n"
 "    bid = true\n"
 "    debug = false\n"
 "    reduce = true\n"
 "  String:\n"
 "    logdir = .\n"
 "    password =\n"
 "    proxy = <http_proxy environment variable, format is http://host:port/>\n"
 "    username =\n"
 "  Numeric: (seconds may also be \"now\")\n"
 "    quantity = 1\n"
 "    seconds = %d\n"
 "\n";
static const char usageConfig2[] =
 "A configuration file consists of option settings, blank lines, and comment\n"
 "lines.  Comment lines begin with #\n"
 "\n"
 "An auction file is similar to a configuration file, but it also has one or\n"
 "more auction lines.  An auction line contains an auction number, optionally\n"
 "followed by a bid price.  If no bid price is given, the auction number uses\n"
 "the bid price of the first prior auction line that contains a bid price.\n";

static int
usage(int helplevel)
{
	if (helplevel & USAGE_SUMMARY)
		fprintf(stderr, usageSummary, progname);
	if (helplevel & USAGE_LONG) {
		fprintf(stderr, usageLong1);
		fprintf(stderr, usageLong2, DEFAULT_BIDTIME);
	}
	if (helplevel & USAGE_CONFIG) {
		fprintf(stderr, usageConfig1, DEFAULT_BIDTIME);
		fprintf(stderr, usageConfig2);
	}
	if (helplevel == USAGE_SUMMARY)
		fprintf(stderr, "Try \"%s -h\" for more help.\n", progname);
	fprintf(stderr,"\n%s\n", blurb);
	return 1;
}

int
main(int argc, char *argv[])
{
	int ret = 1;	/* assume failure, change if successful */
	auctionInfo **auctions = NULL;
	int c, i, numAuctions = 0, numAuctionsOrig = 0;

   /* this table describes options and config entries */
   static optionTable_t optiontab[] = {
   {"username", "u", (void*)&options.username,     OPTION_STRING,   &CheckUser},
   {"password",NULL, (void*)&options.password,     OPTION_STRING,   &CheckPass},
   {"seconds",  "s", (void*)&options.bidtime,      OPTION_SPECIAL,  &CheckSecs},
   {"quantity", "q", (void*)&options.quantity,     OPTION_INT,  &CheckQuantity},
   {"proxy",    "p", (void*)&options.proxy,        OPTION_STRING,  &CheckProxy},
   {NULL,       "P", (void*)&options.password,     OPTION_STRING,   &ReadPass},
   {NULL,       "U", (void*)&options.username,     OPTION_STRING,   &ReadUser},
   {NULL,       "c", (void*)&options.conffilename, OPTION_STRING,   &CheckConfigFile},
   /*
    * -f can't be entered from command line, it's just a convenient way
    * to integrate auction filename processing with option processing.
    */
   {NULL,       "f", (void*)&options.auctfilename, OPTION_STRING,   &CheckAuctionFile},
   {"reduce",  NULL, (void*)&options.reduce,       OPTION_BOOL,     NULL},
   {NULL,       "r", (void*)&options.reduce,       OPTION_BOOL_NEG, NULL},
   {"bid",     NULL, (void*)&options.bid,          OPTION_BOOL,     NULL},
   {NULL,       "n", (void*)&options.bid,          OPTION_BOOL_NEG, NULL},
   {"debug",    "d", (void*)&options.debug,        OPTION_BOOL,    &CheckDebug},
   {"batch",    "b", (void*)&options.batch,        OPTION_BOOL,     NULL},
   {"logdir",   "l", (void*)&options.logdir,       OPTION_STRING,   NULL},
   {NULL,       "?", (void*)&options.usage,        OPTION_BOOL,     NULL},
   {NULL,       "h", (void*)&options.usage,        OPTION_STRING,   &SetLongHelp},
   {NULL,       "H", (void*)&options.usage,        OPTION_STRING,   &SetConfigHelp},
   {NULL, NULL, NULL, 0, NULL}
   };

	/* all known options */
	static const char optionstring[]="bc:dhHl:np:Pq:rs:u:Uv"
#if DEBUG
		"X"
#endif
		;

	atexit(cleanup);
	progname = basename(argv[0]);

	/* environment variables */
	/* TODO - obey no_proxy */
	if (parseProxy(getenv("http_proxy"), &options.proxy))
		printLog(stderr, "http_proxy environment variable invalid\n");

	/* first, check for debug, configuration file and auction file
	 * options but accept all other options to avoid error messages
	 */
	while ((c = getopt(argc, argv, optionstring)) != EOF) {
		switch (c) {
			/* Debug is in both getopt() sections, because we want
			 * debugging as soon as possible, and also because
			 * command line -d overrides settings in config files
			 */
		case 'd': /* debug */
		case 'h': /* command-line options help */
		case 'H': /* configuration and auction file help */
		case '?': /* unknown -> help */
			if (parseGetoptValue(c, NULL, optiontab))
				options.usage |= USAGE_SUMMARY;
			break;
		case 'c': /* configuration file */
		case 'l': /* log directory */
			if (parseGetoptValue(c, optarg, optiontab))
				options.usage |= USAGE_SUMMARY;
			break;
#if DEBUG
		case 'X': /* secret option - for testing page parsing */
			testParser();
			exit(0);
#endif
		case 'v': /* version */
			fprintf(stderr, "%s\n%s\n", version, blurb);
			exit(0);
			break;
		default:
			/* ignore other options, these will be parsed
			 * after configuration and auction files.
			 */
			break;
		}
	}

	if (options.usage)
		exit(usage(options.usage));

	/* One argument after options?  Must be an auction file. */
	if ((argc - optind) == 1) {
		if (parseGetoptValue('f', argv[optind], optiontab)) {
			options.usage |= USAGE_SUMMARY;
			exit(usage(options.usage));
		}
	}

	/*
	 * if configuration file is specified don't try to load any other
	 * configuration file (i.e. $HOME/.esniper, etc).
	 */
	if (options.conffilename) {
		if (readConfigFile(options.conffilename, optiontab) > 1)
			options.usage |= USAGE_SUMMARY;
	} else {
		/* TODO: on UNIX use getpwuid() to find the home dir? */
		char *homedir = getenv("HOME");
#if defined(WIN32)
		char *profiledir = getenv("USERPROFILE");

		if (profiledir && *profiledir) {
			/* parse $USERPROFILE/My Documents/.esniper */
			char *cfname = myStrdup3(profiledir,
						 "\\My Documents\\",
						 DEFAULT_CONF_FILE);

			switch (readConfigFile(cfname, optiontab)) {
			case 1: /* file not found */
				if (homedir && *homedir) {
					/* parse $HOME/.esniper */
					free(cfname);
					cfname = myStrdup3(homedir, "/",
							   DEFAULT_CONF_FILE);
					if (readConfigFile(cfname, optiontab) > 1)
						options.usage |= USAGE_SUMMARY;
				}
				break;
			case 0: /* OK */
				break;
			default: /* other error */
				options.usage |= USAGE_SUMMARY;
			}
			free(cfname);
		} else
			printLog(stderr, "Warning: environment variable USERPROFILE not set. Cannot parse $USERPROFILE/My Documents/%s.\n", DEFAULT_CONF_FILE);
#else
		if (homedir && *homedir) {
			/* parse $HOME/.esniper */
			char *cfname = myStrdup3(homedir,"/",DEFAULT_CONF_FILE);
			if (readConfigFile(cfname, optiontab) > 1)
				options.usage |= USAGE_SUMMARY;
			free(cfname);
		} else
			printLog(stderr, "Warning: environment variable HOME not set. Cannot parse $HOME/%s.\n", DEFAULT_CONF_FILE);
#endif

		if (options.auctfilename) {
			/* parse .esniper in auction file's directory */
			char *auctfilename = myStrdup(options.auctfilename);
			char *cfname = myStrdup3(dirname(auctfilename), "/", DEFAULT_CONF_FILE);

			if (readConfigFile(cfname, optiontab) > 1)
				options.usage |= USAGE_SUMMARY;
			free(auctfilename);
			free(cfname);
		}
	}

	/* parse auction file */
	if (options.auctfilename) {
		if (!options.logdir) {
			char *tmp = myStrdup(options.auctfilename);

			options.logdir = myStrdup(dirname(tmp));
			free(tmp);
		}
		if (readConfigFile(options.auctfilename, optiontab) > 1)
			options.usage |= USAGE_SUMMARY;
	}

	/* skip back to first arg */
	optind = 1;
	/*
	 * check options which may overwrite settings from configuration
	 * or auction file.
	 */
	while ((c = getopt(argc, argv, optionstring)) != EOF) {
		switch (c) {
		case 'l': /* log directory */
		case 'p': /* proxy */
		case 'q': /* quantity */
		case 's': /* seconds */
		case 'u': /* user */
			if (parseGetoptValue(c, optarg, optiontab))
				options.usage |= USAGE_SUMMARY;
			break;
			/* Debug is in both getopt() sections, because we want
			 * debugging as soon as possible, and also because
			 * command line -d overrides settings in config files
			 */
		case 'd': /* debug */
			if (options.debug)
				break;
			/* fall through */
		case 'b': /* batch */
		case 'n': /* don't bid */
		case 'P': /* read password */
		case 'r': /* reduce */
		case 'U': /* read username */
			if (parseGetoptValue(c, NULL, optiontab))
				options.usage |= USAGE_SUMMARY;
			break;
		default:
			/* ignore other options, these have been parsed
			 * before configuration and auction files.
			 */
			break;
		}
	}

	argc -= optind;
	argv += optind;

	/* don't log username/password */
	/*log(("options.username=%s\n", nullStr(options.username)));*/
	/*log(("options.password=%s\n", nullStr(options.password)));*/
	log(("options.bidtime=%d\n", options.bidtime));
	log(("options.quantity=%d\n", options.quantity));
	log(("options.conffilename=%s\n", nullStr(options.conffilename)));
	log(("options.auctfilename=%s\n", nullStr(options.auctfilename)));
	log(("options.bid=%d\n", options.bid));
	log(("options.reduce=%d\n", options.reduce));
	log(("options.debug=%d\n", options.debug));
	log(("options.usage=%d\n", options.usage));

	if (!options.usage) {
		if (!options.auctfilename && argc < 2) {
			printLog(stderr, "Error: no auctions specified.\n");
			options.usage |= USAGE_SUMMARY;
		}
		if (!options.auctfilename && argc % 2) {
			printLog(stderr, "Error: auctions and prices must be specified in pairs.\n");
			options.usage |= USAGE_SUMMARY;
		}
		if (!options.username) {
			if (options.batch) {
				printLog(stderr, "Error: no username specified.\n");
				options.usage |= USAGE_SUMMARY;
			} else if (!options.usage)
				parseGetoptValue('U', NULL, optiontab);
		}
		if (!options.password) {
			if (options.batch) {
				printLog(stderr, "Error: no password specified.\n");
				options.usage |= USAGE_SUMMARY;
			} else if (!options.usage)
				parseGetoptValue('P', NULL, optiontab);
		}
	}

	if (options.usage)
		exit(usage(options.usage));

	/* init variables */
	if (options.auctfilename) {
		numAuctions = readAuctionFile(options.auctfilename, &auctions);
	} else {
		numAuctions = argc / 2;
		auctions = (auctionInfo **)malloc(numAuctions * sizeof(auctionInfo *));
		for (i = 0; i < argc/2; i++)
			auctions[i] = newAuctionInfo(argv[2*i], argv[2*i+1]);
	}

	if (numAuctions <= 0)
		exit(usage(USAGE_SUMMARY));

#if !defined(WIN32)
	signal(SIGALRM, sigAlarm);
	signal(SIGHUP, SIG_IGN);
#endif
	signal(SIGTERM, sigTerm);

	numAuctionsOrig = numAuctions;
	{
		int quantity = options.quantity;
		numAuctions = sortAuctions(auctions, numAuctions,
					   options.username, &quantity);

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
		int retryCount, bidRet = 0;

		if (!auctions[i])
			continue;

		if (options.debug)
			logOpen(progname, auctions[i], options.logdir);

		log(("auction %s price %s quantity %d user %s bidtime %ld\n",
		     auctions[i]->auction, auctions[i]->bidPriceStr,
		     options.quantity, options.username, options.bidtime));

		if (numAuctionsOrig > 1)
			printLog(stdout, "\nNeed to win %d item(s), %d auction(s) remain\n\n", options.quantity, numAuctions - i);

		/* 0 means "now" */
		if (options.bidtime == 0) {
			if (preBid(auctions[i], &options)) {
				printAuctionError(auctions[i], stderr);
				continue;
			}
		} else {
			if (watch(auctions[i], &options)) {
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

		log(("*** BIDDING!!! auction %s price %s quantity %d\n",
			auctions[i]->auction, auctions[i]->bidPriceStr,
			options.quantity));

		for (retryCount = 0; retryCount < 3; retryCount++) {
			bidRet = bid(auctions[i], &options);
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
			sleep((unsigned int)options.bidtime + 1);
		}

		printLog(stdout, "\nAuction %s: Post-bid info:\n",
			 auctions[i]->auction);
		if (getInfo(auctions[i], options.quantity, options.username))
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
