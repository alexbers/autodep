#!/usr/bin/env python

# color output support
class color_printer:
#  HEADER = '\033[95m'
#  OKBLUE = '\033[94m'
#  OKGREEN = '\033[92m'
#  FAIL = '\033[91m'

  def __init__(self, enable_colors=True):
	if enable_colors:
	  self.COLOR2CODE={"warning":'\033[91m', "text":'\033[90m'}
	  self.ENDCOLOR='\033[0m'
	else:
	  self.COLOR2CODE={"warning":'', "text":''}
	  self.ENDCOLOR=''
  def printmsg(self,importance,msg):
	print self.COLOR2CODE[importance] + str(msg) + self.ENDCOLOR,
