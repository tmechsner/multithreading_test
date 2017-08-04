/* 
 * ===========================================================================
 * 
 * printer_management.h --
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
 *
 * 
 * ===========================================================================
 */

#ifndef _PRINTER_MANAGEMENT_H_
#define _PRINTER_MANAGEMENT_H_

extern int
printer_exists(unsigned int printer_no);

extern int
open_printer(unsigned int printer_no);

extern int
close_printer(int prt_fd);

extern int
print_char(int prt_fd, char c);

extern char*
string_append(char *base, char *extension);

#endif
