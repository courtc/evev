// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2017 Courtney Cavin

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>
#include <fnmatch.h>
#include <errno.h>
#include <spawn.h>
#include <glob.h>
#include <err.h>

#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <linux/input.h>

#include "context.h"
#include "parser.h"
#include "tables.h"
#include "types.h"
#include "expr.h"

#define DEV_INPUT "/dev/input"
#define DEF_CFG PREFIX_ETC "/evev"

extern char **environ;

enum {
	FLAG_INFO	= (1 << 0),
	FLAG_MONITOR	= (1 << 1),
	FLAG_LOGGING	= (1 << 2),
	FLAG_QUIET	= (1 << 3),
};

static int execute(const char *command)
{
	char *const args[] = {
		"/bin/sh", "-c", (char *)command, NULL
	};
	pid_t pid;

	return posix_spawn(&pid, args[0], NULL, NULL, args, environ);
}

static void mon_input_event(struct input_event *ev)
{
	const char *codep = NULL;
	const char *typep = NULL;
	char code[14];
	char type[14];

	if (ev->type < nametab_sz)
		typep = nametab[ev->type].name;

	if (typep == NULL) {
		sprintf(type, "%d", ev->code);
		typep = type;
	}

	if (nametab[ev->type].tab != NULL)
		codep = nametab[ev->type].tab[ev->code];

	if (codep == NULL) {
		sprintf(code, "%d", ev->code);
		codep = code;
	}

	printf("%s %s %d\n", typep, codep, ev->value);
}

#define MAX_EV_CNT ((KEY_CNT + 31) / 32)

static int bitstate(u32 *buf, int bit)
{
	return (buf[bit / 32] & (1 << (bit % 32))) != 0;
}

static int open_evdev(const char *evdev, char **names, int nnames,
		int flags, struct context *ctx)
{
	char dphys[128];
	char dname[128];
	int match = nnames == 0;
	int fd;
	int rc;

	fd = open(evdev, O_RDONLY);
	if (fd == -1) {
		if ((flags & FLAG_QUIET) == 0)
			warn(evdev);
		return -1;
	}

	rc = ioctl(fd, EVIOCGPHYS(sizeof(dphys) - 1), dphys);
	if (rc < 1) {
		close(fd);
		return -1;
	}

	rc = ioctl(fd, EVIOCGNAME(sizeof(dname) - 1), dname);
	if (rc < 1) {
		close(fd);
		return -1;
	}

	for (unsigned int i = 0; i < nnames; ++i) {
		const char *pattern;
		const char *text;
		int pflags = 0;

		if (!strncmp(names[i], "phys=", 5)) {
			pattern = names[i] + 5;
			text = dphys;
			pflags = FNM_PATHNAME;
		} else if (!strncmp(names[i], "name=", 5)) {
			pattern = names[i] + 5;
			text = dname;
		} else if (!strncmp(names[i], "dev=", 4)) {
			pattern = names[i] + 4;
			text = evdev;
			pflags = FNM_PATHNAME;
		} else {
			pattern = names[i];
			text = evdev;
			pflags = FNM_PATHNAME;
		}

		if (!fnmatch(pattern, text, pflags)) {
			match = 1;
			break;
		}
	}

	if (flags & FLAG_INFO) {
		fprintf(stderr, "%s: phys=\"%s\" name=\"%s\" match=%s\n",
				evdev, dphys, dname, match ? "yes" : "no");
	}

	if ((flags & FLAG_MONITOR) == 0 && match && ctx) {
		u32 states[MAX_EV_CNT];
		u32 buf[MAX_EV_CNT];
		int type = -1;

		match = 0;

		for (unsigned int i = 0; i < ctx->nstates; ++i) {
			struct evstate *evs = &ctx->states[i];

			int ctype = evs->typecode >> 16;
			int ccode = evs->typecode & 0xffff;

			if (ctype != type) {
				unsigned int len = MAX_EV_CNT * sizeof(states[0]);
				unsigned long ioc;

				memset(buf, 0, sizeof(buf));
				rc = ioctl(fd, EVIOCGBIT(ctype, len), buf);
				if (rc < 1)
					err(1, "EVIOCGBIT");

				type = ctype;
				switch (type) {
				case EV_SW:  ioc = EVIOCGSW(len); break;
				case EV_KEY: ioc = EVIOCGKEY(len); break;
				case EV_SND: ioc = EVIOCGSND(len); break;
				case EV_LED: ioc = EVIOCGLED(len); break;
				default: ioc = 0; break;
				}

				if (ioc != 0) {
					memset(states, 0, sizeof(states));
					rc = ioctl(fd, ioc, states);
					if (rc < 1)
						err(1, "EVIOCGstate");
				}
			}

			if (!bitstate(buf, ccode))
				continue;

			match = 1;
			switch (type) {
			case EV_SW:
			case EV_KEY:
			case EV_SND:
			case EV_LED:
				evs->value = bitstate(states, ccode);
				break;
			case EV_ABS: {
				struct input_absinfo ainfo;
				rc = ioctl(fd, EVIOCGABS(ccode), &ainfo);
				if (rc < 0)
					err(1, "EVIOCGABS");
				evs->value = ainfo.value;
				} break;
			default:
				break;
			}
		}

		if (!match && nnames != 0)
			warnx("%s: no relevant events", evdev);
	}

	if (!match) {
		close(fd);
		return -1;
	}

	return fd;
}

