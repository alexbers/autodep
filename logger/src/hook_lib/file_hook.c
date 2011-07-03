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
#define MAXENVSIZE 65536
#define MAXENVITEMSIZE 256

//extern int errorno;

pthread_mutex_t socketblock = PTHREAD_MUTEX_INITIALIZER;

int (*_open)(const char * pathname, int flags, ...);
int (*_open64)(const char * pathname, int flags, ...);
FILE * (*_fopen)(const char *path, const char *mode);
FILE * (*_fopen64)(const char *path, const char *mode);
ssize_t (*_read)(int fd, void *buf, size_t count);
ssize_t (*_write)(int fd, const void *buf, size_t count);
size_t (*_fread)(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t (*_fwrite)(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int (*_execve)(const char *filename, char *const argv[],char *const envp[]);
int (*_execv)(const char *path, char *const argv[]);
int (*_execvp)(const char *file, char *const argv[]);
int (*_fexecve)(int fd, char *const argv[], char *const envp[]);

int (*_system)(const char *command);

pid_t (*_fork)();

int (*_setenv)(const char *name, const char *value, int overwrite);
int (*_close)(int fd); // we hooking this, because some programs closes our socket

int log_socket=-1;

char log_socket_name[MAXSOCKETPATHLEN];

void __doconnect(){
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
	fprintf(stderr,"Unable to connect a unix-socket %s: %s\n",log_socket_name, strerror(errno));
	fflush(stderr);
	//execlp("/bin/bash","/bin/bash",NULL);
	exit(1);
  }
}

void __dodisconnect() {
  close(log_socket); 
}

void __doreconnect() {
  __doconnect();
  __dodisconnect();
}

// this fucnction executes when library is loaded
void _init() {
  char *log_socket_env=getenv("LOG_SOCKET");
  
  if(log_socket_env==NULL) {
	fprintf(stderr,"LOG_SOCKET environment variable isn't defined."
					"Are this library launched by server?\n");
	exit(1);
  }

  if(strlen(log_socket_env)>=MAXSOCKETPATHLEN) {
	fprintf(stderr,"Unable to create a unix-socket %s: socket name is too long,exiting\n", log_socket_name);
	exit(1);
  }
  strcpy(log_socket_name,log_socket_env);


  _open = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open");
  _open64 = (int (*)(const char * pathname, int flags, ...)) dlsym(RTLD_NEXT, "open64");

  _fopen = (FILE * (*)(const char *path, const char *mode)) dlsym(RTLD_NEXT, "fopen");
  _fopen64 = (FILE * (*)(const char *path, const char *mode)) dlsym(RTLD_NEXT, "fopen64");

  _read= (ssize_t (*)(int fd, void *buf, size_t count)) dlsym(RTLD_NEXT, "read");
  _write= (ssize_t (*)(int fd, const void *buf, size_t count)) dlsym(RTLD_NEXT, "write");
  
  _fork = (pid_t (*)()) dlsym(RTLD_NEXT, "fork");
  _execve = (int (*)(const char *filename, char *const argv[],char *const envp[])) dlsym(RTLD_NEXT, "execve");
  _execv = (int (*)(const char *path, char *const argv[])) dlsym(RTLD_NEXT, "execv");
  _execvp = (int (*)(const char *file, char *const argv[])) dlsym(RTLD_NEXT, "execvp");

  _fexecve = (int (*)(int fd, char *const argv[], char *const envp[])) dlsym(RTLD_NEXT, "fexecve");

  _system = (int (*)(const char *command)) dlsym(RTLD_NEXT, "system");
  
  
  _setenv=(int (*)(const char *name, const char *value, int overwrite)) dlsym(RTLD_NEXT, "setenv");
  _close= (int (*)(int fd)) dlsym(RTLD_NEXT, "close");

  
  if(_open==NULL || _open64==NULL || 
	 _fopen==NULL || _fopen64==NULL || 
	  _read==NULL || _write==NULL ||
	  _fork==NULL || _execve==NULL || _execv==NULL || _fexecve==NULL ||  _execvp==NULL ||
	  _system==NULL || _setenv==NULL || _close==NULL) {
	  fprintf(stderr,"Failed to load original functions of hook\n");
	  exit(1);
  }
  
  __doconnect();
} 

void _fini() {
  __dodisconnect();
}

/*
 * Format of log string: time event filename stage result/err
*/
static int __raw_log_event(const char *event_type, const char *filename, char *result,int err, char* stage) {
  char msg_buff[MAXSOCKETMSGLEN];
  int bytes_to_send;
  if(strcmp(result,"ERR")==0) {
	bytes_to_send=snprintf(msg_buff,MAXSOCKETMSGLEN,"%lld%c%s%c%s%c%s%c%s/%d",
	  (unsigned long long)time(NULL),0,event_type,0,filename,0,stage,0,result,err);
  } else {
	bytes_to_send=snprintf(msg_buff,MAXSOCKETMSGLEN,"%lld%c%s%c%s%c%s%c%s",
	  (unsigned long long)time(NULL),0,event_type,0,filename,0,stage,0,result);	
  }
  
  if(bytes_to_send>=MAXSOCKETMSGLEN) 
	return 0;
  
  if(send(log_socket,msg_buff,bytes_to_send,0)==-1) {
	__doreconnect(); // looks like our socket has been destroyed by logged program
					 // try to recreate it

	if(send(log_socket,msg_buff,bytes_to_send,0)==-1)
	  return 0;
  }
  
  return 1;
}

/*
 * Log an event
*/
static int __log_event(const char *event_type, const char *filename, char *result,int err, char* stage) {
  pthread_mutex_lock( &socketblock );
  int ret=__raw_log_event(event_type,filename,result,err,stage);
  pthread_mutex_unlock( &socketblock );
  return ret;
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
  //printf("asking %s\n",filename);
  
  pthread_mutex_lock( &socketblock );

  __raw_log_event(event_type,filename,"ASKING",0,stage);
  bytes_recieved=recv(log_socket,answer,8,0);

  if(bytes_recieved==-1) {
	__doreconnect(); // looks like our socket has been destroyed by logged program
				   // try to recreate it
	bytes_recieved=recv(log_socket,answer,8,0);
  }
  
  pthread_mutex_unlock( &socketblock );
  
  if(strcmp(answer,"ALLOW")==0) {
	return 1;
  } else if(strcmp(answer,"DENY")==0)
	return 0;
  else {
	fprintf(stderr,"Protocol error, text should be ALLOW or DENY, got: '%s' recv retcode=%d(%s)"
			" socket=%d",answer,bytes_recieved,strerror(errno),log_socket);
	exit(1);
  }
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
	
    ret=_open(path, flags, mode);
	int saved_errno=errno;

	if(ret==-1)
	  __log_event("open",fullpath,"ERR",errno,stage);
	else
	  __log_event("open",fullpath,"OK",0,stage);	  
	errno=saved_errno;
	
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
	
    ret=_open64(path, flags, mode);

	int saved_errno=errno;
	if(ret==-1)
	  __log_event("open",fullpath,"ERR",errno,stage);
	else
	  __log_event("open",fullpath,"OK",0,stage);
	errno=saved_errno;
	
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
	int saved_errno=errno;
	if(ret==NULL)
	  __log_event("open",fullpath,"ERR",errno,stage);
	else
	  __log_event("open",fullpath,"OK",0,stage);
	errno=saved_errno;
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
	int saved_errno=errno;

	if(ret==NULL)
	  __log_event("open",fullpath,"ERR",errno,stage);
	else
	  __log_event("open",fullpath,"OK",0,stage);

	errno=saved_errno;
	return ret;
}

ssize_t read(int fd, void *buf, size_t count){
  ssize_t ret=_read(fd,buf,count);
  int saved_errno=errno;
  char *stage=__get_stage();

  char fullpath[MAXPATHLEN];
  ssize_t path_size=__get_path_by_fd(fd,fullpath,MAXPATHLEN);
  if(path_size!=-1) {
	if(ret==-1)
	  __log_event("read",fullpath,"ERR",errno,stage);
	else
	  __log_event("read",fullpath,"OK",0,stage);
  }
  errno=saved_errno;
  return ret;
}

ssize_t write(int fd,const void *buf, size_t count){
  ssize_t ret=_write(fd,buf,count);
  int saved_errno=errno;
  char *stage=__get_stage();
  char fullpath[MAXPATHLEN];
  ssize_t path_size=__get_path_by_fd(fd,fullpath,MAXPATHLEN);
  if(path_size!=-1){
	if(ret==-1)
	  __log_event("write",fullpath,"ERR",errno,stage);
	else
	  __log_event("write",fullpath,"OK",0,stage);
  }
  
  errno=saved_errno;
  return ret;
}

pid_t fork(void) {
  //fprintf(stderr,"prefork %s %s\n",getenv("LOG_SOCKET"),log_socket_orig);

  //int succ=
  _setenv("LOG_SOCKET",log_socket_name,1);
  
  //fprintf(stderr,"prefork %s%p%p%d %s\n",getenv("LOG_SOCKET"),_setenv,setenv,succ,log_socket_orig);

  int ret=_fork();
  int saved_errno=errno;
  // we must to handle fork for reconnect a socket
  
  if(ret==0) {
	__doreconnect(); // reinit connection for children
					 // because now it is different processes
  } else {
	//fprintf(stderr,"fork new: %d LOG_SOCKET=%s\n", ret,getenv("LOG_SOCKET"));
	//sleep(3);
  }
  errno=saved_errno;
  return ret;
}

int execve(const char *filename, char *const argv[],
                  char *const envp[]) {
  if(access(filename, F_OK)!=-1)
	__log_event("read",filename,"OK",0,__get_stage());
  else
	__log_event("open",filename,"ERR",2,__get_stage());
  
  //fprintf(stderr,"executing %s pid=%d", filename,getpid());
  char *new_envp[MAXENVSIZE];
  char new_ld_preload[MAXENVITEMSIZE];
  char new_log_socket[MAXENVITEMSIZE];
  
  int ld_preload_valid=0;
  int log_socket_valid=0;
  int i;
  for(i=0;envp[i];i++){
	if(strncmp(envp[i],"LD_PRELOAD=",11)==0)
	  if(strcmp(envp[i]+11,getenv("LD_PRELOAD"))==0) 
		ld_preload_valid=1;
	if(strncmp(envp[i],"LOG_SOCKET=",11)==0)
	  if(strcmp(envp[i]+11,log_socket_name)==0) 
		log_socket_valid=1;
  }
  if(!ld_preload_valid || !log_socket_valid) {
	snprintf(new_ld_preload,MAXENVITEMSIZE,"LD_PRELOAD=%s",getenv("LD_PRELOAD"));
	snprintf(new_log_socket,MAXENVITEMSIZE,"LOG_SOCKET=%s",log_socket_name);
	for(i=0; envp[i] && i<MAXENVSIZE-3; i++) {
	  if(strncmp(envp[i],"LD_PRELOAD=",11)==0) {
		new_envp[i]=new_ld_preload;
		ld_preload_valid=1;
	  } else if(strncmp(envp[i],"LOG_SOCKET=",11)==0) {
		new_envp[i]=new_log_socket;
		log_socket_valid=1;
	  } else {
		new_envp[i]=envp[i];
	  }
	}

	if(!ld_preload_valid) {
	  new_envp[i]=new_ld_preload;
	  i++;
	}
	if(!log_socket_valid) {
	  new_envp[i]=new_log_socket;
	  i++;
}	
	new_envp[i]=NULL;
	envp=new_envp;
//	for(i=0;envp[i];i++){
//	  printf("BAY: %s\n",envp[i]);
//	}

  }
  
  fflush(stderr);
  int ret=_execve(filename, argv, envp);
  
  return ret;
}

//int clone(int (*fn)(void *), void *child_stack,
//                 int flags, void *arg, ...) {
//	fprintf(stderr,"clone pid=%d",getpid());
//	fflush(stderr);

//	return -1;//_clone(fn,child_stack,flags,arg);
//}


/*int fexecve(int fd, char *const argv[], char *const envp[]) {
	fprintf(stderr,"fexecuting pid=%d",getpid());
	fflush(stderr);
	return _fexecve(fd,argv,envp);
}

int execle(const char *path, const char *arg, ...) {
	fprintf(stderr,"fexecluting pid=%d",getpid());
	fflush(stderr);
//
	return 0;
}

int execl(const char *path, const char *arg, ...){
	fprintf(stderr,"execluting 1 pid=%d",getpid());
	fflush(stderr);
//
	return 0;  
}

int execv(const char *path, char *const argv[]){
	fprintf(stderr,"execvuting 1 pid=%d",getpid());
	fflush(stderr);
	_execv(path,argv);
	return 0;  
}

int execvp(const char *file, char *const argv[]){
 	fprintf(stderr,"execvpting 1 pid=%d",getpid());
	fflush(stderr);

	return _execvp(file,argv);
	
	return 0;  
} 


int execvpe(const char *file, char *const argv[],
                  char *const envp[]){
	fprintf(stderr,"execvpeting 1 pid=%d",getpid());
	fflush(stderr);
//
	return 0;  
}

int execlp(const char *file, const char *arg, ...){
	fprintf(stderr,"execlpeting 1 pid=%d",getpid());
	fflush(stderr);
//
	return 0;  
}

int rexec(char **ahost, int inport, char *user,
                 char *passwd, char *cmd, int *fd2p){
 	fprintf(stderr,"rexec 1 pid=%d",getpid());
	fflush(stderr);
//
	return 0;  
} 

int system(const char *command) {
 	fprintf(stderr,"rexec 1 pid=%d cmd=%s",getpid(),command);
	fflush(stderr);

	return _system(command);
	return 0;  
  
}*/



int setenv(const char *name, const char *value, int overwrite) {
	//printf ("   CHANGING name: %s, value: %s",name,value);
	if(strcmp(name,"LD_PRELOAD")==0 ||
	  strcmp(name,"LOG_SOCKET")==0) return -1;
	int ret=_setenv(name,value,overwrite);
	return ret;
}

//int putenv(char *string){
// 	fprintf(stderr,"putenv 1 pid=%d cmd=%s",getpid(),string);
//	fflush(stderr);
//
//	//return _system(command);
//	return 0;    
//}


int close(int fd) {
  if(fd!=log_socket) {
	return _close(fd);
  }
  return -1;
}
