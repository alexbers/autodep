#!/usr/bin/env python2

import os
import sys

class logger:
  socketname=''
  hooklibpath='/home/bay/gsoc/src/hook_lib/file_hook.so' # TODO: change
  
  def __init__(self,socketname):
	self.socketname=socketname
	
  def execprog(self,prog_name,arguments):
	try:
	  env=os.environ.copy()
	  env["LD_PRELOAD"]=self.hooklibpath
	  env["LOG_SOCKET"]=self.socketname
	  
	  os.execvpe(prog_name, arguments, env)
	except OSError, e:
	  print "Failed to launch the programm: %s" % e
	  sys.exit(1)

