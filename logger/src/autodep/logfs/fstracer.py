#!/usr/bin/env python2
"""
This module is a bridge between low-level logging services and high level handling dependency logic
"""

import os
import time
import tempfile
import socket
import asyncore

class socket_selecter(asyncore.dispatcher):
  connects=0;
  
  def __init__(self,path):
	asyncore.dispatcher.__init__(self)
	self.path=path
	self.create_socket(socket.AF_UNIX, socket.SOCK_STREAM)
	self.set_reuse_addr()
	self.bind(self.path)
	self.listen(128)
	
  def handle_accept(self):
	ret = self.accept()
	if ret is None:
	  pass
	else:
	  (sock,addr)=ret
	  print "Client accepted\n"
	  self.connects+=1
	  handler = log_handler(sock,addr,self)
	  print "After client accepted connects=%s\n" % self.connects


class log_handler(asyncore.dispatcher_with_send):

  def __init__(self, sock, addr,listen_socket_dispatcher):
	asyncore.dispatcher_with_send.__init__(self, sock)
	self.addr = addr
	self.buffer = ''
	self.listen_sock_dispatcher=listen_socket_dispatcher

  def handle_read(self):
	print self.recv(8192)

  def writable(self):
	return (len(self.buffer) > 0)

  def handle_write(self):
	pass
	#self.send(self.buffer)
	#self.buffer = ''

  def handle_close(self):
	print "Client closed the socket\n"
	self.listen_sock_dispatcher.connects-=1
	if self.listen_sock_dispatcher.connects == 0:
	  #pass
	  self.listen_sock_dispatcher.close()
	self.close()
	



# run the program and get file access events
def getfsevents(prog_name,arguments):
  # generating a random socketname
  tmpdir = tempfile.mkdtemp()
  socketname = os.path.join(tmpdir, 'socket')

  try:
	pass
    #os.mkfifo(fifoname)
  except OSError, e:
    print "Failed to create a socket for exchange data with logger: %s" % e
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
	  os.exit(0)
	else:
	  server = socket_selecter(socketname)
	  #fifo = open(fifoname, 'r')
	  
	  try:
		asyncore.loop()
	  finally:
		if os.path.exists(server.path):
		  os.unlink(server.path)
		os.wait()
	  
	  pass
	
	
