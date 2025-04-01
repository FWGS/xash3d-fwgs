#pragma once
#ifndef GETOPT_H
#define GETOPT_H
#if XASH_WIN32
extern int	 opterr;
extern int	 optind;
extern int	 optopt;
extern int	 optreset;
extern char	*optarg;

int		 getopt( int nargc, char * const nargv[], const char *ostr );
#endif // XASH_WIN32
#endif // GETOPT_H
