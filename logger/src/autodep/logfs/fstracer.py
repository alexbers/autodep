#!/usr/bin/env python2
"""
This module is a bridge between low-level logging services and high level handling dependency logic
"""

import os
import sys
import time
import tempfile
import socket
import select
import re

import proc_helpers

import logger_hooklib
import logger_fusefs

def parse_message(message):
  ret=message.split("\0")
  return ret

# check if proccess is finished
def checkfinished(pid):
  if not os.path.exists("/proc/%d/stat" % pid):
	return True

  try:
	pid_child,exitcode = os.waitpid(pid, os.WNOHANG)
  except OSError, e:
	if e.errno == 10: 
	  return False
	else:
	  raise
  
  if pid_child==0:
	return False
  return True

#check if process is zombie
def iszombie(pid):
  try:
	statfile=open("/proc/%d/stat" % pid,"r")
	line=statfile.readline()
	statfile.close()
	line=line.rsplit(")")[1] # find last ")" char
	line=line.strip()
	match=re.match(r"^(\w)",line)
	if match==None:
	  print "Failed to get check if process is zombie. Format of /proc/<pid>/stat is incorrect. Did you change a kernel?"
	  return False
	
	return match.group(1)=="Z"
	
  except IOError,e:
	return True
  

# uses /proc filesystem to get pid of parent
# it is not used in program. Using function on C instead
def getparentpid(pid):
  try:
	statfile=open("/proc/%d/stat" % pid,"r")
	line=statfile.readline()
	statfile.close()
	line=line.rsplit(")")[1] # find last ")" char
	line=line.strip()
	match=re.match(r"^\w\s(\d+)",line)
	if match==None:
	  print "Failed to get parent process. Format of /proc/<pid>/stat is incorrect. Did you change a kernel?"
	  return 1
	
	return int(match.group(1))
	
  except IOError,e:
	return 1

#check if message came from one of a child
def checkparent(parent,child):
  #print "Parent %s, child %s"%(parent,child)
  if child==1 or getparentpid(child)==1:
	return True
	  
  currpid=child
#   for(pid=getpid();pid!=0;pid=__getparentpid(pid))
  while getparentpid(currpid)!=1:
	currpid=getparentpid(currpid)
	if currpid==parent:
	  return True
  
  print "External actions with filesystem detected pid of external prog is %d" % child
  return False

# check pid, returns stage of building
def get_stage_by_pid(pid,toppid):
  #return "unknown"

  currpid=proc_helpers.getparentpid(pid)
  try:
	while currpid>1 and currpid!=toppid:
	  cmdlinefile=open("/proc/%d/cmdline" % currpid,"r")
	  cmdline=cmdlinefile.read()
	  cmdlinefile.close()
	  arguments=cmdline.split("\0")
	  #print arguments
	  if len(arguments)>=3 and arguments[1][-9:]=="ebuild.sh":
		return arguments[2]
	  currpid=proc_helpers.getparentpid(currpid)


  except IOError,e:
	return "unknown"

  return "unknown"

# default access filter. Allow acess to all files
def defaultfilter(eventname, filename, pid):
  
  return True

# run the program and get file access events
def getfsevents(prog_name,arguments,approach="hooklib",filterproc=defaultfilter):
  events={}
  # generate a random socketname
  tmpdir = tempfile.mkdtemp()
  socketname = os.path.join(tmpdir, 'socket')

  try:
	#sock_listen=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	sock_listen=socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)

	sock_listen.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	sock_listen.bind(socketname)
	sock_listen.listen(1024)
  except socket.error, e:
    print "Failed to create a socket for exchange data with the logger: %s" % e
    return []
  else:
	#print socketname
	
	pid=os.fork()
	if pid==0:
	  logger=None
	  if approach=="hooklib":
		logger=logger_hooklib.logger(socketname)
	  elif approach=="fusefs":
		logger=logger_fusefs.logger(socketname)
	  else:
		print "Unknown logging approach"
		sys.exit(1)
	  
	  logger.execprog(prog_name,arguments)
	  
	  # should not get here
	  print "Launch likely was unsuccessful"
	  sys.exit(1)
	else:
	  epoll=select.epoll()
	  epoll.register(sock_listen.fileno(), select.EPOLLIN)

	  connects = 0;
	  clients={}
	  stop=0
	  
	  while stop==0:
		sock_events = epoll.poll(3)
		for fileno, sock_event in sock_events:
		  if fileno == sock_listen.fileno():
			ret = sock_listen.accept()
			if ret is None:
			  pass
			else:
			  (client,addr)=ret
			  connects+=1; # client accepted
			  epoll.register(client.fileno(), select.EPOLLIN)
			  clients[client.fileno()]=client
		  elif sock_event & select.EPOLLHUP:
			epoll.unregister(fileno)
			clients[fileno].close()
			del clients[fileno]
			connects-=1
			
			if connects==0:
			  stop=1
			  break
		  elif sock_event & select.EPOLLIN:
			s=clients[fileno]
			record=s.recv(8192)
			
			message=record.split("\0")
			#print message
			
			try:
			  if message[4]=="ASKING":
				if filterproc(message[1],message[2],message[3]):
				  #print "Allowing an access to %s" % message[2]
				  s.sendall("ALLOW"); # TODO: think about flush here
				  
				else:
				  print "Blocking an access to %s" % message[2]
				  s.sendall("DENY"); # TODO: think about flush here
				  
			  else:
				eventname,filename,stage,result=message[1:5]

				if not stage in events:
				  events[stage]=[{},{}]
				
				hashofsucesses=events[stage][0]
				hashoffailures=events[stage][1]
				
				if result=="OK":
				  if not filename in hashofsucesses:
					hashofsucesses[filename]=[False,False]
				  
				  readed_or_writed=hashofsucesses[filename]
				  
				  if eventname=="read":
					readed_or_writed[0]=True
				  elif eventname=="write":
					readed_or_writed[1]=True
					
				elif result[0:3]=="ERR" or result=="DENIED":
				  if not filename in hashoffailures:
					hashoffailures[filename]=[False,False]
				  notfound_or_blocked=hashoffailures[filename]
				  
				  if result=="ERR/2":
					notfound_or_blocked[0]=True
				  elif result=="DENIED":
					notfound_or_blocked[1]=True

				else:
				  print "Error in logger module<->analyser protocol"
				
			except IndexError:
			  print "IndexError while parsing %s"%record

		
		if len(sock_events)==0 and len(clients)==0: 
		#  # seems like there is no connect
		  print "It seems like a logger module was unable to start or failed to finish\n" + \
				"Check that you are not launching a suid program under non-root user."
		  return []
		if len(clients)==0 and iszombie(pid):
		 break

	  os.wait()
  
	  epoll.unregister(sock_listen.fileno())
	  epoll.close()
	  sock_listen.close()
	
  return events

