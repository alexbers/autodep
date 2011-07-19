#!/usr/bin/env python2

import os
import subprocess
import re

def getpackagesbyfiles(files):
  ret={}
  listtocheck=[]
  for f in files:
	if os.path.isdir(f):
	  ret[f]="directory"
	else:
	  listtocheck.append(f)
	  
  try:
	proc=subprocess.Popen(['qfile']+['--nocolor','--exact','','--from','-'],
	  stdin=subprocess.PIPE, stdout=subprocess.PIPE,stderr=subprocess.PIPE, 
	  bufsize=4096)
	
	out,err=proc.communicate("\n".join(listtocheck))
	if err!=None:
	  print "Noncritical error while launch qfile %s"%err;

	lines=out.split("\n")
	#print lines
	line_re=re.compile(r"^([^ ]+)\s+\(([^)]+)\)$")
	for line in lines:
	  if len(line)==0:
		continue
	  match=line_re.match(line)
	  if match:
		ret[match.group(2)]=match.group(1)
	  else:
		print "Util qfile returned unparsable string: %s" % line

  except OSError,e:
	print "Error while launching qfile: %s" % e
	
	
  return ret
  
def getfilesbypackage(packagename):
  ret=[]
  try:
	proc=subprocess.Popen(['qlist']+['--nocolor',"--obj",packagename],
	  stdout=subprocess.PIPE,stderr=subprocess.PIPE, 
	  bufsize=4096)
	
	out,err=proc.communicate()
	if err!=None and len(err)!=0 :
	  print "Noncritical error while launch qlist: %s" % err;
	
	ret=out.split("\n")
	if ret==['']:
	  ret=[]
  except OSError,e:
	print "Error while launching qfile: %s" % e

  return ret
  
   
   