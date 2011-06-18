/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>
  Copyright (C) 2011       Alexander Bersenev <bay@hackerdom.ru>

  This program can be distributed under the terms of the GNU GPL.
*/

#define PACKAGE_NAME "hookfs"
#define PACKAGE_VERSION "0.1"

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE

#include <fuse.h>
#include <ulockmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>


#define MIN(a, b)  (((a) < (b)) ? (a) : (b))


#define MAXPATHLEN 256
#define MAXSOCKETPATHLEN 108
#define MAXFILEBUFFLEN 2048

#define IOBUFSIZE 65536


struct hookfs_config {
     int argv_debug;
};


pthread_mutex_t socketblock = PTHREAD_MUTEX_INITIALIZER;

int mountpoint_fd = -1;
char *mountpoint = NULL;
FILE * log_file = NULL;
int log_socket=-1;
struct hookfs_config config;

// Pid of parent process
pid_t parent_pid=-1;

char *stagenames[]={
  "pretend","setup","fetch","digest","manifest","unpack","prepare","configure","compile","test",
  "preinst","postinst","install","qmerge","merge","prerm","postrm","unmerge","config",
  "package","rpm","clean"
};
int stagenameslength=sizeof(stagenames)/sizeof(char *);

/*
 * Get a pid of the parent proccess
 * Parse the /proc/pid/stat
 * We need a first number after the last ')' character
*/
pid_t getparentpid(pid_t pid){
  char filename[MAXPATHLEN];
  snprintf(filename,MAXPATHLEN, "/proc/%d/stat",pid);
  FILE *stat_file_handle=fopen(filename,"r");
  if(stat_file_handle==NULL)
	return 0;
  
  char filedata[MAXFILEBUFFLEN];
  size_t bytes_readed=fread(filedata,sizeof(char),MAXFILEBUFFLEN,stat_file_handle);
  if(bytes_readed==0 || bytes_readed>=MAXFILEBUFFLEN) {
	fclose(stat_file_handle);
	return 0;	
  }
  
  filedata[bytes_readed]=0;
  
  char *beg_scan_offset=strrchr(filedata,')');
  if(beg_scan_offset==NULL) {
	fclose(stat_file_handle);
	return 0;	
  }
  
  pid_t parent_pid;
  int tokens_readed=sscanf(beg_scan_offset,") %*c %d",&parent_pid);
  if(tokens_readed!=1) {
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
 * Get a stage of building
*/
static char * getstagebypid(pid_t pid, pid_t toppid) {
  pid_t currpid;
  for(currpid=getparentpid(pid); currpid!=0 && currpid!=1 && currpid!=toppid;currpid=getparentpid(currpid)) {
	char filename[MAXPATHLEN];
	snprintf(filename,MAXPATHLEN, "/proc/%d/cmdline",pid);
	FILE *cmdline_file_handle=fopen(filename,"r");
	
	if(cmdline_file_handle==NULL)
	  return "unknown";
	
	char read_buffer[MAXFILEBUFFLEN];
	int readed;
	readed=fread(read_buffer,sizeof(char),MAXFILEBUFFLEN,cmdline_file_handle);
	
	fclose(cmdline_file_handle);  

	int off;
	int null_bytes_num=0;
	char *stage_name;
	// small automata here. For readabilyity it is not in classic form
	for(off=0;off<readed;off++) {
		if(read_buffer[off]==0){
		  null_bytes_num++;
		  if(null_bytes_num==2) {
			if(read_buffer+off-9<read_buffer) // 9 is a "ebuild.sh" string length
			  break;
			if(strcmp(read_buffer+off-9,"ebuild.sh")!=0)
			  break;
			stage_name=read_buffer+off+1;	
		  } else if(null_bytes_num==3) {
			// ugly, but memory allocation is not better
			// there is better way to write this
			int i;
			for(i=0; i<stagenameslength;i++) {
			  if(strcmp(stage_name,stagenames[i])==0)
				return stagenames[i];
			}
		  }
		}
	}
  }
  
  return "unknown";
}

/*
 * This is here because launching of a task is very slow without it
 */
static int is_file_excluded(const char *filename) {
  if(strcmp(filename,"/")==0)
	return 1;
  if(strcmp(filename,"/etc/ld.so.preload")==0)
	return 1;
  if(strcmp(filename,"/etc/ld.so.cache")==0)
	return 1;
  if(strcmp(filename,"/usr/lib64/locale/locale-archive")==0)
	return 1;
  if(strcmp(filename,"/usr/lib64/locale")==0)
	return 1;
  
  return 0;
}

/*
 * Check external access - for example user's access to mount
*/ 
static int is_process_external(pid_t pid) {
  if(pid==1 || getparentpid(pid)==1)
	return 0;
  
  for(;pid!=0;pid=getparentpid(pid))
	if(pid==parent_pid)
	  return 0;

  return 1;
}

static void raw_log_event(const char *event_type, const char *filename, char *result,int err, char* stage) {
  fprintf(log_file,"%lld%c",(unsigned long long)time(NULL),0);
  fprintf(log_file,"%s%c%s%c%s%c",event_type,0,filename,0,stage,0);
  
  if(strcmp(result,"ERR")==0)
	fprintf(log_file,"%s/%d",result,err);
  else
	fprintf(log_file,"%s",result);

  fprintf(log_file,"%c%c",0,0);
}

/*
 * Format of log string: time event file flags result parents
*/
static void log_event(const char *event_type, const char *filename, char *result,int err, char* stage) {
  if(is_file_excluded(filename)) return;

  pthread_mutex_lock( &socketblock );
  raw_log_event(event_type,filename,result,err,stage);

  pthread_mutex_unlock( &socketblock );
}

/*
 * Ack a python part about an event
 * Returns 1 if access is allowed and 0 if denied
*/
static int is_event_allowed(const char *event_type,const char *filename, pid_t pid, char* stage) {
  if(is_file_excluded(filename)) return 1;
  if(is_process_external(pid)) return 0; // protecting from external access
  //return 1;
  pthread_mutex_lock( &socketblock );

  // sending asking log_event
  raw_log_event(event_type,filename,"ASKING",0,stage);
  fflush(log_file);
  char answer[8];

  fscanf(log_file,"%7s",answer);
  pthread_mutex_unlock( &socketblock );
  
  if(strcmp(answer,"ALLOW")==0)
	return 1;
  else if(strcmp(answer,"DENY")==0)
	return 0;
  else
	fprintf(stderr,"Protocol error, text should be ALLOW or DENY, got: %s",answer);
  return 0;
}

static char * malloc_relative_path(const char *path) {
	int len = strlen(path);
	char * buf = malloc(1 + len + 1);
	if (! buf) {
		return NULL;
	}

	strcpy(buf, ".");
	strcpy(buf + 1, path);
	return buf;
}

static void give_to_creator_fd(int fd) {
	struct fuse_context * context = fuse_get_context();
	fchown(fd, context->uid, context->gid);
}

static void give_to_creator_path(const char *path) {
	struct fuse_context * context = fuse_get_context();
	lchown(path, context->uid, context->gid);
}

static int hookfs_getattr(const char *path, struct stat *stbuf)
{
  	struct fuse_context * context = fuse_get_context();
	char *stage=getstagebypid(context->pid,parent_pid);
	
	if(! is_event_allowed("stat",path,context->pid,stage)) {
	  errno=2; // not found
	  log_event("stat",path,"DENIED",errno,stage);

	  return -errno;
	}

	
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = lstat(rel_path, stbuf);
	free(rel_path);
		
	if (res == -1) {
   		log_event("stat",path,"ERR",errno,stage);
		return -errno;
	}
	log_event("stat",path,"OK",errno,stage);

	return 0;
}

static int hookfs_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi)
{
	int res;

  	struct fuse_context * context = fuse_get_context();
	char *stage=getstagebypid(context->pid,parent_pid);

	if(! is_event_allowed("stat",path,context->pid,stage)) {
	  errno=2; // not found
	  log_event("stat",path,"DENIED",errno,stage);

	  return -errno;
	}

	
	res = fstat(fi->fh, stbuf);
	
	if (res == -1) {
   		log_event("stat",path,"ERR",errno,stage);
		return -errno;
	}
	log_event("stat",path,"OK",errno,stage);
	
	return 0;
}

// according FUSE documentation this function 
// never run due to default_permissions option
static int hookfs_access(const char *path, int mask)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	access(rel_path, mask);
	free(rel_path);
		
	return 0;
}

