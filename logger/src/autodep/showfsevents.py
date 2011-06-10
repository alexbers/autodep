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

succ_events=[]
err_events=[]
deny_events=[]

for event in events:
  if event[4]=="OK":
	succ_events.append(event)
  elif event[4]=="DENIED":
	deny_events.append(event)
  else:
	err_events.append(event)
	
  
	
print "Report:"	
if len(succ_events)>0:
  print "Successful events:"
  printevents(succ_events)
if len(err_events)>0:
  print "\nNon-successful events:"
  printevents(err_events)
if len(deny_events)>0:
  print "\nBlocked events:"
  printevents(deny_events)
#logfs.fstracer.getfsevents("emerge", ["emerge","--info"])