/* 
 * ===========================================================================
 * 
 * printer_management.c
 * 
 * 
 * Ralf Moeller
 * 
 *    Copyright (C) 2006
 *    Computer Engineering Group
 *    Faculty of Technology
 *    University of Bielefeld
 *    www.ti.uni-bielefeld.de
 * 
 * 1.0 / 16. Aug 06 (rm)
 * - from scratch
 * 1.1 / 23. Aug 06 (rm)
 * 1.2 / 04. Aug 17 (tm)
 * - Added flag for switching tty paths to make it work under OS X
 * ===========================================================================
 */

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "printer_management.h"

#ifdef OSX
char tty_path[] = "/dev/ttys00%d";
#else
char tty_path[] = "/dev/pts/%d";
#endif

/* returns 1 (exists) or 0 (does not exist) */
int
printer_exists(unsigned int printer_no)
{
  char filename[MAX_CANON];
  struct stat statbuf;

  sprintf(filename, tty_path, printer_no);
  if (stat(filename, &statbuf) == -1)
    return 0;
  return S_ISCHR(statbuf.st_mode);
}

/* returns a file id or -1 */
int
open_printer(unsigned int printer_no)
{
  char filename[MAX_CANON];

  if (!printer_exists(printer_no))
    return -1;
  sprintf(filename, tty_path, printer_no);
  return open(filename, O_WRONLY);
}

/* close */
int
close_printer(int prt_fd)
{
  return(close(prt_fd));
}

/* returns 1 (success) or negative error code (no success) */
int
print_char(int prt_fd, char c)
{
  int res, i;

  if (c == '\f') {
    /* form feed: write dashed line */
    for (i = 0; i < 30; i++) {
      res = write(prt_fd, "- ", 2);
      if (res != 2) return -1;
    }
    res = write(prt_fd, "\n", 1);
    if (res != 1) return -1;
  }
  else {
    /* any other character */
    res = write(prt_fd, &c, 1);
    if (res != 1) return -1;
  }
  /* very slow printer */
  usleep(100000);
  return 1;
}

/* appends extension to base, returns address of resulting string */
/* base argument can be NULL */
/* caller needs to free reallocated space */
char*
string_append(char *base, char *extension)
{
  int len;
  if (base) {
    /* base is non-NULL, realloc and append */
    len = strlen(base) + strlen(extension) + 1;
    base = realloc(base, len);
    strcat(base, extension);
  }
  else {
    /* base is NULL, alloc and copy */
    base = malloc(strlen(extension) + 1);
    strcpy(base, extension);
  }
  return base;
}


