/*
**  GETOPT PROGRAM AND LIBRARY ROUTINE
**
**  I wrote main() and AT&T wrote getopt() and we both put our efforts into
**  the public domain via mod.sources.
**	Rich $alz
**	Mirror Systems
**	(mirror!rs, rs@mirror.TMC.COM)
**	August 10, 1986
**
**  This is the public-domain AT&T getopt(3) code.  Hacked by Rich and by Jim.
*/

/*
 * Added to esniper to cover for Windows deficiency.  Unix versions of esniper
 * don't need to use this file!
 */

#include "getopt.h"
#include <stdio.h>
#include <string.h>

#define ERR(_s, _c) { if (opterr) fprintf (stderr, "%s%s%c\n", argv[0], _s, _c);}

int	opterr = 1;
int	optind = 1;
int	optopt;
char	*optarg;

int
getopt(int argc, char *const *argv, const char *opts)
{
	static int sp = 1;
	register int c;
	register char *cp;

	if(sp == 1) {
		if(optind >= argc ||
		   argv[optind][0] != '-' || argv[optind][1] == '\0')
			return(EOF);
		else if(strcmp(argv[optind], "--") == 0) {
			optind++;
			return(EOF);
		}
	}
	optopt = c = argv[optind][sp];
	if(c == ':' || (cp=strchr(opts, c)) == NULL) {
		ERR(": illegal option -- ", c);
		if(argv[optind][++sp] == '\0') {
			optind++;
			sp = 1;
		}
		return('?');
	}
	if(*++cp == ':') {
		if(argv[optind][sp+1] != '\0')
			optarg = &argv[optind++][sp+1];
		else if(++optind >= argc) {
			ERR(": option requires an argument -- ", c);
			sp = 1;
			return('?');
		} else
			optarg = argv[optind++];
		sp = 1;
	} else {
		if(argv[optind][++sp] == '\0') {
			sp = 1;
			optind++;
		}
		optarg = NULL;
	}
	return(c);
}