static u64 time_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (u64)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void epoll_add(int efd, int fd)
{
	struct epoll_event ev = {0,};
	int rc;

	ev.events = EPOLLIN;
	ev.data.fd = fd;
	rc = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
	if (rc == -1)
		err(1, "epoll_ctl");
}

static void evev(char **names, int nnames, int flags,
		const char *cfg, const char *cfgtext)
{
	struct epoll_event events[10];
	struct binding **pbindings;
	struct binding *bindings;
	struct context ctx;
	int polltime;
	glob_t gr;
	int wfd;
	int ifd;
	int efd;
	int rc;
	int fd;

	bindings = NULL;
	pbindings = &bindings;

	if (nnames == 0 && (flags & FLAG_QUIET) == 0)
		warnx("no input evdevs specified, resorting to all");

	if (flags & FLAG_MONITOR) {
		rc = GLOB_NOMATCH;
	} else if (cfgtext) {
		*pbindings = psr_parse(cfgtext);
		if (*pbindings == NULL)
			errx(1, "<cmdline>: failed parsing");
		pbindings = &(*pbindings)->next;

		rc = GLOB_NOMATCH;
		if (cfg)
			rc = glob(cfg, 0, NULL, &gr);
	} else if (cfg) {
		rc = glob(cfg, 0, NULL, &gr);
	} else {
		rc = glob(DEF_CFG "/*.cfg", 0, NULL, &gr);
	}

	if (rc == GLOB_NOSPACE)
		errx(1, "glob: out of memory");
	if (rc == GLOB_ABORTED)
		errx(1, "glob: read error");
	if (rc != GLOB_NOMATCH) {
		for (unsigned int i = 0; gr.gl_pathv[i]; ++i) {
			struct stat st;
			char *mem;

			fd = open(gr.gl_pathv[i], O_RDONLY);
			if (fd == -1)
				err(1, gr.gl_pathv[i]);

			rc = fstat(fd, &st);
			if (rc == -1)
				err(1, gr.gl_pathv[i]);

			mem = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
			if (mem == MAP_FAILED)
				err(1, gr.gl_pathv[i]);

			*pbindings = psr_parse(mem);
			if (*pbindings == NULL)
				errx(1, "%s: failed parsing", gr.gl_pathv[i]);

			pbindings = &(*pbindings)->next;

			munmap(mem, st.st_size);

			close(fd);
		}
	}

	globfree(&gr);

	if (bindings == NULL && (flags & FLAG_MONITOR) == 0)
		errx(1, "no configs loaded; exiting");

	ctx_init(&ctx, bindings);

	efd = epoll_create1(0);
	if (efd == -1)
		err(1, "epoll_create1");

	ifd = inotify_init1(IN_NONBLOCK);
	if (ifd == -1)
		err(1, "inotify_init1");

	wfd = inotify_add_watch(ifd, DEV_INPUT, IN_CREATE | IN_ONLYDIR);
	if (wfd == -1)
		err(1, DEV_INPUT);

	epoll_add(efd, ifd);

	rc = glob(DEV_INPUT "/event*", 0, NULL, &gr);
	if (rc == GLOB_NOSPACE)
		errx(1, "glob: out of memory");
	if (rc == GLOB_ABORTED)
		errx(1, "glob: read error");
	if (rc != GLOB_NOMATCH) {
		for (unsigned int i = 0; gr.gl_pathv[i]; ++i) {
			fd = open_evdev(gr.gl_pathv[i], names, nnames, flags, &ctx);
			if (fd == -1)
				continue;

			epoll_add(efd, fd);
		}
	}

	globfree(&gr);

	if (flags & FLAG_MONITOR)
		polltime = -1;
	else
		polltime = ctx_timeout(&ctx, execute, time_ms());

	for (;;) {
		int nfds;

		nfds = epoll_wait(efd, events, ARRAY_SIZE(events), polltime);
		if (nfds == -1 && errno != EINTR)
			err(1, "epoll_wait");

		if (nfds <= 0) {
			if ((flags & FLAG_MONITOR) == 0)
				polltime = ctx_timeout(&ctx, execute, time_ms());
			continue;
		}

		polltime = -1;
		for (unsigned int i = 0; i < nfds; ++i) {
			int len;

			fd = events[i].data.fd;
			if (fd == ifd) {
				struct inotify_event *ev;
				char buffer[4096];

				ev = (struct inotify_event *)buffer;
				rc = read(fd, buffer, sizeof(*ev));
				if (rc != sizeof(*ev))
					errx(1, "short read");

				if (!ev->len)
					continue;

				len = snprintf(buffer, sizeof(buffer),
						"%s/", DEV_INPUT);
				rc = read(fd, buffer + len, ev->len);
				if (rc != ev->len)
					errx(1, "short read");
				buffer[rc + len] = 0;

				fd = open_evdev(buffer, names, nnames, flags, NULL);
				if (fd == -1)
					continue;

				epoll_add(efd, fd);
			} else {
				struct input_event ev;

				rc = read(fd, &ev, sizeof(ev));
				if (rc != sizeof(ev))
					errx(1, "short read");

				if (ev.type == EV_KEY && ev.value == 2) {
					/* ignore key repeat */
				} else if (flags & FLAG_MONITOR) {
					mon_input_event(&ev);
				} else {
					u64 now;

					if (flags & FLAG_LOGGING)
						mon_input_event(&ev);

					now = (u64)ev.time.tv_sec * 1000 +
							ev.time.tv_usec / 1000;

					rc = ctx_input_event(&ctx, execute,
						expr_typecode(ev.type, ev.code),
						ev.value, now);

					if (rc > 0 && (polltime < 0 || rc < polltime))
						polltime = rc;
				}
			}
		}
	}
}

