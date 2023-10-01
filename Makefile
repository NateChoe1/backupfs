backup: backupfs.c
	$(CC) -Wall $< -o $@ $(shell pkg-config --libs --cflags fuse)
