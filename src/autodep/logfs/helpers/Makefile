all: proc_helpers.so

proc_helpers.so: proc_helpers.c
	$(CC) -shared -fPIC -Wall `python-config --cflags` `python-config --ldflags` $(CFLAGS) $(LDFLAGS) proc_helpers.c -o ../proc_helpers.so

clean:
	rm -f ../proc_helpers.so