static void handle_sigchld(int sig)
{
	wait(NULL);
}

static struct sigaction sigchld_ign_nowait = {
	.sa_handler = handle_sigchld,
};

static void version(const char *name)
{
	fprintf(stderr, "-- version 0.1 --\n");
}

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s OPTIONS <event...>\n\n", name);
	fprintf(stderr,
		"   <event...> can be a pattern in the form of:\n"
		"       name=<device name>  (e.g name='AT Keyboard')\n"
		"       phys=<device phys>  (e.g phys='isa0060/input[0-9]')\n"
		"       dev=<device file>   (e.g dev=/dev/input/event0)\n"
		"       <device file>       (e.g /dev/input/event0)\n"
		"   Options:\n"
		"	-m        monitor mode\n"
		"	-l        enable logging\n"
		"	-I        output information about event devices\n"
		"	-c <cfg>  config location (pattern)\n"
		"	-e <txt>  inline configuration\n"
		"	-q        disable non-fatal errors and warnings\n"
		"	-h        this cruft\n"
		"	-v        version info\n"
		"\n"
	);
	version(name);
}

int main(int argc, char **argv)
{
	const char *cfgtext = NULL;
	const char *cfg = NULL;
	int flags = 0;
	int rc;

	while ((rc = getopt(argc, argv, "hvmlIc:e:q")) != -1) {
		switch (rc) {
		case 'h':
			usage(argv[0]);
			return 0;
		case 'v':
			version(argv[0]);
			return 0;
		case 'm':
			flags |= FLAG_MONITOR;
			break;
		case 'l':
			flags |= FLAG_LOGGING;
			break;
		case 'I':
			flags |= FLAG_INFO;
			break;
		case 'q':
			flags |= FLAG_QUIET;
			break;
		case 'c':
			cfg = optarg;
			break;
		case 'e':
			cfgtext = optarg;
			break;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	if ((flags & FLAG_MONITOR) == 0) {
		sigaction(SIGCHLD, &sigchld_ign_nowait, NULL);
	} else {
		if (cfg) {
			warnx("-m & -c are mutually exclusive; try -l");
			usage(argv[0]);
			return -1;
		}

		if (cfgtext) {
			warnx("-m & -e are mutually exclusive; try -l");
			usage(argv[0]);
			return -1;
		}

		if (flags & FLAG_LOGGING) {
			warnx("-m & -l are mutually exclusive");
			usage(argv[0]);
			return -1;
		}
	}

	evev(argv + optind, argc - optind, flags, cfg, cfgtext);

	return 0;
}
