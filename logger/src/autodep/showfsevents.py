#!/usr/bin/env python2

import os
import sys

import logfs.fstracer

def printevents(events):
  for event in events:
	print "%s %s"%(event[1],event[2])
	
#logfs.fstracer.getfsevents("/bin/sh", ["sh" , "-c", "/usr/bin/tac bay_success; /usr/bin/tac bay_god bay_god2"])
#events=logfs.fstracer.getfsevents("/bin/cat", ["cat" , "l l l"])
if len(sys.argv)<2:
  print "Usage: showfsevents.py <command>"
  exit(1)
  
events=logfs.fstracer.getfsevents(sys.argv[1], sys.argv[1:],approach="fusefs")
#print events

for stage in events:
  succ_events=events[stage][0]
  fail_events=events[stage][1]
  print "On stage %s:" % stage
  for filename in sorted(succ_events):
	print " %40s\t" % filename,
	if succ_events[filename]==[False,False]:
	  print "accessed"
	elif succ_events[filename]==[True,False]:
	  print "readed"
	elif succ_events[filename]==[False,True]:
	  print "writed"
	elif succ_events[filename]==[True,True]:
	  print "readed and writed"
  for filename in sorted(fail_events):
	
	print " %40s\t"%filename,
	if fail_events[filename]==[True,False]:
	  print "file not found"
	elif fail_events[filename]==[True,True]:
	  print "blocked somewhen, not found somewhen"
	elif fail_events[filename]==[False,True]:
	  print "blocked"
	elif fail_events[filename]==[False,False]:
	  print "other error"
	
##logfs.fstracer.getfsevents("emerge", ["emerge","--info"])