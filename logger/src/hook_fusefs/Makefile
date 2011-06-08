all: hookfs

hookfs: hookfs.c
	$(CC) -std=c99 -Wall `pkg-config fuse --cflags --libs` -lulockmgr $(CFLAGS) $(LDFLAGS) hookfs.c -o hookfs

clean:
	rm -f hookfs
