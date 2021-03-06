/*	$NetBSD: script.c,v 1.12 2006/06/14 16:05:38 liamjfoy Exp $	*/

/*  GNU port by pancake <pancake@youterm.com> */

/*
 * Copyright (c) 1980, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define PKGNAME "yt-script"

#include <sys/cdefs.h>
#if 0
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1980, 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n");
#endif /* not lint */
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)script.c	8.1 (Berkeley) 6/6/93";
__RCSID("$NetBSD: script.c,v 1.12 2006/06/14 16:05:38 liamjfoy Exp $");
#endif
#endif /* not lint */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/uio.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
//#include <tzfile.h>
#include <unistd.h>
//#include <util.h>

#define	DEF_BUF	65536
#ifndef uint64_t
#define uint64_t long long
#endif
#ifndef uint32_t
#define uint32_t int
#endif
#define SECSPERMIN 60

struct stamp {
	uint64_t scr_len;	/* amount of data */
	uint64_t scr_sec;	/* time it arrived in seconds... */
	uint32_t scr_usec;	/* ...and microseconds */
	uint32_t scr_direction;	/* 'i', 'o', etc (also indicates endianness) */
};

FILE	*fscript;
int	master, slave;
int	child, subchild;
int	outcc;
int	usesleep, rawout;
char	*fname;

struct	termios tt;

void	done(void);
void	dooutput(void);
void	doshell(void);
void	fail(void);
void	finish(int);
int	main(int, char **);
void	scriptflush(int);
void	record(FILE *, char *, size_t, int);
void	playback(FILE *);

#ifndef __NetBSD__
uint64_t
bswap64(uint64_t x) {
	u_int32_t tl, th;
	 
	th = bswap32((u_int32_t)(x & 0x00000000ffffffffULL));
	tl = bswap32((u_int32_t)((x >> 32) & 0x00000000ffffffffULL));
	return ((u_int64_t)th << 32) | tl;
}

uint32_t
bswap32(uint32_t x) {
	return  ((x << 24) & 0xff000000 ) |
		((x <<  8) & 0x00ff0000 ) |
		((x >>  8) & 0x0000ff00 ) |
		((x >> 24) & 0x000000ff );
}
#endif

void
show_usage()
{
	(void)fprintf(stderr,
		"Usage: %s [-adh] [-pr file]\n"
		"  -a    append output to file or typescript\n"
		"  -d    don't sleep when playing\n"
		"  -h    shows this help message\n"
		"  -p    playback recorded session\n"
		"  -r    record a session\n"
		"  -V    show version information\n"
	, PKGNAME);
}

int
main(int argc, char *argv[])
{
	int cc;
	struct termios rtt;
	struct winsize win;
	int aflg, pflg, ch;
	char ibuf[BUFSIZ];

	aflg = 0;
	pflg = 0;
	usesleep = 1;
	rawout = 0;
	while ((ch = getopt(argc, argv, "hadprV")) != -1)
		switch(ch) {
		case 'a':
			aflg = 1;
			break;
		case 'd':
			usesleep = 0;
			break;
		case 'p':
			pflg = 1;
			break;
		case 'r':
			rawout = 1;
			break;
		case 'V':
			puts(VERSION);
			exit(0);
			break;
		case '?':
		case 'h':
		default:
			show_usage();
			exit(0);
		}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		fname = argv[0];
	else {
		show_usage();
		exit(1);
	}

	if ((fscript = fopen(fname, pflg ? "r" : aflg ? "a" : "w")) == NULL)
		err(1, "fopen %s", fname);

	if (pflg)
		playback(fscript);

	(void)tcgetattr(STDIN_FILENO, &tt);
	(void)ioctl(STDIN_FILENO, TIOCGWINSZ, &win);
	if (openpty(&master, &slave, NULL, &tt, &win) == -1)
		err(1, "openpty");

	(void)printf("Script started, output file is %s\n", fname);
	rtt = tt;
	cfmakeraw(&rtt);
	rtt.c_lflag &= ~ECHO;
	(void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &rtt);

	(void)signal(SIGCHLD, finish);
	child = fork();
	if (child < 0) {
		warn("fork");
		fail();
	}
	if (child == 0) {
		subchild = child = fork();
		if (child < 0) {
			warn("fork");
			fail();
		}
		if (child)
			dooutput();
		else
			doshell();
	}

	if (!rawout)
		(void)fclose(fscript);
	while ((cc = read(STDIN_FILENO, ibuf, BUFSIZ)) > 0) {
		if (rawout)
			record(fscript, ibuf, cc, 'i');
		(void)write(master, ibuf, cc);
	}
	done();
	/* NOTREACHED */
	return (0);
}

void
finish(int signo)
{
	int die, pid, status;

	die = 0;
	while ((pid = wait3(&status, WNOHANG, 0)) > 0)
		if (pid == child)
			die = 1;

	if (die)
		done();
}

