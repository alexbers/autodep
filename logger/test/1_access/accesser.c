#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char **argv) {
  if(argc<=1) {
	printf("Usage: accesser.c <file1> [file2] [file3] ...\n");
  }
  return 0; 
}
