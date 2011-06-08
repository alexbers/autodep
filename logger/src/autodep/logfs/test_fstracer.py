import unittest

import fstracer

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
	eventslist=fstracer.getfsevents('/bin/cat', ['/bin/cat','/f1','/f2'],approach="fusefs")
	self.assertTrue(eventslist.count(['stat', '/f1'])==1)
	def test_open_exists(self):
	  eventslist=fstracer.getfsevents('/bin/cat', ['/bin/cat','/etc/passwd'],approach="fusefs")
	  self.assertTrue(eventslist.count(['stat', '/etc/passwd'])>=1)

  def test_open_many(self):
	filesnum=200
	eventslist=fstracer.getfsevents('/bin/cat',['/bin/cat']+
									map(lambda x: '/file'+str(x),range(0,filesnum)), approach="fusefs")
	for f in map(lambda x: ['stat','/file'+str(x)],range(0,filesnum)):
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
	command+=" >/dev/null 2>&1"
	#command+=" "+"A"*65536
	
	resultarray=fstracer.getfsevents('/bin/sh', ['/bin/sh','-c',command],approach="fusefs")

	#self.assertTrue(resultarray.count(['execve', '/bin/cat'])==procnum)

	print resultarray

	for p in xrange(0,procnum):
	  for f in xrange(0,filesnum):
		self.assertTrue(resultarray.count(['stat', '/file_%d_%d' % (p,f)])==1)




if __name__ == '__main__':
  #unittest.main()
  suite = unittest.TestLoader().loadTestsFromTestCase(fusefs_simple_tests)
  unittest.TextTestRunner(verbosity=2).run(suite)