void
dooutput()
{
	struct itimerval value;
	int cc;
	time_t tvec;
	char obuf[BUFSIZ];

	(void)close(STDIN_FILENO);
	tvec = time(NULL);
	if (rawout)
		record(fscript, NULL, 0, 's');
	else
		(void)fprintf(fscript, "Script started on %s", ctime(&tvec));

	(void)signal(SIGALRM, scriptflush);
	value.it_interval.tv_sec = SECSPERMIN / 2;
	value.it_interval.tv_usec = 0;
	value.it_value = value.it_interval;
	(void)setitimer(ITIMER_REAL, &value, NULL);
	for (;;) {
		cc = read(master, obuf, sizeof (obuf));
		if (cc <= 0)
			break;
		(void)write(1, obuf, cc);
		if (rawout)
			record(fscript, obuf, cc, 'o');
		else
			(void)fwrite(obuf, 1, cc, fscript);
		outcc += cc;
	}
	done();
}

void
scriptflush(int signo)
{
	if (outcc) {
		(void)fflush(fscript);
		outcc = 0;
	}
}

void
doshell()
{
	char *shell;

	shell = getenv("SHELL");
	if (shell == NULL)
		shell = _PATH_BSHELL;

	(void)close(master);
	(void)fclose(fscript);
	login_tty(slave);
	execl(shell, shell, "-i", NULL);
	warn("execl %s", shell);
	fail();
}

void
fail()
{

	(void)kill(0, SIGTERM);
	done();
}

void
done()
{
	time_t tvec;

	if (subchild) {
		tvec = time(NULL);
		if (rawout)
			record(fscript, NULL, 0, 'e');
		else
			(void)fprintf(fscript,"\nScript done on %s",
			    ctime(&tvec));
		(void)fclose(fscript);
		(void)close(master);
	} else {
		(void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &tt);
		(void)printf("Script done, output file is %s\n", fname);
	}
	exit(0);
}

void
record(FILE *fscript, char *buf, size_t cc, int direction)
{
	struct iovec iov[2];
	struct stamp stamp;
	struct timeval tv;

	(void)gettimeofday(&tv, NULL);
	stamp.scr_len = cc;
	stamp.scr_sec = tv.tv_sec;
	stamp.scr_usec = tv.tv_usec;
	stamp.scr_direction = direction;
	iov[0].iov_len = sizeof(stamp);
	iov[0].iov_base = &stamp;
	iov[1].iov_len = cc;
	iov[1].iov_base = buf;
	if (writev(fileno(fscript), &iov[0], 2) == -1)
		err(1, "writev");
}

#define swapstamp(stamp) do { \
	if (stamp.scr_direction > 0xff) { \
		stamp.scr_len = bswap64(stamp.scr_len); \
		stamp.scr_sec = bswap64(stamp.scr_sec); \
		stamp.scr_usec = bswap32(stamp.scr_usec); \
		stamp.scr_direction = bswap32(stamp.scr_direction); \
	} \
} while (0/*CONSTCOND*/)

void
playback(FILE *fscript)
{
	struct timespec tsi, tso;
	struct stamp stamp;
	struct stat playback_stat;
	char buf[DEF_BUF];
	off_t nread, save_len;
	size_t l;
	time_t clock;

	if (fstat(fileno(fscript), &playback_stat) == -1)
		err(1, "fstat failed");	

	for (nread = 0; nread < playback_stat.st_size; nread += save_len) {
		if (fread(&stamp, sizeof(stamp), 1, fscript) != 1)
			err(1, "reading playback header");
		swapstamp(stamp);
		save_len = sizeof(stamp);

		if (stamp.scr_len >
		    (uint64_t)(playback_stat.st_size - save_len) - nread)
			err(1, "invalid stamp");

		save_len += stamp.scr_len;
		clock = stamp.scr_sec;
		tso.tv_sec = stamp.scr_sec;
		tso.tv_nsec = stamp.scr_usec * 1000;

		switch (stamp.scr_direction) {
		case 's':
			(void)printf("Script started on %s", ctime(&clock));
			tsi = tso;
			fseek(fscript, stamp.scr_len, SEEK_CUR);
			break;
		case 'e':
			(void)printf("\nScript done on %s", ctime(&clock));
			fseek(fscript, stamp.scr_len, SEEK_CUR);
			break;
		case 'i':
			/* throw input away */
			fseek(fscript, stamp.scr_len, SEEK_CUR);
			break;
		case 'o':
			tsi.tv_sec = tso.tv_sec - tsi.tv_sec;
			tsi.tv_nsec = tso.tv_nsec - tsi.tv_nsec;
			if (tsi.tv_nsec < 0) {
				tsi.tv_sec -= 1;
				tsi.tv_nsec += 1000000000;
			}
			if (usesleep)
				(void)nanosleep(&tsi, NULL);
			tsi = tso;
			while (stamp.scr_len > 0) {
				l = MIN(DEF_BUF, stamp.scr_len);
				if (fread(buf, sizeof(char), l, fscript) != l)
					err(1, "cannot read buffer");

				(void)write(STDOUT_FILENO, buf, l);
				stamp.scr_len -= l;
			}
			break;
		default:
			err(1, "invalid direction");
		}
	}
	(void)fclose(fscript);
	exit(0);
}
