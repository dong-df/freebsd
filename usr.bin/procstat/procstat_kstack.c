/*-
 * Copyright (c) 2007 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "procstat.h"

/*
 * Walk the stack trace provided by the kernel and reduce it to what we
 * actually want to print.  This involves stripping true instruction pointers,
 * frame numbers, and carriage returns as generated by stack(9).  If -kk is
 * specified, print the function and offset, otherwise just the function.
 */
enum trace_state { TS_FRAMENUM, TS_PC, TS_AT, TS_FUNC, TS_OFF };

static enum trace_state
kstack_nextstate(enum trace_state ts)
{

	switch (ts) {
	case TS_FRAMENUM:
		return (TS_PC);

	case TS_PC:
		return (TS_AT);

	case TS_AT:
		return (TS_FUNC);

	case TS_FUNC:
		return (TS_OFF);

	case TS_OFF:
		return TS_FRAMENUM;

	default:
		errx(-1, "kstack_nextstate");
	}
}

static void
kstack_cleanup(const char *old, char *new, int kflag)
{
	enum trace_state old_ts, ts;
	const char *cp_old;
	char *cp_new;

	ts = TS_FRAMENUM;
	for (cp_old = old, cp_new = new; *cp_old != '\0'; cp_old++) {
		switch (*cp_old) {
		case ' ':
		case '\n':
		case '+':
			old_ts = ts;
			ts = kstack_nextstate(old_ts);
			if (old_ts == TS_OFF) {
				*cp_new = ' ';
				cp_new++;
			}
			if (kflag > 1 && old_ts == TS_FUNC) {
				*cp_new = '+';
				cp_new++;
			}
			continue;
		}
		if (ts == TS_FUNC || (kflag > 1 && ts == TS_OFF)) {
			*cp_new = *cp_old;
			cp_new++;
		}
	}
	*cp_new = '\0';
}

/*
 * Sort threads by tid.
 */
static int
kinfo_kstack_compare(const void *a, const void *b)
{

        return ((struct kinfo_kstack *)a)->kkst_tid -
            ((struct kinfo_kstack *)b)->kkst_tid;
}

static void
kinfo_kstack_sort(struct kinfo_kstack *kkstp, int count)
{

        qsort(kkstp, count, sizeof(*kkstp), kinfo_kstack_compare);
}


void
procstat_kstack(pid_t pid, struct kinfo_proc *kipp, int kflag)
{
	struct kinfo_kstack *kkstp, *kkstp_free;
	char trace[KKST_MAXLEN];
	int error, i, name[4];
	size_t len;

	if (!hflag)
		printf("%5s %6s %-20s %-45s\n", "PID", "TID", "COMM",
		    "KSTACK");

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_KSTACK;
	name[3] = pid;

	len = 0;
	error = sysctl(name, 4, NULL, &len, NULL, 0);
	if (error < 0 && errno != ESRCH && errno != EPERM && errno != ENOENT) {
		warn("sysctl: kern.proc.kstack: %d", pid);
		return;
	}
	if (error < 0 && errno == ENOENT)
		errx(-1, "kern.proc.kstack sysctl unavailable; options DDB "
		    "is required.");
	if (error < 0)
		return;

	kkstp = kkstp_free = malloc(len);
	if (kkstp == NULL)
		err(-1, "malloc");

	if (sysctl(name, 4, kkstp, &len, NULL, 0) < 0) {
		warn("sysctl: kern.proc.pid: %d", pid);
		free(kkstp);
		return;
	}

	kinfo_kstack_sort(kkstp, len / sizeof(*kkstp));
	for (i = 0; i < len / sizeof(*kkstp); i++) {
		kkstp = &kkstp_free[i];
		printf("%5d ", pid);
		printf("%6d ", kkstp->kkst_tid);
		printf("%-20s ", kipp->ki_comm);

		switch (kkstp->kkst_state) {
		case KKST_STATE_RUNNING:
			printf("%-45s\n", "<running>");
			continue;

		case KKST_STATE_SWAPPED:
			printf("%-45s\n", "<swapped>");
			continue;

		case KKST_STATE_STACKOK:
			break;

		default:
			printf("%-45s\n", "<unknown>");
			continue;
		}

		/*
		 * The kernel generates a trace with carriage returns between
		 * entries, but for a more compact view, we convert carriage
		 * returns to spaces.
		 */
		kstack_cleanup(kkstp->kkst_trace, trace, kflag);
		printf("%-45s\n", trace);
	}
	free(kkstp_free);
}
