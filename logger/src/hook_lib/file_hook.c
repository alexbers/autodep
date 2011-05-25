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

#include <sys/socket.h>
#include <sys/un.h>

#define MAXPATHLEN 256
#define MAXSOCKETPATHLEN 108
#define MAXFILEBUFFLEN 2048

//extern int errorno;

int (*_open)(const char * pathname, int flags, ...);
int (*_open64)(const char * pathname, int flags, ...);
int (*_execve)(const char *filename, char *const argv[],char *const envp[]);

FILE *log_file_handle; // one of these two vars will be used for logging
int log_socket=-1;

int is_log_into_socket=0;

void _init() {
  _open = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open");
  _open64 = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open64");
  _execve = (int (*)(const char *filename, char *const argv[],char *const envp[])) dlsym(RTLD_NEXT, "execve");

  if(_open==NULL || _open64==NULL || execve==NULL) {
	  fprintf(stderr,"Failed to load original functions of hook\n");
	  exit(1);
  }
  
  
  char *log_socket_name=getenv("LOG_SOCKET");
  if(log_socket_name==NULL) {
	fprintf(stderr,"Using stderr as output for logs "
				   "because the LOG_SOCKET environment variable isn't defined.\n");

	log_file_handle=stderr;
  } else {
	is_log_into_socket=1;
	
	if(strlen(log_socket_name)>=MAXSOCKETPATHLEN) {
	  fprintf(stderr,"Unable to create a unix-socket %s: socket name is too long,exiting\n", log_socket_name);
	  exit(1);
	}
	
	fprintf(stderr,"Using a socket for logging: %s\n",log_socket_name);
	
	log_socket=socket(AF_UNIX, SOCK_STREAM, 0);
	if(log_socket==-1) {
	  fprintf(stderr,"Unable to create a unix-socket %s: %s\n", log_socket_name, strerror(errno));
	  exit(1);
	}
	
	struct sockaddr_un serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sun_family = AF_UNIX;
	strcpy(serveraddr.sun_path, log_socket_name);
	
	int ret=connect(log_socket, (struct sockaddr *)&serveraddr, SUN_LEN(&serveraddr));
	if(ret==-1) {
	  fprintf(stderr,"Unable to connect a unix-socket: %s\n", strerror(errno));
	  exit(1);
	}
	
	log_file_handle=fdopen(log_socket,"r+");
	
	if(log_file_handle==NULL) {
	  fprintf(stderr,"Unable to open a socket for a steam writing: %s\n", strerror(errno));
	  exit(1);
	}
  }
} 

void _fini() {
  fflush(log_file_handle);
  fclose(log_file_handle);

  if(is_log_into_socket)
	close(log_socket); 
  
  //fprintf(stderr,"All sockets closed\n");
}

/*
 * Prints a string escaping spaces and '\'
 * Does not check input variables
*/
void __print_escaped(FILE *fh ,const char *s){
	for(;(*s)!=0; s++) {
		if(*s==' ')
		  fprintf(fh,"\\ ");
		if(*s==',')
		  fprintf(fh,"\\,");
		else if(*s=='\r')
		  fprintf(fh,"\\r");
		else if(*s=='\n')
		  fprintf(fh,"\\n");
		else if(*s=='\\')
		  fprintf(fh,"\\\\");
		else
		  fprintf(fh,"%c", *s);
	}
}

/*
 *  Fprint 
*/

//void __fprint

