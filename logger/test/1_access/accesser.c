#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int main(int argc, char **argv) {
  if(argc<=1) {
	printf("Usage: accesser.c <file1> [file2] [file3] ...\n");
	return 1;
  }
  int i;
  for(i=1;i<argc;i++) {
	printf("Accessing %s: ",argv[i]);
	int fh;
	fh=open(argv[i], O_RDONLY);
	if(fh!=-1)
	  printf("OK\n");
	else 
	  printf("ERR, %s\n", strerror(errno));
  }
  
  return 0;
}
