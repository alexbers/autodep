#!/usr/bin/env python2
"""
This module is a bridge between low-level logging services and high level handling dependency logic
"""

import os
import time
import tempfile
import socket
import select


# run the program and get file access events
def getfsevents(prog_name,arguments):
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
    return ()
  else:
	print socketname
	
	pid=os.fork()
	if pid==0: 
	  # wait while the socket opens
	  os.execvpe(prog_name, arguments,{
		"LD_PRELOAD":"/home/bay/gsoc/logger/src/hook_lib/file_hook.so",
		"LOG_SOCKET":socketname
	  })
	  print "Failed to launch the programm"
	  os.exit(0)
	else:
	  input = [sock_listen]
	  connects = 0;
	  
	  while input:
		inputready,outputready,exceptready = select.select(input,[],[])
		
		for s in inputready:
		  if s == sock_listen:
			ret = s.accept()
			if ret is None:
			  pass
			else:
			  (client,addr)=ret
			  print "Client accepted\n";
			  connects+=1;
			  input.append(client)
		  else:
			data = s.recv(8192)
			if data:
			  print data
			else:
			  s.close()
			  input.remove(s)
			  connects-=1;
			  if connects==0:
				input.remove(sock_listen)		
	  os.wait()
	
