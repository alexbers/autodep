#!/usr/bin/env python2

import subprocess
import time
import os
import sys
import signal



class logger:
  socketname=''
  readytolaunch=False
  mountlist=["/dev/","/dev/pts","/dev/shm","/proc/","/sys/"]
  rootmountpath="/mnt/logfs_root_"+str(time.time())+"/"
  currpid=-1
#  rootmountpath="/mnt/logfs_roott_"+"/"
  
  def __init__(self, socketname, accuracy=False):
	if os.geteuid() != 0:
	  print "only root user can use FUSE approach for logging"
	  exit(1)

	self.socketname=socketname
	self.currpid=os.getpid()

	if accuracy==False:
	  self.mountlist=self.mountlist+["/lib64/", "/lib32/","/var/tmp/portage/"]
	
	
	print "mounting root filesystem into %s. External access to this folder will be blocked. Please, DON'T DELETE THIS FOLDER !!" % self.rootmountpath
	
	try:
	  os.mkdir(self.rootmountpath)
	except OSError,e:
	  if e.errno==17: # 17 is a "file exists" errno
		pass			# all is ok
	  else:
		print "failed to make mount directory %s: %s" % (self.rootmountpath,e)
		print "this error is fatal"
		exit(1)


	ret=subprocess.call(['mount','-o','bind','/',self.rootmountpath])
	if ret!=0:
	  print "failed to bind root filesystem to %s. Check messages above"%self.rootmountpath
	  exit(1)
	
	os.environ["LOG_SOCKET"]=self.socketname
	os.environ["PARENT_PID"]=str(self.currpid)

	# TODO: change
	ret=subprocess.call(['/home/bay/gsoc/logger/src/hook_fusefs/hookfs',self.rootmountpath,
						 '-o','allow_other,suid'])
	if ret!=0:
	  print "failed to launch FUSE logger. Check messages above"
	  exit(1)
	
	for mount in self.mountlist:
	  if not os.path.exists(mount):
		continue
	  
	  ret=subprocess.call(['mount','-o','bind',mount,self.rootmountpath+mount])
	  if ret!=0:
		print "failed to mount bind %s directory to %s. Check messages above" % (
			   mount,self.rootmountpath)
		exit(1)
	self.readytolaunch=True;
	
  def __del__(self):
	#we will delete the object manually after execprog
	pass
  
  # launches command, if it returns not 0 waits for 1 or 2 second and launches again
  # for various umounts
  def smartcommandlauncher(self,args):
	for waittime in (1,1,2):
	  ret=subprocess.call(args)
	  if ret==0:
		return
	  print "Auto-retrying after %d sec" % waittime
	  time.sleep(waittime)
	print "Giving up. Command %s failed" % args
	
  
  def execprog(self,prog_name,arguments):
	if self.currpid!=os.getpid():
	  print "Detected an attempt to execute execproc in other thread"
	  sys.exit(1)

	pid=os.fork()
	if pid==0:
	  try:
		cwd=os.getcwd()
		os.chroot(self.rootmountpath)
		os.chdir(cwd)
		
		env=os.environ.copy()
		env["LOGGER_PROCESS_IS_INTERNAL"]="YES"

		os.execvpe(prog_name, arguments, env)
		sys.exit(1)
	  except OSError, e:
		print "Error while chrooting and starting a program: %s" % e
		sys.exit(1)
	  
	else:
	  exitcode=2; # if ctrl-c pressed then returning this value
	  needtokillself=False
	  try:
		exitcode=os.wait()[1]/256;
	  except KeyboardInterrupt:
		needtokillself=True
	  try:
		print "Unmounting partitions"
		self.mountlist.reverse()
		for mount in self.mountlist:
	  	  if not os.path.exists(self.rootmountpath+mount):
			continue
		  self.smartcommandlauncher(['umount','-l',self.rootmountpath+mount])
		self.smartcommandlauncher(['fusermount','-z','-u',self.rootmountpath]);
		self.smartcommandlauncher(['umount','-l',self.rootmountpath]);
		os.rmdir(self.rootmountpath)

	  except OSError, e:
		print "Error while unmounting fuse filesystem: %s" % e
		sys.exit(1)
		
	  if needtokillself: # we kill self for report the status correct
		signal.signal(signal.SIGINT,signal.SIG_DFL)
		os.kill(os.getpid(),signal.SIGINT)
	  os._exit(exitcode)
