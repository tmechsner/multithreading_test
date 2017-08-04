# multithreading_test
Small server program simulating a print server.
Written to get familiar with concurrency in C.

## Summary
After starting the server you can connect to it using e. g. telnet.
Then use commands to print files on other opened terminals, cancel them, get print job information etc.
A "page" has five lines, ever printed page costs 5 cent. Every successful or canceled job receives an invoice.

## Compile
Compile with "./mkps". Remove option "-DOSX" if you want to build it for linux (OSX is default).

## Run
Run the server with "./print_server portnr".
Connect to the server e. g. by "telnet locahost portnr".
Open some further terminals so that you have something to print on.
You can get the number of the "printer" e. g. with command "tty".
Output of "tty" for some terminal on my mac: "/dev/ttys002" -> printer number is 2.

## Commands
- print printer_no filename - creates a print job for printer "printer_no" to print the given file. Returns a job number (unique per client).
- status job_no - returns the status of the given job.
- cancel job_no - cancel the job with the given number.
- invoice job_no - returns the invoice for the given job (5 cent per one page a 5 lines).
- jobs printer_no - lists all jobs and their status for the given printer.
- quit - cancels all jobs of this client and quits the connection.
