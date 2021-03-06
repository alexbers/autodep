import unittest
import logfs.fstracer
import os
import sys

class null_writer:
  def __init__(self):
	pass
  def write(self,string):
	pass


def simple_getfsevents(prog,args,approach="fusefs"):
  ret=[]
  null_out=null_writer()
  nullfile=open("/dev/null")
  nullfd=nullfile.fileno()
  outfd=os.dup(sys.stdout.fileno())
  errfd=os.dup(sys.stderr.fileno())
  os.dup2(nullfd,sys.stdout.fileno())
  os.dup2(nullfd,sys.stderr.fileno())
  
  events = logfs.fstracer.getfsevents(prog,args,approach)
  os.dup2(outfd,sys.stdout.fileno())
  os.dup2(errfd,sys.stderr.fileno())
  
  #print events
  for stage in events:
	for filename in events[stage][0]:
	  ret.append([filename,'success'])
	for filename in events[stage][1]:
	  ret.append([filename,'fail'])
	  
  return ret



class fusefs_simple_tests(unittest.TestCase):
  def test_open_unexists(self):
	eventslist=simple_getfsevents('/bin/cat', ['/bin/cat','/f1','/f2'])
	self.assertTrue(eventslist.count(['/f1',"fail"])==1)
	self.assertTrue(eventslist.count(['/f2',"fail"])==1)
	
  def test_open_exists(self):
	  eventslist=simple_getfsevents('/bin/cat', ['/bin/cat','/etc/passwd'])
	  self.assertTrue(eventslist.count(['/etc/passwd','success'])>=1)

  def test_open_many(self):
	filesnum=200
	eventslist=simple_getfsevents('/bin/cat',['/bin/cat']+
									map(lambda x: '/file'+str(x),range(0,filesnum)))
	for f in map(lambda x: ['/file'+str(x),'fail'],range(0,filesnum)):
	  self.assertTrue(f in eventslist)

  def test_parralel(self):
	filesnum=400
	procnum=8
	
	# create command
	command=""
	for p in xrange(0,procnum):
  	  command+="/bin/cat "
	  for f in xrange(0,filesnum):
		command+="/file_%d_%d " % (p,f)
	  command+="& "
	command+=" 2>/dev/null"
	#command+=" "+"A"*65536
	
	resultarray=simple_getfsevents('/bin/sh', ['/bin/sh','-c',command])

	for p in xrange(0,procnum):
	  for f in xrange(0,filesnum):
		self.assertTrue(resultarray.count(['/file_%d_%d' % (p,f),"fail"])==1)

  def test_open_very_many(self):
	resultarray=simple_getfsevents('/bin/sh', ['/bin/sh','-c',
									"for i in `seq 1 1000`; do cat /testmany$i;done 2> /dev/null"])
	#print resultarray
	for i in range(1,1000):
	  self.assertTrue(resultarray.count(['/testmany'+str(i),'fail'])==1)

  def test_exec(self):
	eventslist=simple_getfsevents('tests/helpers/exec', ['test/helpers/exec'])
	for i in range(1,14):
	  self.assertTrue(eventslist.count(['/f'+str(i),"fail"])==1)

#if __name__ == '__main__':
  #unittest.main()
  #suite = unittest.TestLoader().loadTestsFromTestCase(fusefs_simple_tests)
#  suite = unittest.TestLoader().loadTestsFromTestCase(hooklib_simple_tests)
#  unittest.TextTestRunner(verbosity=2).run(suite)