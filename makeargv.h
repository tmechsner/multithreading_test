/* header file for .c-file from  Robbins & Robbins: Unix Systems Programming */

#ifndef _MAKEARGV_H_
#define _MAKEARGV_H_

extern int makeargv(const char *s, const char *delimiters, char ***argvp);
extern void freemakeargv(char **argv);

#endif

