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

#ifndef GETOPT_H_INCLUDED
#define GETOPT_H_INCLUDED

extern int	opterr;
extern int	optind;
extern int	optopt;
extern char	*optarg;

extern int getopt(int argc, char *const *argv, const char *opts);

#endif /* GETOPT_H_INCLUDED */
