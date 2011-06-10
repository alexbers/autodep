import unittest

import fstracer

def simple_getfsevents(prog,args,approach="hooklib"):
  ret=[]
  events = fstracer.getfsevents(prog,args,approach)
  for event in events:
	ret.append([event[1],event[2],event[4]])
  return ret

class hooklib_simple_tests(unittest.TestCase):
  def test_open_unexists(self):
	self.assertEqual(fstracer.getfsevents('/bin/cat',
	                ['/bin/cat','f1','f2']),
	                [['open', 'f1'], ['open', 'f2']])

  def test_open_exists(self):
	self.assertEqual(fstracer.getfsevents('/bin/cat',
	                ['/bin/cat','/etc/passwd']),
	                [['open', '/etc/passwd']])

  
  def test_open_many(self):
	filesnum=200
	self.assertEqual(fstracer.getfsevents('/bin/cat',
	                ['/bin/cat']+map(lambda x: 'file'+str(x),range(0,filesnum))),
	                map(lambda x: ['open','file'+str(x)],range(0,filesnum)))
  

  def test_parralel(self):
	filesnum=200
	procnum=6
	
	# create command
	command=""
	for p in xrange(0,procnum):
  	  command+="/bin/cat "
	  for f in xrange(0,filesnum):
		command+="file_%d_%d " % (p,f)
	  command+="& "
	command+=" >/dev/null 2>&1"
	command+=" "+"A"*65536
	
	
	resultarray=fstracer.getfsevents('/bin/sh', ['/bin/sh','-c',command])

	self.assertTrue(resultarray.count(['execve', '/bin/cat'])==procnum)

	print resultarray

	for p in xrange(0,procnum):
	  for f in xrange(0,filesnum):
		self.assertTrue(resultarray.count(['open', 'file_%d_%d' % (p,f)])==1)

class fusefs_simple_tests(unittest.TestCase):
  def test_open_unexists(self):
	eventslist=simple_getfsevents('/bin/cat', ['/bin/cat','/f1','/f2'],approach="fusefs")
	print eventslist
	self.assertTrue(eventslist.count(['stat', '/f1',"ERR/2"])==1)
	self.assertTrue(eventslist.count(['stat', '/f2',"ERR/2"])==1)
	
  def test_open_exists(self):
	  eventslist=simple_getfsevents('/bin/cat', ['/bin/cat','/etc/passwd'],approach="fusefs")
	  self.assertTrue(eventslist.count(['stat', '/etc/passwd',"OK"])>=1)

  def test_open_many(self):
	filesnum=200
	eventslist=simple_getfsevents('/bin/cat',['/bin/cat']+
									map(lambda x: '/file'+str(x),range(0,filesnum)), approach="fusefs")
	for f in map(lambda x: ['stat','/file'+str(x),"ERR/2"],range(0,filesnum)):
	  self.assertTrue(f in eventslist)

  def test_parralel(self):
	filesnum=200
	procnum=6
	
	# create command
	command=""
	for p in xrange(0,procnum):
  	  command+="/bin/cat "
	  for f in xrange(0,filesnum):
		command+="/file_%d_%d " % (p,f)
	  command+="& "
	command+=" 2>/dev/null"
	#command+=" "+"A"*65536
	
	resultarray=simple_getfsevents('/bin/sh', ['/bin/sh','-c',command],approach="fusefs")

	for p in xrange(0,procnum):
	  for f in xrange(0,filesnum):
		self.assertTrue(resultarray.count(['stat', '/file_%d_%d' % (p,f),"ERR/2"])==1)

  def test_open_very_many(self):
	resultarray=simple_getfsevents('/bin/sh', ['/bin/sh','-c',
									"for i in `seq 1 10`; do cat /testmany;done 2> /dev/null"],approach="fusefs")
	print resultarray
	self.assertTrue(resultarray.count(['stat','/testmany',"ERR/2"])>=1000)

if __name__ == '__main__':
  #unittest.main()
  suite = unittest.TestLoader().loadTestsFromTestCase(fusefs_simple_tests)
  unittest.TextTestRunner(verbosity=2).run(suite)