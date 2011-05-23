#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <dlfcn.h>

#define _FCNTL_H
#include <bits/fcntl.h>


//extern int errorno;

int (*_open)(const char * pathname, int flags, ...);
int (*_open64)(const char * pathname, int flags, ...);

FILE *log_file_handle;

void _init() {
  _open = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open");
  _open64 = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open64");

  if(_open==NULL || _open64==NULL) {
	  fprintf(stderr,"Failed to load original functions of hook\n");
	  exit(1);
  }
  
  char *log_file_name=getenv("FILE_LOG");
  if(log_file_name==NULL) {
	fprintf(stderr,"Using stderr as output for logs "
					"because the FILE_LOG environment variable isn't defined.\n");
	log_file_handle=stderr;
  } else {
	log_file_handle=fopen(log_file_name,"a+");
	if(log_file_handle==NULL) {
	  fprintf(stderr,"Failed to open log file %s: %s\n", log_file_name, strerror(errno));
	  exit(1);
	}
  }
} 

void _fini() {
  fclose(log_file_handle);
}

/*
 * Prints a string escaping spaces and '\'
 * Does not check input variables
*/
void __print_escaped(FILE *fh ,const char *s){
	for(;(*s)!=0; s++) {
		if(*s==' ')
		  fprintf(fh,"\\ ");
		else if(*s=='\\')
		  fprintf(fh,"\\\\");
		else
		  fprintf(fh,"%c", *s);
	}
}

/*
 * Format of log string: time event file flags result parents
*/
void __hook_log(const char *event_type, const char *filename,int flags, int result, int err) {

  fprintf(log_file_handle,"%lld ",(unsigned long long)time(NULL));

  __print_escaped(log_file_handle, event_type);
  fprintf(log_file_handle," ");
  __print_escaped(log_file_handle, filename);
  fprintf(log_file_handle," %d %d %d", flags, result, err);
  // TODO: add a parent processes in output
  
  
  fprintf(log_file_handle,"\n");
}

int open(const char * pathname, int flags, mode_t mode) {
    int ret;
    if(flags & O_CREAT)
        ret=_open(pathname, flags, mode);
    else
        ret=_open(pathname, flags, 0);

    __hook_log("open",pathname,flags,ret,errno);

	return ret;
}

int open64(const char * pathname, int flags, mode_t mode) {
	int ret;
	
    if(flags & O_CREAT)
        ret=_open64(pathname, flags, mode);
    else
        ret=_open64(pathname, flags, 0);
	
	__hook_log("open64",pathname,flags,ret,errno);
	
	return ret;
}

//int execve(const char *filename, char *const argv[],
//                  char *const envp[]) {
  //printf("FORK!!!!(canceled)");
//  return NULL;
//}
