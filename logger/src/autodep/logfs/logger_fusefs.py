#!/usr/bin/env python2

import subprocess
import time
import os
import sys


class logger:
  socketname=''
  readytolaunch=False
  mountlist=["/dev/","/proc/","/sys/"]
#  rootmountpath="/mnt/logfs_root_"+str(time.time())+"/"
  rootmountpath="/mnt/logfs_roott_"+"/"
  
  def __init__(self, socketname):
	self.socketname=socketname
	
	if os.geteuid() != 0:
	  print "only root user can use FUSE approach for logging"
	  exit(1)
	
	print "mounting root filesystem into %s. Please, DON'T DELETE THIS FOLDER!!" % self.rootmountpath
	
	for mount in self.mountlist:
	  try:
		os.makedirs(self.rootmountpath+mount)
	  except OSError,e:
		if e.errno==17: # 17 is a "file exists" errno
		  pass			# all is ok
		else:
		  print "failed to make mount directory %s: %s" % (self.rootmountpath+mount,e)
		  print "this error is fatal"
		  exit(1)
		  
	ret=subprocess.call(['mount','-o','bind','/',self.rootmountpath])
	if ret!=0:
	  print "failed to bind root filesystem to %s. Check messages above"%self.rootmountpath
	  exit(1)
	
	#env["LOG_SOCKET"]=self.socketname
	os.environ["LOG_SOCKET"]=self.socketname


	ret=subprocess.call(['/home/bay/gsoc/logger/src/hook_fusefs/hookfs',self.rootmountpath,
						 '-o','allow_other,suid'])
	if ret!=0:
	  print "failed to launch FUSE logger. Check messages above"
	
	for mount in self.mountlist:
	  ret=subprocess.call(['mount','-o','bind',mount,self.rootmountpath+mount])
	  if ret!=0:
		print "failed to mount bind %s directory to %s. Check messages above" % (
			   mount,self.rootmountpath)
		exit(1)
	self.readytolaunch=True;
	
  def __del__(self):
	#we will delete the object manually after execprog
	pass
  
  def execprog(self,prog_name,arguments):
	pid=os.fork()
	if pid==0:
	  try:
		cwd=os.getcwd()
		os.chroot(self.rootmountpath)
		os.chdir(cwd)
		
		env=os.environ.copy()

		os.execvpe(prog_name, arguments, env)
		sys.exit(1)
	  except OSError, e:
		print "Error while chrooting and starting a program: %s" % e
		sys.exit(1)
	  
	else:
	  exitcode=os.wait()[1];
	  try:
		# unmount all manually
		self.mountlist.reverse()
		for mount in self.mountlist:
		  ret=subprocess.call(['umount',self.rootmountpath+mount])
		  if ret!=0:
			print "failed to umount bind %s directory. Check messages above" % ( self.rootmountpath+mount)
		ret=subprocess.call(['fusermount','-u',self.rootmountpath]);
		if ret!=0:
		  print "Error while unmounting fuse filesystem. Check messages above"
		ret=subprocess.call(['umount',self.rootmountpath]);
		if ret!=0:
		  print "Error while unmounting %s Check messages above" % (self.rootmountpath)

	  except OSError, e:
		print "Error while unmounting fuse filesystem: %s" % e
		sys.exit(1)
		
	  sys.exit(int(exitcode/256))
