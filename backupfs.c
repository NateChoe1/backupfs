#define FUSE_USE_VERSION 31

#include <fuse.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#include <unistd.h>

/* I stole this code directly from hello.c */
static struct options {
	char *pattern;
	char *devname;
	int show_help;
	int uid;
	int gid;
} options;
#define OPTION(t, p)                           \
	{ t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--pattern=%s", pattern),
	OPTION("--devname=%s", devname),
	OPTION("--uid=%d", uid),
	OPTION("--gid=%d", gid),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

static void show_help(char *name) {
	fprintf(stderr,
"Usage: %s --pattern=[pattern] --devname=[device name]\n"
"          --uid=[device uid] --gid=[device gid]\n"
"The pattern specifies the path that backups get stored in. Everything is\n"
"pretty much echoed except for these exceptions:\n"
"    %%Y - the current year padded to 4 digits\n"
"    %%M - the current month padded to 2 digits\n"
"    %%D - the current day padded to 2 digits\n"
"    %%h - the current hour padded to 2 digits\n"
"    %%m - the current minute padded to 2 digits\n"
"    %%s - the current second padded to 2 digits\n"
"    %%u - A random UUID\n",
		name);
}

static int *fds;
static size_t fdalloc;
static int randfd;

static FILE *logs;

static int genfd() {
	time_t currtime = time(NULL);
	struct tm *broken = localtime(&currtime);
	char buff[1000];
	int written = 0;
	const char *digstr = "0123456789";
	const char *hexstr = "0123456789abcdef";

#define APPEND(c) \
	if (written >= sizeof buff) { \
		goto end; \
	} \
	buff[written++] = c
#define APPEND_NUM(num, digits) \
	do { \
		char numstr[digits]; \
		for (int d = 0; d < digits; ++d) { \
			numstr[d] = digstr[num % 10]; \
			num /= 10; \
		} \
		for (int d = digits-1; d >= 0; --d) { \
			APPEND(numstr[d]); \
		} \
	} while (0)
#define APPEND_HEX(byte) \
	do { \
		int low, high; \
		high = (byte >> 4) & 0x0f; \
		low = byte & 0x0f; \
		APPEND(hexstr[high]); \
		APPEND(hexstr[low]); \
	} while (0)
	for (int i = 0; options.pattern[i]; ++i) {
		char c = options.pattern[i];
		if (c != '%') {
			APPEND(c);
			continue;
		}
		c = options.pattern[++i];
		switch (c) {
		case 'Y':
			int year = broken->tm_year + 1900;
			APPEND_NUM(year, 4);
			break;
		case 'M':
			int month = broken->tm_mon + 1;
			APPEND_NUM(month, 2);
			break;
		case 'D':
			int day = broken->tm_mday;
			APPEND_NUM(day, 2);
			break;
		case 'h':
			int hour = broken->tm_hour;
			APPEND_NUM(hour, 2);
			break;
		case 'm':
			int min = broken->tm_min;
			APPEND_NUM(min, 2);
			break;
		case 's':
			int sec = broken->tm_sec;
			APPEND_NUM(sec, 2);
			break;
		case 'u':
			unsigned char bytes[16];
			if (read(randfd, bytes, sizeof bytes) < sizeof bytes) {
				return -EAGAIN;
			}
			for (int p = 0; p < sizeof bytes; ++p) {
				if (p == 4 || p == 6 || p == 8) {
					APPEND('-');
				}
				APPEND_HEX(bytes[p]);
			}
			break;
		}
	}
#undef APPEND_HEX
#undef APPEND_NUM
#undef APPEND
end:
	buff[written] = '\0';

	fprintf(logs, "Opening %s\n", buff);

	return open(buff, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

static int backupfs_getattr(const char *path, struct stat *st) {
	fprintf(logs, "getattr(): %s\n", path);
	memset(st, 0, sizeof *st);
	if (strcmp(path, "/") == 0) {
		st->st_nlink = 2;
		/* User and group can read, write, and search */
		st->st_mode = S_IFDIR | 0770;
		st->st_uid = options.uid;
		st->st_gid = options.gid;
		return 0;
	}
	if (path[0] == '/' && strcmp(path+1, options.devname) == 0) {
		st->st_nlink = 1;
		/* User and group can only write */
		st->st_mode = S_IFREG | 0220;
		st->st_uid = options.uid;
		st->st_gid = options.gid;
		st->st_size = 0;
		return 0;
	}
	return -ENOENT;
}

static int backupfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi) {
	fprintf(logs, "readdir(): %s\n", path);
	if (strcmp(path, "/") != 0) {
		return -ENOENT;
	}
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, options.devname, NULL, 0);
	return 0;
}

static int backupfs_open(const char *path, struct fuse_file_info *fi) {
	fprintf(logs, "open(): %s\n", path);
	if (path[0] != '/' || strcmp(path+1, options.devname) != 0) {
		return -ENOENT;
	}

	if ((fi->flags & O_ACCMODE) != O_WRONLY) {
		return -EACCES;
	}

	long newid;

	for (newid = 0; newid < fdalloc; ++newid) {
		if (fds[newid] == -1) {
			goto foundid;
		}
	}
	size_t newalloc = fdalloc;
	int *newfds;
	newfds = realloc(fds, newalloc * sizeof *fds);
	if (newfds == NULL) {
		return -EAGAIN;
	}
	newid = fdalloc;
	for (long i = fdalloc; i < newalloc; ++i) {
		newfds[i] = -1;
	}
	fds = newfds;
	fdalloc = newalloc;
foundid:

	if ((fds[newid] = genfd()) == -1) {
		return -errno;
	}
	fi->fh = newid;
	fi->direct_io = 1;

	return 0;
}

static int backupfs_write(const char *path, const char *data, size_t len,
		off_t offset, struct fuse_file_info *fi) {
	fprintf(logs, "write(): %s\n", path);
	if (fi->fh >= fdalloc || fds[fi->fh] < 0) {
		return -EINVAL;
	}
	ssize_t ret = write(fds[fi->fh], data, len);
	return ret == -1 ? -errno : ret;
}

static int backupfs_flush(const char *path, struct fuse_file_info *fi) {
	fprintf(logs, "flush(): %s\n", path);
	return fsync(fds[fi->fh]) == 0 ? 0 : -errno;
}

static int backupfs_release(const char *path, struct fuse_file_info *fi) {
	fprintf(logs, "release(): %s\n", path);
	return close(fds[fi->fh]) == 0 ? 0 : -errno;
}

static int backupfs_truncate(const char *path, off_t newsz) {
	fprintf(logs, "truncate(): %s\n", path);
	return 0;
}

static const struct fuse_operations ops = {
	.getattr = backupfs_getattr,
	.readdir = backupfs_readdir,
	.open = backupfs_open,
	.write = backupfs_write,
	.flush = backupfs_flush,
	.release = backupfs_release,
	.truncate = backupfs_truncate,
};

int main(int argc, char *argv[]) {
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	options.pattern = NULL;
	options.devname = strdup("dev");
	options.show_help = options.uid = options.gid = 0;

	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) {
		return 1;
	}

	if (options.show_help) {
		show_help(argv[0]);
		return 1;
	}

	if (options.pattern == NULL) {
		show_help(argv[0]);
		return 1;
	}

	fdalloc = 10;
	fds = malloc(fdalloc * sizeof *fds);
	for (int i = 0; i < fdalloc; ++i) {
		fds[i] = -1;
	}

#ifdef REAL_LOGS
	int logfd = dup(2);
	logs = fdopen(logfd, "w");
#else
	logs = stderr;
#endif

	if ((randfd = open("/dev/random", O_RDONLY)) < 0) {
		fputs("Failed to open /dev/random\n", stderr);
		return 1;
	}

	int ret = fuse_main(args.argc, args.argv, &ops, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
