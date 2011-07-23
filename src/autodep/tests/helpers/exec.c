#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <bits/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>



int main() {

  // test one: execve
  if(fork()==0) {
    char *argv[]={"/bin/cat","/f1",NULL};
    char *env[]={"TEST=TEST",NULL};
    execve("/bin/cat",argv,env);
    exit(1);
  } else wait(NULL);

  // test two: execve with env changing
  if(fork()==0) {
    char *argv[]={"/bin/cat","/f2",NULL};
    char *env[]={"LD_PRELOAD=BADVALUE",NULL};
    execve("/bin/cat",argv,env);
    exit(1);
  } else wait(NULL);

  // test three: execve without env
  if(fork()==0) {
    char *argv[]={"/bin/cat","/f3",NULL};
    char *env[]={NULL};
    execve("/bin/cat",argv,env);
    exit(1);
  } else wait(NULL);

  // test four: execv
  if(fork()==0) {
    char *argv[]={"/bin/cat","/f4",NULL};
    execv("/bin/cat",argv);
    exit(1);
  } else wait(NULL);

  // test five: execv with env changing
  if(fork()==0) {
    setenv("LD_PRELOAD","BADVALUE",1);
    char *argv[]={"/bin/cat","/f5",NULL};
    execv("/bin/cat",argv);
    exit(1);
  } else wait(NULL);

  // test six: execv with env changing(putenv)
  if(fork()==0) {
    putenv("LD_PRELOAD=BADVALUE");
    char *argv[]={"/bin/cat","/f6",NULL};
    execv("/bin/cat",argv);
    exit(1);
  } else wait(NULL);

  // test seven: execvp with env changing(putenv)
  if(fork()==0) {
    putenv("LD_PRELOAD=BADVALUE");
    char *argv[]={"/bin/cat","/f7",NULL};
    execvp("/bin/cat",argv);
    exit(1);
  } else wait(NULL);

  // test eight: execvp with env changing(putenv)
  if(fork()==0) {
    //putenv("LD_PRELOAD=BADVALUE");
    char *env[]={"LD_PRELOAD=BADVALUE",NULL};
    char *argv[]={"/bin/cat","/f8",NULL};
    execvpe("/bin/cat",argv,env);
    exit(1);
  } else wait(NULL);

  // test nine: execl with env changing(putenv)
  if(fork()==0) {
    putenv("LD_PRELOAD=BADVALUE");
    //char *env[]={"LD_PRELOAD=BADVALUE",NULL};
    //char *argv[]={"/bin/cat","/f9",NULL};
    execl("/bin/cat","/bin/cat","/f9",NULL);
    exit(1);
  } else wait(NULL);

  // test ten: execlp with env changing(putenv)
  if(fork()==0) {
    putenv("LD_PRELOAD=BADVALUE");
    //char *env[]={"LD_PRELOAD=BADVALUE",NULL};
    //char *argv[]={"/bin/cat","/f9",NULL};
    execlp("/bin/cat","/bin/cat","/f10",NULL);
    exit(1);
  } else wait(NULL);

  // test eleven: execle with env changing
  if(fork()==0) {
    char *env[]={"LD_PRELOAD=BADVALUE",NULL};
    execle("/bin/cat","/bin/cat","/f11",NULL,env);
    exit(1);
  } else wait(NULL);

  // test twelve: fexecve with env changing
  if(fork()==0) {
    char *argv[]={"/bin/cat","/f12",NULL};
    char *env[]={"LD_PRELOAD=BADVALUE",NULL};
    int handle=open("/bin/cat",O_RDONLY);
    fexecve(handle,argv,env);
    exit(1);
  } else wait(NULL);

  // test thirteen: system wit env changing(putenv)
  if(fork()==0) {
    putenv("LD_PRELOAD=BADVALUE");
    //putenv("LOG_SOCKET=BADVALUE");
    //system("echo aa $LOG_SOCKET fff");
    system("cat /f13");
    //system("set");
    exit(1);
  } else wait(NULL);

  return 0;
}
