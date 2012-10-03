/*
 * eltakoMS parses and logs the RS485 serial datastream of the
 * Eltako Multisensor weather sensor (temperature, direct sunlight from 3
 * directions, dawning, obscurity, wind, rain). A datasheet can be found on
 * http://www.eltako.com/dwl/Multisensor%20MS_4801_internet_gb.pdf .

 * Incoming datagrams
 *    W+07.6016300N99901.2N?151515151515?1889
 *  are converted to a more human readable format and get timestamped
 *    2008-04-03 17:03:20 t+07.6s01w63e00od999v01.2r
 * t[+-]\d\d\.\d : Temperature; degree celsius
 *  s\d\d         : sunlight from South; 00 to 99
 *  w\d\d         : sunlight from West; 00 to 99
 *  e\d\d         : sunlight from East; 00 to 99
 * [oO]          : Obscurity; uppercase when pitch black
 *  d\d\d\d       : Dawning; from 000 (dark) to 999 (bright)
 *  v\d\d\.\d     : Velocity of wind; meter per second
 *  [rR]          : Rain; uppercase when raining
 *
 *  It also writes the current data to a file in /dev/shm/ for easy pickup by
 *  other programs like an snmp-agent or rrdtool.
 *
 *  This software is copyright 2008 by Frank Sautter
 *  (eltakoms~at~sautter~dot~com)
 *
 *  This software is based on the quante.c serial logging software copyright
 * 1996 by Harald Milz (ht~at~seneca~dot~muc~dot~de)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 675
 * Mass Ave, Cambridge, MA 02139, USA.
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <syslog.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include "config.h"
#include "version.h"

/* select which terminal handling to use (currently only SysV variants) */
#if defined(HAVE_TERMIOS)
#include <termios.h>
#define TTY_GETATTR(_FD_, _ARG_) tcgetattr((_FD_), (_ARG_))
#define TTY_SETATTR(_FD_, _ARG_) tcsetattr((_FD_), TCSANOW, (_ARG_))
#endif

#if defined(HAVE_TERMIO)
#include <termio.h>
#include <sys/ioctl.h>
#define TTY_GETATTR(_FD_, _ARG_) ioctl((_FD_), TCGETA, (_ARG_))
#define TTY_SETATTR(_FD_, _ARG_) ioctl((_FD_), TCSETAW, (_ARG_))
#endif

#ifndef TTY_GETATTR
MUST DEFINE ONE OF "HAVE_TERMIOS" or "HAVE_TERMIO"
#endif

#define LINELEN 150
#define DEFTTY  "/dev/ttyS1"
#define DEFLOG  "/usb/log"
#define DEFSHM  "/dev/shm"

void usage (char *prog) {
  printf ("usage: %s [ -V ] [ -s ] [ -l <logfile> ] [ -f <device> ]\n", prog);
  printf ("\tdefault device: "DEFTTY"\n");
  printf ("\t-l <logfile>\tuse specified <logfile>\n");
  printf ("\t-i <interval>\tuse specified <interval> for logging\n");
  printf ("\t-s\tuse syslog instead of logfile\n");
  printf ("\t-V\tprint version and exit\n");
}


/* must be global variables */
char lock[80] = LOCKPATH;
int fd;
int use_syslog = 0; /* default: use logfile */

void closefiles () {
  signal (SIGTERM, SIG_IGN);
  signal (SIGHUP, SIG_IGN);
  signal (SIGINT, SIG_IGN);
  signal (SIGQUIT, SIG_IGN);
  if (use_syslog) {
    syslog (LOG_INFO, "caught signal, exiting");
    closelog();
  } else {
    close (fd);
  }
  unlink (lock);
  exit (0);
}

void copyright (char *prog) {
  printf ("%s ver %s\n", prog, VERSION);
  printf ("Copyright (c) 2008 Frank Sautter; (c) 1996 Harald Milz\n");
  exit (0);
}


