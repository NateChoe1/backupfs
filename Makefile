backup: backupfs.c
	$(CC) $(CFLAGS) -Wall -Wpedantic -Wextra -Werror $< -o $@ $(shell pkg-config --libs --cflags fuse)
