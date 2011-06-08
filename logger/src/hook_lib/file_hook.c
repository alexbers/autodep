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

#include <bits/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MAXPATHLEN 256
#define MAXSOCKETPATHLEN 108
#define MAXFILEBUFFLEN 2048

//extern int errorno;

int (*_open)(const char * pathname, int flags, ...);
int (*_open64)(const char * pathname, int flags, ...);
FILE * (*_fopen)(const char *path, const char *mode);
FILE * (*_fopen64)(const char *path, const char *mode);
int (*_execve)(const char *filename, char *const argv[],char *const envp[]);
pid_t (*_fork)();

FILE *log_file; // one of these two vars will be used for logging
int log_socket=-1;

int is_log_into_socket=0;

void __doinit(){
  //stat(NULL,NULL);
  _open = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open");
  _open64 = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open64");
  _fopen = (FILE * (*)(const char *path, const char *mode)) dlsym(RTLD_NEXT, "fopen");
  _fopen64 = (FILE * (*)(const char *path, const char *mode)) dlsym(RTLD_NEXT, "fopen64");
  _execve = (int (*)(const char *filename, char *const argv[],char *const envp[])) dlsym(RTLD_NEXT, "execve");
  _fork = (pid_t (*)()) dlsym(RTLD_NEXT, "fork");

  if(_open==NULL || _open64==NULL || 
	 _fopen==NULL || _fopen64==NULL || 
	 execve==NULL || _fork==NULL) {
	  fprintf(stderr,"Failed to load original functions of hook\n");
	  exit(1);
  }
  
  
  char *log_socket_name=getenv("LOG_SOCKET");
  if(log_socket_name==NULL) {
	fprintf(stderr,"Using stderr as output for logs "
				   "because the LOG_SOCKET environment variable isn't defined.\n");

	log_file=stderr;
  } else {
	is_log_into_socket=1;
	
	if(strlen(log_socket_name)>=MAXSOCKETPATHLEN) {
	  fprintf(stderr,"Unable to create a unix-socket %s: socket name is too long,exiting\n", log_socket_name);
	  exit(1);
	}
		
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
	
	log_file=fdopen(log_socket,"r+");
	
	if(log_file==NULL) {
	  fprintf(stderr,"Unable to open a socket for a steam writing: %s\n", strerror(errno));
	  exit(1);
	}
  }  
}

void __dofini() {
  fflush(log_file);
  fclose(log_file);

  if(is_log_into_socket)
	close(log_socket); 
  
  //fprintf(stderr,"All sockets closed\n");  
}

void _init() {
  __doinit();
} 

void _fini() {
  __dofini();
}

/*
 * Prints a string escaping spaces and '\'
 * Does not check input variables
*/
void __print_escaped(FILE *fh ,const char *s){
	for(;(*s)!=0; s++) {
		if(*s==' ')
		  fprintf(fh,"\\ ");
		else if(*s==',')
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
 * Get a pid of the parent proccess
 * Parse the /proc/pid/stat
 * We need a first number after last ')' character
*/
pid_t __getparentpid(pid_t pid){
  char filename[MAXPATHLEN];
  snprintf(filename,MAXPATHLEN, "/proc/%d/stat",pid);
  FILE *stat_file_handle=fopen(filename,"r");
  if(stat_file_handle==NULL) {
	fprintf(log_file,"NULL");
	return 0;
  }
  
  char filedata[MAXFILEBUFFLEN];
  size_t bytes_readed=fread(filedata,sizeof(char),MAXFILEBUFFLEN,stat_file_handle);
  if(bytes_readed==0 || bytes_readed>=MAXFILEBUFFLEN) {
	fprintf(log_file,"NULL");
	fclose(stat_file_handle);
	return 0;	
  }
  
  filedata[bytes_readed]=0;
  
  char *beg_scan_offset=rindex(filedata,')');
  if(beg_scan_offset==NULL) {
	fprintf(log_file,"NULL");
	fclose(stat_file_handle);
	return 0;	
  }
  
  pid_t parent_pid;
  int tokens_readed=sscanf(beg_scan_offset,") %*c %d",&parent_pid);
  if(tokens_readed!=1) {
	fprintf(log_file,"NULL");
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
	fprintf(log_file,"UNKNOWN");
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
		  __print_escaped(log_file,last_printed);
		  fprintf(log_file,"\\0");
		  last_printed=read_buffer+i+1;
		}
	}
	read_buffer[readed]=0;
	if(last_printed<read_buffer+readed)
	  __print_escaped(log_file,last_printed); // print rest of buffer

  } while(readed==MAXFILEBUFFLEN);
  fclose(cmdline_file_handle);
}

/*
 * Format of log string: time event file flags result parents
*/
void __hook_log(const char *event_type, const char *filename, int result, int err) {

  fprintf(log_file,"%lld ",(unsigned long long)time(NULL));

  __print_escaped(log_file, event_type);
  fprintf(log_file," ");
  __print_escaped(log_file, filename);
  fprintf(log_file," %d %d %d", result, err, getpid());
  // TODO: add a parent processes in output
//  pid_t pid;
//  __getparentpid(getpid());
//  for(pid=getpid();pid!=0;pid=__getparentpid(pid)){
//	__print_cmdline(pid);
//	if(pid!=1)
//	  fprintf(log_file,",");
	
//  }
  
  fprintf(log_file,"\n");
  fflush(log_file);
}

int open(const char * pathname, int flags, mode_t mode) {
    int ret;
    if(flags & O_CREAT)
        ret=_open(pathname, flags, mode);
    else
        ret=_open(pathname, flags, 0);

    __hook_log("open",pathname,ret,errno);

	return ret;
}

int open64(const char * pathname, int flags, mode_t mode) {
	int ret;
	
    if(flags & O_CREAT)
        ret=_open64(pathname, flags, mode);
    else
        ret=_open64(pathname, flags, 0);
	
	__hook_log("open",pathname,ret,errno);
	
	return ret;
}

FILE *fopen(const char *path, const char *mode) {
	FILE *ret;
	ret=_fopen(path,mode);
	__hook_log("open",path,0,errno);
	return ret;
}

FILE *fopen64(const char *path, const char *mode) {
	FILE *ret;
	ret=_fopen64(path,mode);
	__hook_log("open",path,0,errno);
	return ret;
}


int execve(const char *filename, char *const argv[],
                  char *const envp[]) {
  __hook_log("execve",filename,0,0);

  int ret=_execve(filename, argv, envp);
  
  return ret;
}

pid_t fork(void) {
  int ret=_fork();
  // we must to handle fork for reconnect a socket

  if(ret==0) {
	__dofini(); // reinit connection for clildren
	__doinit(); // because now it is different processes
  }
  
  return ret;
}
