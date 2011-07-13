file_hook.so: file_hook.o
	ld -shared -o file_hook.so -ldl -lc file_hook.o

file_hook.o: file_hook.c
	cc -Wall -fPIC -o file_hook.o -c file_hook.c

all: file_hook.so

clean:
	rm -f file_hook.o file_hook
