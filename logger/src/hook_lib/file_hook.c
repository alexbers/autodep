#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <dlfcn.h>
#include <pthread.h>

#define _FCNTL_H
#include <bits/fcntl.h>

#include <bits/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MAXPATHLEN 1024
#define MAXSOCKETPATHLEN 108
#define MAXFILEBUFFLEN 2048

#define MAXSOCKETMSGLEN 8192

//extern int errorno;

pthread_mutex_t socketblock = PTHREAD_MUTEX_INITIALIZER;

int (*_open)(const char * pathname, int flags, ...);
int (*_open64)(const char * pathname, int flags, ...);
FILE * (*_fopen)(const char *path, const char *mode);
FILE * (*_fopen64)(const char *path, const char *mode);
int (*_execve)(const char *filename, char *const argv[],char *const envp[]);
ssize_t (*_read)(int fd, void *buf, size_t count);
ssize_t (*_write)(int fd, const void *buf, size_t count);
size_t (*_fread)(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t (*_fwrite)(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int (*_close)(int fd); // we hooking this, because some programs closes our socket

int log_socket=-1;

void __doinit(){
  _open = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open");
  _open64 = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open64");

  _fopen = (FILE * (*)(const char *path, const char *mode)) dlsym(RTLD_NEXT, "fopen");
  _fopen64 = (FILE * (*)(const char *path, const char *mode)) dlsym(RTLD_NEXT, "fopen64");

  _read= (ssize_t (*)(int fd, void *buf, size_t count)) dlsym(RTLD_NEXT, "read");
  _write= (ssize_t (*)(int fd, const void *buf, size_t count)) dlsym(RTLD_NEXT, "write");
  
  _execve = (int (*)(const char *filename, char *const argv[],char *const envp[])) dlsym(RTLD_NEXT, "execve");

  _close= (int (*)(int fd)) dlsym(RTLD_NEXT, "close");

  
  if(_open==NULL || _open64==NULL || 
	 _fopen==NULL || _fopen64==NULL || 
	 execve==NULL || _read==NULL || _write==NULL || close==NULL) {
	  fprintf(stderr,"Failed to load original functions of hook\n");
	  exit(1);
  }
  
  
  char *log_socket_name=getenv("LOG_SOCKET");
  if(log_socket_name==NULL) {
	fprintf(stderr,"LOG_SOCKET environment variable isn't defined."
					"Are this library launched by server?\n");

	exit(1);
  } else {
	if(strlen(log_socket_name)>=MAXSOCKETPATHLEN) {
	  fprintf(stderr,"Unable to create a unix-socket %s: socket name is too long,exiting\n", log_socket_name);
	  exit(1);
	}
		
	log_socket=socket(AF_UNIX, SOCK_SEQPACKET, 0);
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
  }  
}

void __dofini() {
  //close(log_socket); 
}

void _init() {
  __doinit();
} 

void _fini() {
  __dofini();
}

/*
 * Format of log string: time event filename stage result/err
*/
static void __raw_log_event(const char *event_type, const char *filename, char *result,int err, char* stage) {
  //printf("lololo:%s %s %s\n",event_type,filename,stage);

  char msg_buff[MAXSOCKETMSGLEN];
  int bytes_to_send;
  if(strcmp(result,"ERR")==0) {
	bytes_to_send=snprintf(msg_buff,MAXSOCKETMSGLEN,"%lld%c%s%c%s%c%s%c%s/%d",
	  (unsigned long long)time(NULL),0,event_type,0,filename,0,stage,0,result,err);
  } else {
	bytes_to_send=snprintf(msg_buff,MAXSOCKETMSGLEN,"%lld%c%s%c%s%c%s%c%s",
	  (unsigned long long)time(NULL),0,event_type,0,filename,0,stage,0,result);	
  }
  
  if(bytes_to_send>=MAXSOCKETMSGLEN) return;
  if(send(log_socket,msg_buff,bytes_to_send,0)==-1) {
	printf("BAYBAY!!!11 %d %d\n",log_socket, getpid());
	sleep(100500);
  }
  
}

/*
 * Log an event
*/
static void __log_event(const char *event_type, const char *filename, char *result,int err, char* stage) {
  __raw_log_event(event_type,filename,result,err,stage);
}

/*
 * Get a stage. Stage is from environment
*/
static char * __get_stage(){
  char *ret=getenv("EBUILD_PHASE");
  if(ret==NULL)
	return "unknown";
  return ret;
}

/*
 * Get full path by fd
*/
ssize_t __get_path_by_fd(int fd, char *output, int output_len) {
  char path_to_fd_link[MAXPATHLEN];
  
  snprintf(path_to_fd_link,MAXPATHLEN,"/proc/self/fd/%d",fd);
  ssize_t bytes_num=readlink(path_to_fd_link,output,output_len-1);
  output[bytes_num]=0; // because readlink don't do this
  if(output[0]!='/') return -1; // some odd string like pipe:
  return bytes_num;
}

/*
 * Ask for event "alloweness"
*/
static int __is_event_allowed(const char *event_type,const char *filename, char* stage) {
  char answer[8];
  int bytes_recieved;

  
  pthread_mutex_lock( &socketblock );

  __raw_log_event(event_type,filename,"ASKING",0,stage);
  bytes_recieved=recv(log_socket,answer,8,0);

  pthread_mutex_unlock( &socketblock );
  
  if(strcmp(answer,"ALLOW")==0)
	return 1;
  else if(strcmp(answer,"DENY")==0)
	return 0;
  else 
	fprintf(stderr,"Protocol error, text should be ALLOW or DENY, got: %s",answer);
  return 0;
}


int open(const char * path, int flags, mode_t mode) {
    int ret;
	char fullpath[MAXPATHLEN];
	realpath(path,fullpath);
	char *stage=__get_stage();
	if(! __is_event_allowed("open",fullpath,stage)) {
	  errno=2; // not found
	  __log_event("open",fullpath,"DENIED",errno,stage);
	  return -1;
	}

	
    if(flags & O_CREAT)
        ret=_open(path, flags, mode);
    else
        ret=_open(path, flags, 0);

	if(ret==-1)
	  __log_event("open",fullpath,"ERR",errno,stage);
	else
	  __log_event("open",fullpath,"OK",0,stage);
	  
	
	return ret;
}

int open64(const char * path, int flags, mode_t mode) {
	int ret;
	char fullpath[MAXPATHLEN];
	realpath(path,fullpath);
	char *stage=__get_stage();
	if(! __is_event_allowed("open",fullpath,stage)) {
	  errno=2; // not found
	  __log_event("open",path,"DENIED",errno,stage);
	  return -1;
	}
	
    if(flags & O_CREAT)
        ret=_open64(path, flags, mode);
    else
        ret=_open64(path, flags, 0);
	
	if(ret==-1)
	  __log_event("open",fullpath,"ERR",errno,stage);
	else
	  __log_event("open",fullpath,"OK",0,stage);
	
	return ret;
}

FILE *fopen(const char *path, const char *mode) {
	FILE *ret;
	char fullpath[MAXPATHLEN];
	realpath(path,fullpath);

	char *stage=__get_stage();
	if(! __is_event_allowed("open",fullpath,stage)) {
	  errno=2; // not found
	  __log_event("open",path,"DENIED",errno,stage);
	  return NULL;
	}

	ret=_fopen(path,mode);
	if(ret==NULL)
	  __log_event("open",fullpath,"ERR",errno,stage);
	else
	  __log_event("open",fullpath,"OK",0,stage);
	return ret;
}

FILE *fopen64(const char *path, const char *mode) {
	FILE *ret;
	char fullpath[MAXPATHLEN];
	realpath(path,fullpath);

	char *stage=__get_stage();
	if(! __is_event_allowed("open",fullpath,stage)) {
	  errno=2; // not found
	  __log_event("open",fullpath,"DENIED",errno,stage);
	  return NULL;
	}

	ret=_fopen64(path,mode);

	if(ret==NULL)
	  __log_event("open",fullpath,"ERR",errno,stage);
	else
	  __log_event("open",fullpath,"OK",0,stage);

	return ret;
}

ssize_t read(int fd, void *buf, size_t count){
  ssize_t ret=_read(fd,buf,count);
  char *stage=__get_stage();

  char fullpath[MAXPATHLEN];
  ssize_t path_size=__get_path_by_fd(fd,fullpath,MAXPATHLEN);
  if(path_size==-1)
	return ret;
  
  if(ret==-1)
	__log_event("read",fullpath,"ERR",errno,stage);
  else
	__log_event("read",fullpath,"OK",0,stage);

  return ret;
}

ssize_t write(int fd,const void *buf, size_t count){
  ssize_t ret=_write(fd,buf,count);
  char *stage=__get_stage();
  char fullpath[MAXPATHLEN];
  ssize_t path_size=__get_path_by_fd(fd,fullpath,MAXPATHLEN);
  if(path_size==-1)
	return ret;
  
  if(ret==-1)
	__log_event("write",fullpath,"ERR",errno,stage);
  else
	__log_event("write",fullpath,"OK",0,stage);

  return ret;
}



int execve(const char *filename, char *const argv[],
                  char *const envp[]) {
  if(access(filename, F_OK)!=-1)
	__log_event("open",filename,"OK",0,__get_stage());
  else
	__log_event("open",filename,"ERR",2,__get_stage());
	
  
  int ret=_execve(filename, argv, envp);
  
  return ret;
}

int close(int fd) {
  if(fd!=log_socket) {
	return _close(fd);
  }
  return -1;
}