static int hookfs_readlink(const char *path, char *buf, size_t size)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = readlink(rel_path, buf, size - 1);
	free(rel_path);

	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

struct hookfs_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static int hookfs_opendir(const char *path, struct fuse_file_info *fi)
{
	struct hookfs_dirp *d = malloc(sizeof(struct hookfs_dirp));
	if (d == NULL)
		return -ENOMEM;

	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	d->dp = opendir(rel_path);
	free(rel_path);
	if (d->dp == NULL) {
		free(d);
		return -errno;
	}
	d->offset = 0;
	d->entry = NULL;

	fi->fh = (unsigned long) d;
	return 0;
}

static inline struct hookfs_dirp *get_dirp(struct fuse_file_info *fi)
{
	return (struct hookfs_dirp *) (uintptr_t) fi->fh;
}

static int hookfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	struct hookfs_dirp *d = get_dirp(fi);

	(void) path;
	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		struct stat st;
		off_t nextoff;

		if (!d->entry) {
			d->entry = readdir(d->dp);
			if (!d->entry)
				break;
		}

		memset(&st, 0, sizeof(st));
		st.st_ino = d->entry->d_ino;
		st.st_mode = d->entry->d_type << 12;
		nextoff = telldir(d->dp);
		if (filler(buf, d->entry->d_name, &st, nextoff))
			break;

		d->entry = NULL;
		d->offset = nextoff;
	}

	return 0;
}