int main (int argc, char **argv) {

#if defined(HAVE_TERMIOS) || defined(STREAM)
  struct termios term;
#endif
#if defined(HAVE_TERMIO) || defined(HAVE_SYSV_TTYS)
  struct termio term;
#endif
  char buf[LINELEN];
  char device[LINELEN] = DEFTTY;
  char logf[LINELEN];
  char shmf[LINELEN];
  char proc[21];
  int interval = 60;
  int pid;
  FILE *lockfile;
  FILE *logfile;
  FILE *procfile;
  FILE *shmfile;
  fd_set fds;
  char c;
  char *s;
  char *program = argv[0];
  char *tty;
  time_t ltime, epoch;
  char datestr[80];
  char bufcpy[LINELEN];
  int autoname = 1;
  int err, i, sum, count, temp, wind, dawn, sunS, sunE, sunW, avgTemp,
    maxWind, avgDawn, avgSunS, avgSunE, avgSunW;
  long oldEpoch, accDawn, accSunE, accSunW, accSunS, accTemp;
  char rain, obsc, maxRain, maxObsc;

  /* remove the dirpath from the program name */
  if ((s = strrchr (program, '/')))
    program = ++s;

  while ((c = getopt (argc, argv, "f:l:i:tsV")) != 255) {
    switch (c) {
      case 'f':
        strcpy (device, optarg);
        break;
      case 'V':
        copyright(program);
        break;
      case 's':
        use_syslog = 1;
        break;
      case 'l':
        strcpy (logf, optarg);
        use_syslog = 0;
        autoname = 0;
        break;
      case 'i':
        interval = atoi(optarg);
        if (interval < 10) {
          printf ("interval to short.\n");
          exit (1);
        }
        break;
      default:
        printf ("found %i\n", c);
        usage (program);
        exit (1);
        break;
    } /* switch () */
  } /* while getopt */

  tty = ((s = strrchr (device, '/'))) ? ++s : device; 
  sprintf (shmf, "%s/%s-%s", DEFSHM, program, tty);

  if (autoname)
    sprintf (logf, "%s/%s-%s.log", DEFLOG, program, tty);

  if (use_syslog) {
    openlog (program, LOG_PID, LOG_LOCAL5);
  } else {
    if ((logfile = fopen (logf, "a")) == NULL) {
      fprintf (stderr, "cannot open %s for logging\n", logf);
      perror ("fopen");
      exit (1);
    }
    fclose (logfile);
  }
	
  /* check for a valid UUCP lock file */
  s = strrchr (device, '/');
  s++;
  strcat (lock, "/LCK..");
  strcat (lock, s);
  if ((lockfile = fopen (lock, "r")) != NULL ) { /* does exist */
    fscanf (lockfile, "%11d", &pid);
    sprintf (proc, "/proc/%d/cmdline", pid);
    if ((procfile = fopen (proc, "r")) == NULL) { /* process doesn't exist */
      fprintf (stderr, "stale lockfile exists: %s, pid %d\n", lock, pid);
      if (use_syslog) {
        syslog (LOG_ERR, "stale lockfile exists: %s, pid %d", lock, pid);
      }
      fclose (lockfile);
      unlink (lock);
    } else {
      fprintf (stderr, "valid lockfile exists: %s, pid %d\n", lock, pid);
      if (use_syslog) {
        syslog (LOG_ERR, "valid lockfile exists: %s, pid %d", lock, pid);
      }
      fclose (lockfile);
      exit (2);
    }
  }
	
  /* create new PID file */
  lockfile = fopen (lock, "w");
  fprintf (lockfile, "%11d", getpid());
  fclose (lockfile);
	
  /* open the serial device */
  if ((fd = open (device, O_RDONLY | O_NDELAY)) == -1) {
    if (use_syslog)
      syslog (LOG_ERR, "cannot open %s", device);

    fprintf (stderr, "cannot open %s\n", device);
    perror ("open");
    exit (1);
  }
  fcntl (fd, F_SETFL, O_RDONLY);
  if (use_syslog)
    syslog (LOG_INFO, "startup, logging from %s into %s\n", device, logf);
  else
    fprintf (stderr, "startup, reading from %s into %s\n", device, logf);

  /* setup TTY (19200 bps, 8N1) */
  if (TTY_GETATTR(fd,  &term) == -1) {
    if (use_syslog)
      syslog (LOG_ERR, "error in tcgetattr");

    perror("tcgetattr");
    exit(1);
  }

  memset(term.c_cc, 0, sizeof(term.c_cc));
  term.c_cc[VMIN] = 1;             /* read ONE character */
  term.c_cc[VTIME] = 5;            /* read timeout 5/10 sec */
  term.c_cflag = B19200|CS8|CREAD|CLOCAL|CRTSCTS;
  term.c_iflag = IGNBRK;
  term.c_oflag = 0;
  term.c_lflag = 0;

  if (TTY_SETATTR(fd, &term) == -1) {
    if (use_syslog)
      syslog (LOG_ERR, "error in tcsetattr");

    perror("tcsetattr");
    exit(1);
  }

  signal (SIGTERM, closefiles);
  signal (SIGHUP, closefiles);
  signal (SIGINT, closefiles);
  signal (SIGQUIT, closefiles);

  /* endless loop */

  count = maxRain = maxWind = accDawn = maxObsc = accSunE = accSunW = accSunS = accTemp = 0;
  oldEpoch = -1;
  while (1) {
    FD_ZERO (&fds);
    FD_SET (fd, &fds);
		
    select (FD_SETSIZE, &fds, NULL, NULL, NULL);
    if (FD_ISSET (fd, &fds)) {
      s = buf;
      err = 0;
      do {
        read (fd, s++, 1);
      } while (*(s-1) != 0x03 && (s-buf) < LINELEN );			// end marking read?

      *(s) = '\0';							// terminate string
      strncpy(bufcpy,buf,LINELEN-1);

      /* sanity checks */

      if ( (s - buf) != 40 )                      err |= 0x0001;	// incorrect length
      if ( *(buf)    != 'W')                      err |= 0x0002;	// no 'W' on 1
      if ( *(buf+ 1) != '+' && *(buf+ 1) != '-' ) err |= 0x0004;	// no '+' or '-' on pos 2

      if ( *(buf+ 2) <  '0' || *(buf+ 2) >  '9' ) err |= 0x0008;	// no numeric on pos 3
      if ( *(buf+ 3) <  '0' || *(buf+ 3) >  '9' ) err |= 0x0008;	// no numeric on pos 4
      if ( *(buf+ 4) != '.' )                     err |= 0x0008;	// no '.' on pos 5
      if ( *(buf+ 5) <  '0' || *(buf+ 5) >  '9' ) err |= 0x0008;	// no numeric on pos 6

      if ( *(buf+ 6) <  '0' || *(buf+ 6) >  '9' ) err |= 0x0010;	// no numeric on pos 7
      if ( *(buf+ 7) <  '0' || *(buf+ 7) >  '9' ) err |= 0x0010;	// no numeric on pos 8

      if ( *(buf+ 8) <  '0' || *(buf+ 8) >  '9' ) err |= 0x0020;	// no numeric on pos 9
      if ( *(buf+ 9) <  '0' || *(buf+ 9) >  '9' ) err |= 0x0020;	// no numeric on pos 10

      if ( *(buf+10) <  '0' || *(buf+10) >  '9' ) err |= 0x0040;	// no numeric on pos 11
      if ( *(buf+11) <  '0' || *(buf+11) >  '9' ) err |= 0x0040;	// no numeric on pos 12

      if ( *(buf+12) != 'J' && *(buf+12) != 'N' ) err |= 0x0080;	// no 'J' or 'N' on pos 13

      if ( *(buf+13) <  '0' || *(buf+13) >  '9' ) err |= 0x0100;	// no numeric on pos 14
      if ( *(buf+14) <  '0' || *(buf+14) >  '9' ) err |= 0x0100;	// no numeric on pos 15
      if ( *(buf+15) <  '0' || *(buf+15) >  '9' ) err |= 0x0100;	// no numeric on pos 16

      if ( *(buf+16) <  '0' || *(buf+16) >  '9' ) err |= 0x0200;	// no numeric on pos 17
      if ( *(buf+17) <  '0' || *(buf+17) >  '9' ) err |= 0x0200;	// no numeric on pos 18
      if ( *(buf+18) != '.' )                     err |= 0x0200;	// no '.' on pos 19
      if ( *(buf+19) <  '0' || *(buf+19) >  '9' ) err |= 0x0200;	// no numeric on pos 20

      if ( *(buf+20) != 'J' && *(buf+20) != 'N' ) err |= 0x0400;	// no 'J' or 'N' on pos 21

      if ( *(buf+21) != '?' )                     err |= 0x0800;	// no '?' on pos 22
      if ( *(buf+22) != '1' )                     err |= 0x0800;	// no '1' on pos 23
      if ( *(buf+23) != '5' )                     err |= 0x0800;	// no '5' on pos 24
      if ( *(buf+24) != '1' )                     err |= 0x0800;	// no '1' on pos 25
      if ( *(buf+25) != '5' )                     err |= 0x0800;	// no '5' on pos 26
      if ( *(buf+26) != '1' )                     err |= 0x0800;	// no '1' on pos 27
      if ( *(buf+27) != '5' )                     err |= 0x0800;	// no '5' on pos 28
      if ( *(buf+28) != '1' )                     err |= 0x0800;	// no '1' on pos 29
      if ( *(buf+29) != '5' )                     err |= 0x0800;	// no '5' on pos 30
      if ( *(buf+30) != '1' )                     err |= 0x0800;	// no '1' on pos 31
      if ( *(buf+31) != '5' )                     err |= 0x0800;	// no '5' on pos 32
      if ( *(buf+32) != '1' )                     err |= 0x0800;	// no '1' on pos 33
      if ( *(buf+33) != '5' )                     err |= 0x0800;	// no '5' on pos 34
      if ( *(buf+34) != '?' )                     err |= 0x0800;	// no '?' on pos 35

      if ( *(buf+35) <  '0' || *(buf+35) >  '9' ) err |= 0x1000;	// no numeric on pos 36
      if ( *(buf+36) <  '0' || *(buf+36) >  '9' ) err |= 0x1000;	// no numeric on pos 37
      if ( *(buf+37) <  '0' || *(buf+37) >  '9' ) err |= 0x1000;	// no numeric on pos 38
      if ( *(buf+38) <  '0' || *(buf+38) >  '9' ) err |= 0x1000;	// no numeric on pos 39

      for (i=0, sum=0; i<35; i++) sum += *(buf+i);
      if ((err & 0x1000) || sum != atoi(buf+35))  err |= 0x2000;	// errorneous checksum 

      ltime = time(NULL);						// get current calendar time

      if (err == 0) {
	rain = (*(buf+20) == 'J') ? 'R' : 'r';
        maxRain = (rain > maxRain) ? rain : maxRain;
        *(buf+20) = '\0';
        wind = (int)(atof(buf+16)*10);
        maxWind = (wind > maxWind) ? wind : maxWind;
        *(buf+16) = '\0';
        dawn = atoi(buf+13);
        accDawn += dawn;
        obsc = (*(buf+12) == 'J') ? 'O' : 'o';
        maxObsc = (obsc > maxObsc) ? obsc : maxObsc;
        *(buf+12) = '\0';
        sunE = atoi(buf+10);
        accSunE += sunE;
        *(buf+10) = '\0';
        sunW = atoi(buf+8);
        accSunW += sunW;
        *(buf+8) = '\0';
        sunS = atoi(buf+6);
        accSunS += sunS;
        *(buf+6) = '\0';
        temp = (int)(atof(buf+1)*10);
        accTemp += temp;
	count++;

        shmfile = fopen (shmf, "w");
        fprintf (shmfile, "t%+05.1fs%2.2dw%2.2de%2.2d%cd%3.3dv%04.1f%c\n"
                          "Temperature : %+.1f\n"
                          "Sun South   : %d\n"
                          "Sun West    : %d\n"
                          "Sun East    : %d\n"
                          "Obscure     : %c\n"
                          "Dawn        : %d\n"
                          "Wind        : %.1f\n"
                          "Rain        : %c\n", 
                 (float)temp/10, sunS, sunW, sunE, obsc, dawn, (float)wind/10, rain,
                 (float)temp/10, sunS, sunW, sunE, obsc, dawn, (float)wind/10, rain);
        fclose (shmfile);
        
	
        if (ltime % interval < oldEpoch) {
	  epoch = (ltime % interval <= interval * .05 ) ? (time_t)((long)(ltime/interval)*interval) : ltime;
          strftime(datestr,80,"%F %H:%M:%S",localtime(&epoch));
          avgDawn = (int)(accDawn / count + .5);
          avgSunE = (int)(accSunE / count + .5);
          avgSunW = (int)(accSunW / count + .5);
          avgSunS = (int)(accSunS / count + .5);
          avgTemp = (int)(accTemp / count + .5);
          if (use_syslog) {
            syslog (LOG_INFO, "ELTAKO-MS: t%+05.1fs%2.2dw%2.2de%2.2d%cd%3.3dv%04.1f%c", (float)avgTemp/10, avgSunS, avgSunW, avgSunE, maxObsc, avgDawn, (float)maxWind/10, maxRain);
          } else {
//debug     printf ("%s t%+05.1fs%2.2dw%2.2de%2.2d%cd%3.3dv%04.1f%c %04x %s\n", datestr, (float)avgTemp/10, avgSunS, avgSunW, avgSunE, maxObsc, avgDawn, (float)maxWind/10, maxRain, err, bufcpy);
            logfile = fopen (logf, "a");
            fprintf (logfile, "%s t%+05.1fs%2.2dw%2.2de%2.2d%cd%3.3dv%04.1f%c\n", datestr, (float)avgTemp/10, avgSunS, avgSunW, avgSunE, maxObsc, avgDawn, (float)maxWind/10, maxRain);
            fclose (logfile);
          } /* if (use_syslog) */
          count = maxRain = maxWind = accDawn = maxObsc = accSunE = accSunW = accSunS = accTemp = 0;
        } /* if (ltime .. */
        oldEpoch = ltime % interval;
      } else {
        syslog (LOG_INFO, "ELTAKO-MS: Error 0x%04x reading sensordata: %s", err, bufcpy);
      } /* if (err ...) */
    } /* if (FD_ISSET ...) */
  } /* while (1) */
  /* NEVER REACHED */
} /* main () */


