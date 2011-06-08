#!/usr/bin/env python2

import os
import sys

import logfs.fstracer

#logfs.fstracer.getfsevents("/bin/sh", ["sh" , "-c", "/usr/bin/tac bay_success; /usr/bin/tac bay_god bay_god2"])
#events=logfs.fstracer.getfsevents("/bin/cat", ["cat" , "l l l"])
if len(sys.argv)<2:
  print "Usage: showfsevents.py <command>"
  exit(1)
  
events=logfs.fstracer.getfsevents(sys.argv[1], sys.argv[1:])
print events
#logfs.fstracer.getfsevents("emerge", ["emerge","--info"])