/*
 * Get a pid of the parent proccess
 * Parse the /proc/pid/stat
 * We need a first number after last ')' character
*/
pid_t __getparentpid(pid_t pid){
  char filename[MAXPATHLEN];
  snprintf(filename,MAXPATHLEN, "/proc/%d/stat",pid);
  FILE *stat_file_handle=fopen(filename,"r");
  if(stat_file_handle==NULL) {
	fprintf(log_file_handle,"NULL");
	return 0;
  }
  
  char filedata[MAXFILEBUFFLEN];
  size_t bytes_readed=fread(filedata,sizeof(char),MAXFILEBUFFLEN,stat_file_handle);
  if(bytes_readed==0 || bytes_readed>=MAXFILEBUFFLEN) {
	fprintf(log_file_handle,"NULL");
	fclose(stat_file_handle);
	return 0;	
  }
  
  filedata[bytes_readed]=0;
  
  char *beg_scan_offset=rindex(filedata,')');
  if(beg_scan_offset==NULL) {
	fprintf(log_file_handle,"NULL");
	fclose(stat_file_handle);
	return 0;	
  }
  
  pid_t parent_pid;
  int tokens_readed=sscanf(beg_scan_offset,") %*c %d",&parent_pid);
  if(tokens_readed!=1) {
	fprintf(log_file_handle,"NULL");
	fclose(stat_file_handle);
	return 0;
  }
  fclose(stat_file_handle);
  
  if(pid==1)
	return 0; // set this explicitly. 
	//           I am not sure that ppid of init proccess is always 0
  
  return parent_pid;
}

/*
 * Print cmdline of proccess(escaped)
*/
void __print_cmdline(pid_t pid) {
  char filename[MAXPATHLEN];
  snprintf(filename,MAXPATHLEN, "/proc/%d/cmdline",pid);
  FILE *cmdline_file_handle=fopen(filename,"r");
  if(cmdline_file_handle==NULL) {
	fprintf(log_file_handle,"UNKNOWN");
	return;
  }
  
  char read_buffer[MAXFILEBUFFLEN+1]={0};
  int readed;
  do {
	readed=fread(read_buffer,sizeof(char),MAXFILEBUFFLEN,cmdline_file_handle);
	char *last_printed=read_buffer;
	int i;
	for(i=0; i<readed; i++) {
	    if(read_buffer[i]==0) {
		  __print_escaped(log_file_handle,last_printed);
		  fprintf(log_file_handle,"\\0");
		  last_printed=read_buffer+i+1;
		}
	}
	read_buffer[readed]=0;
	if(last_printed<read_buffer+readed)
	  __print_escaped(log_file_handle,last_printed); // print rest of buffer

  } while(readed==MAXFILEBUFFLEN);
  fclose(cmdline_file_handle);
}

/*
 * Format of log string: time event file flags result parents
*/
void __hook_log(const char *event_type, const char *filename, char* result, int err) {

  fprintf(log_file_handle,"%lld ",(unsigned long long)time(NULL));

  __print_escaped(log_file_handle, event_type);
  fprintf(log_file_handle," ");
  __print_escaped(log_file_handle, filename);
  fprintf(log_file_handle," %s %d ", result, err);
  // TODO: add a parent processes in output
  pid_t pid;
  __getparentpid(getpid());
  for(pid=getpid();pid!=0;pid=__getparentpid(pid)){
	__print_cmdline(pid);
	if(pid!=1)
	  fprintf(log_file_handle,",");
	
  }
  
  fprintf(log_file_handle,"\n");
  fflush(log_file_handle);
}

int open(const char * pathname, int flags, mode_t mode) {
    int ret;
    if(flags & O_CREAT)
        ret=_open(pathname, flags, mode);
    else
        ret=_open(pathname, flags, 0);

    __hook_log("open",pathname,"todo",errno);

	return ret;
}

int open64(const char * pathname, int flags, mode_t mode) {
	int ret;
	
    if(flags & O_CREAT)
        ret=_open64(pathname, flags, mode);
    else
        ret=_open64(pathname, flags, 0);
	
	__hook_log("open64",pathname,"todo",errno);
	
	return ret;
}

int execve(const char *filename, char *const argv[],
                  char *const envp[]) {
  __hook_log("execve",filename,"todo",0);

  int ret=_execve(filename, argv, envp);
  
  return ret;
}
