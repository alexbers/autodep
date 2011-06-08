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

import logger_hooklib
import logger_fusefs


def unescape(s):
  s=re.sub(r'\\r', '\r', s)
  s=re.sub(r'\\n', '\n', s)
  s=re.sub(r'\\(.)', r'\1', s)
  return s

def parse_message(message):
  ret=re.split(r"(?<![\\]) ",message,6)
  #for i in ret:
	
  return map(unescape, ret)

# check if proccess is finished
def checkfinished(pid):
  if not os.path.exists("/proc/%d/stat" % pid):
	return (True,0)

  try:
	pid_child,exitcode = os.waitpid(pid, os.WNOHANG)
  except OSError, e:
	if e.errno == 10: 
	  return (False,0)
	else:
	  raise
  
  if pid_child==0:
	return (False,0)
  return (True,exitcode)
  

# run the program and get file access events
def getfsevents(prog_name,arguments,approach="hooklib"):
  events=[]
  # generate a random socketname
  tmpdir = tempfile.mkdtemp()
  socketname = os.path.join(tmpdir, 'socket')

  try:
	sock_listen=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
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
	  input = [sock_listen]
	  connects = 0;
	  
	  buffers = {}
	  
	  while input:
		inputready,outputready,exceptready = select.select(input,[],[],5)
		
		for s in inputready:
		  #print "!! len: %d" % len(buffers)
		  if s == sock_listen:
			ret = s.accept()
			if ret is None:
			  pass
			else:
			  (client,addr)=ret
			  connects+=1; # client accepted
			  input.append(client)
			  buffers[client]=''
		  else:
			data=s.recv(65536)
			
			buffers[s]+=data
			  
			if not data:
			  s.close()
			  input.remove(s)
			  buffers[s]=""
			  connects-=1;
			  if connects==0:
				input.remove(sock_listen)
				sock_listen.close()
			  continue
			
			
			if not "\n" in buffers[s]:
			  continue
			  
			data,buffers[s] = buffers[s].rsplit("\n",1)
			
			for record in data.split("\n"):
			  if len(record)==0:
				continue
			  # TODO: check array
			  #print "!"+"%d"%len(record)+"?"
			  #print "datalen: %d" % len(data)
			  message=parse_message(record)
			  try:
				events.append([message[1],message[2]]);
			  except IndexError:
				print "IndexError while parsing %s"%record
			  #print "!!"+data+"!!"
			  #print parse_message(data)

		if len(input)==1 and connects==0:
		  # seems like there is no connect
		  print "It seems like a logger module was unable to start." + \
				"Check that you are not launching a suid program under non-root user."
		  return []

	  os.wait()
  return events