static int hookfs_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct hookfs_dirp *d = get_dirp(fi);
	(void) path;
	closedir(d->dp);
	free(d);
	return 0;
}

static int hookfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res;
	if (S_ISFIFO(mode))
		res = mkfifo(rel_path, mode);
	else
		res = mknod(rel_path, mode, rdev);
	if (res == 0)
		give_to_creator_path(rel_path);
	free(rel_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_mkdir(const char *path, mode_t mode)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = mkdir(rel_path, mode);
	if (res == 0)
		give_to_creator_path(rel_path);
	free(rel_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_unlink(const char *path)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = unlink(rel_path);
	free(rel_path);

	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_rmdir(const char *path)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = rmdir(rel_path);
	free(rel_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_symlink(const char *from, const char *to)
{
	char * rel_to = malloc_relative_path(to);
	if (! rel_to) {
		return -errno;
	}

	int res = symlink(from, rel_to);
	if (res == 0)
		give_to_creator_path(rel_to);
	free(rel_to);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_rename(const char *from, const char *to)
{
	char * rel_from = malloc_relative_path(from);
	if (! rel_from) {
		return -errno;
	}
	char * rel_to = malloc_relative_path(to);
	if (! rel_to) {
		free(rel_from);
		return -errno;
	}

	int res = rename(rel_from, rel_to);
	//NOTIFY(post_rename, from, to, res);
	free(rel_from);
	free(rel_to);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_link(const char *from, const char *to)
{
	char * rel_from = malloc_relative_path(from);
	if (! rel_from) {
		return -errno;
	}
	char * rel_to = malloc_relative_path(to);
	if (! rel_to) {
		free(rel_from);
		return -errno;
	}

	int res = link(rel_from, rel_to);
	free(rel_from);
	if (res == 0)
		give_to_creator_path(rel_to);
	free(rel_to);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_chmod(const char *path, mode_t mode)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = chmod(rel_path, mode);
	free(rel_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_chown(const char *path, uid_t uid, gid_t gid)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = lchown(rel_path, uid, gid);
	free(rel_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_truncate(const char *path, off_t size)
{
	struct fuse_context * context = fuse_get_context();
	char *stage=getstagebypid(context->pid,parent_pid);

	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = truncate(rel_path, size);
	free(rel_path);

	if (res == -1) {
  		log_event("write",path,"ERR",errno,stage);
		return -errno;
	}
	log_event("write",path,"OK",errno,stage);
	
	return 0;
}

static int hookfs_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{
	int res;

	struct fuse_context * context = fuse_get_context();
	char *stage=getstagebypid(context->pid,parent_pid);

	res = ftruncate(fi->fh, size);

	if (res == -1) {
  		log_event("write",path,"ERR",errno,stage);
		return -errno;
	}
	log_event("write",path,"OK",errno,stage);

	return 0;
}

static int hookfs_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];
	char * rel_path = NULL;

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;

	rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	res = utimes(rel_path, tv);
	free(rel_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int writing_flags(mode_t mode) {
	if (((mode & O_WRONLY) == O_WRONLY)
			|| ((mode & O_RDWR) == O_RDWR)
			|| ((mode & O_CREAT) == O_CREAT)) {
		return 1;
	}
	return 0;
}

static int open_safely(const char *rel_path, int flags, mode_t mode) {
	int fd = -1;
	if (writing_flags(flags)) {
		fd = open(rel_path, flags | O_CREAT | O_EXCL, mode);
		if (fd == -1) {
			/* Try again with original flags */
			fd = open(rel_path, flags, mode);
		} else {
			give_to_creator_fd(fd);
		}
	} else {
		fd = open(rel_path, flags, mode);
	}
	return fd;
}

static int hookfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct fuse_context * context = fuse_get_context();
  	char *stage=getstagebypid(context->pid,parent_pid);

	if(! is_event_allowed("create",path,context->pid,stage)) {
	  errno=2; // not found
	  log_event("create",path,"DENIED",errno,stage);

	  return -errno;
	}

	char * rel_path = malloc_relative_path(path);
	
	if (! rel_path) {
		return -errno;
	}

	int fd = open_safely(rel_path, fi->flags, mode);
	free(rel_path);

	if (fd == -1) {
		log_event("create",path,"ERR",errno,stage);
		return -errno;
	} 
	log_event("create",path,"OK",errno,stage);

	fi->fh = fd;
	return 0;
}

static int hookfs_open(const char *path, struct fuse_file_info *fi)
{
	int fd;
	char * rel_path = NULL;

	struct fuse_context * context = fuse_get_context();
	char *stage=getstagebypid(context->pid,parent_pid);

	if(! is_event_allowed("open",path,context->pid,stage)) {
	  errno=2; // not found
	  log_event("open",path,"DENIED",errno,stage);
	
	  return -errno;
	}
	
	rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	fd = open_safely(rel_path, fi->flags, 0000);
	free(rel_path);

	if (fd == -1) {
		log_event("open",path,"ERR",errno,stage);
		return -errno;
	}
	
	log_event("open",path,"OK",errno,stage);
	fi->fh = fd;
	return 0;
}

static int hookfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int res;

	struct fuse_context * context = fuse_get_context();
	char *stage=getstagebypid(context->pid,parent_pid);

	res = pread(fi->fh, buf, size, offset);
	if (res == -1) {
		log_event("read",path,"ERR",errno,stage);
		res = -errno;
	}

	log_event("read",path,"OK",errno,stage);
	return res;
}

static int hookfs_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int res;

	struct fuse_context * context = fuse_get_context();
	char *stage=getstagebypid(context->pid,parent_pid);

	res = pwrite(fi->fh, buf, size, offset);
	if (res == -1) {
  		log_event("write",path,"ERR",errno,stage);
		res = -errno;
	}

	log_event("write",path,"OK",errno,stage);

	return res;
}

static int hookfs_statfs(const char *path, struct statvfs *stbuf)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = statvfs(rel_path, stbuf);
	free(rel_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_flush(const char *path, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	/* This is called from every close on an open file, so call the
	   close on the underlying filesystem.	But since flush may be
	   called multiple times for an open file, this must not really
	   close the file.  This is important if used on a network
	   filesystem like NFS which flush the data/metadata on close() */
	res = close(dup(fi->fh));
	if (res == -1)
		return -errno;

	return 0;
}

static int hookfs_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;

	//int res = 
	close(fi->fh);

	return 0;
}

static int hookfs_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;
	(void) path;

#ifndef HAVE_FDATASYNC
	(void) isdatasync;
#else
	if (isdatasync)
		res = fdatasync(fi->fh);
	else
#endif
		res = fsync(fi->fh);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int hookfs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = lsetxattr(rel_path, name, value, size, flags);
	free(rel_path);
	if (res == -1)
		return -errno;
	return 0;
}

static int hookfs_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = lgetxattr(rel_path, name, value, size);
	free(rel_path);
	if (res == -1)
		return -errno;
	return res;
}

static int hookfs_listxattr(const char *path, char *list, size_t size)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = llistxattr(rel_path, list, size);
	free(rel_path);
	if (res == -1)
		return -errno;
	return res;
}

static int hookfs_removexattr(const char *path, const char *name)
{
	char * rel_path = malloc_relative_path(path);
	if (! rel_path) {
		return -errno;
	}

	int res = lremovexattr(rel_path, name);
	free(rel_path);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static int hookfs_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
	(void) path;

	return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
			   sizeof(fi->lock_owner));
}

static void * hookfs_init(struct fuse_conn_info *conn) {
	int res = fchdir(mountpoint_fd);
	assert(! res);

	close(mountpoint_fd);
	mountpoint_fd = -1;

	return NULL;
}

static void hookfs_destroy() {
	fflush(log_file);
}

static struct fuse_operations hookfs_oper = {
	.getattr	= hookfs_getattr,
	.fgetattr	= hookfs_fgetattr,
	.access		= hookfs_access,
	.readlink	= hookfs_readlink,
	.opendir	= hookfs_opendir,
	.readdir	= hookfs_readdir,
	.releasedir	= hookfs_releasedir,
	.mknod		= hookfs_mknod,
	.mkdir		= hookfs_mkdir,
	.symlink	= hookfs_symlink,
	.unlink		= hookfs_unlink,
	.rmdir		= hookfs_rmdir,
	.rename		= hookfs_rename,
	.link		= hookfs_link,
	.chmod		= hookfs_chmod,
	.chown		= hookfs_chown,
	.truncate	= hookfs_truncate,
	.ftruncate	= hookfs_ftruncate,
	.utimens	= hookfs_utimens,
	.create		= hookfs_create,
	.open		= hookfs_open,
	.read		= hookfs_read,
	.write		= hookfs_write,
	.statfs		= hookfs_statfs,
	.flush		= hookfs_flush,
	.release	= hookfs_release,
	.fsync		= hookfs_fsync,
#ifdef HAVE_SETXATTR
	.setxattr	= hookfs_setxattr,
	.getxattr	= hookfs_getxattr,
	.listxattr	= hookfs_listxattr,
	.removexattr	= hookfs_removexattr,
#endif
	.lock		= hookfs_lock,
	.init		= hookfs_init,
	.destroy	= hookfs_destroy,

	.flag_nullpath_ok = 1,
};

static int try_chdir_to_mountpoint(int argc, char *argv[]) {
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	int err = -1;

	err = fuse_parse_cmdline(&args, &mountpoint, NULL, NULL);
	fuse_opt_free_args(&args);
	if (err || ! mountpoint) {
		fprintf(stderr, "Mountpoint missing\n");
		return 0;
	}

	mountpoint_fd = open(mountpoint, O_DIRECTORY|O_RDONLY);
	if (err || (mountpoint_fd == -1)) {
		fprintf(stderr, "Could not open \"%s\" as a directory.\n", mountpoint);
		return 0;
	}

	fprintf(stderr, "Switching workdir to mountpoint \"%s\"...\n", mountpoint);
	return 1;
}

void default_config(struct hookfs_config * config) {
	config->argv_debug = 0;
}

enum {
	 KEY_HELP,
	 KEY_VERSION,
};

#define HOOKFS_OPT(t, p, v) { t, offsetof(struct hookfs_config, p), v }

static struct fuse_opt hookfs_opts[] = {
	HOOKFS_OPT("--argv-debug", argv_debug, 1),

	FUSE_OPT_KEY("-V", KEY_VERSION),
	FUSE_OPT_KEY("--version", KEY_VERSION),
	FUSE_OPT_KEY("-h", KEY_HELP),
	FUSE_OPT_KEY("--help", KEY_HELP),
	FUSE_OPT_END
};

void debug_argv(int argc, char *argv[], const struct hookfs_config * config) {
	if (! config->argv_debug) {
		return;
	}

	for (int i = 0; i < argc; i++) {
		fprintf(stderr, "[%d] '%s'\n", i, argv[i]);
	}
}

static int hookfs_handle_opt(void *data, const char *arg, int key, struct fuse_args *outargs) {
	struct hookfs_config * config = (struct hookfs_config *)data;

	switch (key) {
	case KEY_HELP:
			fuse_opt_add_arg(outargs, "-ho");
			debug_argv(outargs->argc, outargs->argv, config);

			fprintf(stderr,
					 "usage: %s mountpoint [options]\n"
					 "\n"
					 "%s options:\n"
					 "    --argv-debug           enable argv debugging\n"
					 "\n"
					 "general options:\n"
					 "    -o opt,[opt...]        mount options\n"
					 "    -h   --help            print help\n"
					 "    -V   --version         print version\n"
					 "\n"
					 , outargs->argv[0], PACKAGE_NAME);
			fuse_main(outargs->argc, outargs->argv, &hookfs_oper, NULL);
			exit(1);

	case KEY_VERSION:
			fuse_opt_add_arg(outargs, "--version");
			debug_argv(outargs->argc, outargs->argv, config);

			fprintf(stderr, "%s version %s\n", PACKAGE_NAME, PACKAGE_VERSION);
			fuse_main(outargs->argc, outargs->argv, &hookfs_oper, NULL);
			exit(0);
	}
	return 1;
}

int main(int argc, char *argv[]) {
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  default_config(&config);

  fuse_opt_parse(&args, &config, hookfs_opts, hookfs_handle_opt);

  fuse_opt_add_arg(&args, "-o" "nonempty");  /* to allow shadowing */
  fuse_opt_add_arg(&args, "-o" "use_ino");  /* to keep hardlinks working */
  fuse_opt_add_arg(&args, "-o" "default_permissions");  /* to enable access checks */

  debug_argv(args.argc, args.argv, &config);

  char *parent_pid_env=getenv("PARENT_PID");
  if(parent_pid_env==NULL || atoi(parent_pid_env)==0) {
	fprintf(stderr, "Parent pid is unknown, external access will be not blocked\n");
  } else {
	parent_pid=atoi(parent_pid_env);
  }
  
  char *log_socket_name=getenv("LOG_SOCKET");
  if(log_socket_name==NULL) {
	  fprintf(stderr,"Using stderr as output for logs "
			  "because the LOG_SOCKET environment variable isn't defined.\n");
	  log_file=stderr;
  } else {
	if(strlen(log_socket_name)>=MAXSOCKETPATHLEN) {
	  fprintf(stderr,"Unable to create a unix-socket %s: socket name is too long,exiting\n", log_socket_name);
	  return 1;
	}

	log_socket=socket(AF_UNIX, SOCK_STREAM, 0);
	if(log_socket==-1) {
	  fprintf(stderr,"Unable to create a unix-socket %s: %s\n", log_socket_name, strerror(errno));
	  return 1;
	}
	
	struct sockaddr_un serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sun_family = AF_UNIX;
	strcpy(serveraddr.sun_path, log_socket_name);
	
	int ret=connect(log_socket, (struct sockaddr *)&serveraddr, SUN_LEN(&serveraddr));
	if(ret==-1) {
	  fprintf(stderr,"Unable to connect a unix-socket: %s\n", strerror(errno));
	  return 1;
	}
	
	log_file=fdopen(log_socket,"a+");
	
	if(log_file==NULL) {
	  fprintf(stderr,"Unable to open a socket for a steam writing: %s\n", strerror(errno));
	  exit(1);
	}
	
	ret=setvbuf(log_file,NULL,_IOFBF,IOBUFSIZE);
	if(ret!=0){
	  fprintf(stderr,"Unable to set a size of io buffer");
	  exit(1);
	} 
  }
  
  if (! try_chdir_to_mountpoint(args.argc, args.argv)) {
	  return 1;
  }

  umask(0);
  int res = fuse_main(args.argc, args.argv, &hookfs_oper, NULL);
  fflush(log_file);
  fclose(log_file);
  return res;
}
