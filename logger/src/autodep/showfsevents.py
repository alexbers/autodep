#!/usr/bin/env python2

import optparse

import os
import sys

import logfs.fstracer
import logfs.portage_utils

#logfs.fstracer.getfsevents("/bin/sh", ["sh" , "-c", "/usr/bin/tac bay_success; /usr/bin/tac bay_god bay_god2"])
#events=logfs.fstracer.getfsevents("/bin/cat", ["cat" , "l l l"])
#if len(sys.argv)<2:
#  print "Usage: showfsevents.py <command>"
#  exit(1)
  
args_parser=optparse.OptionParser("%prog [options] <command>")
args_parser.add_option("-v", action="store_true", dest="verbose", default=False, help="show accessed files")

(options, args) = args_parser.parse_args()
print args

events=logfs.fstracer.getfsevents(args[0], args,approach="hooklib")
print "Program finished, analyzing dependencies"

# get unique filenames
filenames={}
for stage in events:
  succ_events=events[stage][0]
  fail_events=events[stage][1]
  for filename in succ_events:
	filenames[filename]=""
  for filename in fail_events:
	filenames[filename]=""
filenames=filenames.keys();

# temporary disabled
file_to_package=logfs.portage_utils.getpackagesbyfiles(filenames)
#file_to_package={}
#print events

# this part is completly unreadable. It converting one complex struct(returned with getfsevents) to
# another which good for user

events_converted_for_output={}
packagesinfo={}
events_converted_for_output["packagesinfo"]=packagesinfo
otherfilesinfo={}
events_converted_for_output["otherfilesinfo"]=otherfilesinfo

for stage in sorted(events):
  succ_events=events[stage][0]
  fail_events=events[stage][1]
  
  for filename in succ_events:
	if filename in file_to_package:
	  package=file_to_package[filename]
	  if not package in packagesinfo:
		packagesinfo[package]={}
	  stageinfo=packagesinfo[package]
	  if not stage in stageinfo:
		stageinfo[stage]={}
	else:
	  stageinfo=otherfilesinfo
	  if not stage in stageinfo:
		stageinfo[stage]={}
	  
	filesinfo=stageinfo[stage]
	if not filename in filesinfo:
	  filesinfo[filename]={"found":[],"notfound":[]}
	filesinfo[filename]["found"]=succ_events[filename]
	
  for filename in fail_events:
	if filename in file_to_package:
	  package=file_to_package[filename]
	  if not package in packagesinfo:
		packagesinfo[package]={}
	  stageinfo=packagesinfo[package]
	  if not stage in stageinfo:
		stageinfo[stage]={}
	else:
	  stageinfo=otherfilesinfo
	  if not stage in stageinfo:
		stageinfo[stage]={}
	  
	filesinfo=stageinfo[stage]
	if not filename in filesinfo:
	  filesinfo[filename]={"found":[],"notfound":[]}
	filesinfo[filename]["notfound"]=fail_events[filename]

#print events_converted_for_output
	  
stagesorder={"clean":1,"setup":2,"unpack":3,"prepare":4,"configure":5,"compile":6,"test":7,
			 "install":8,"preinst":9,"postinst":10,"prerm":11,"postrm":12,"unknown":13}

	  
for package in sorted(packagesinfo):
  print "%-40s: %s"%(package,packagesinfo[package].keys())
	
  
"""
for stage in sorted(events, key=stagesorder.get):
  succ_events=events[stage][0]
  fail_events=events[stage][1]
  print "On stage %s:" % stage
  for filename in sorted(succ_events, key=file_to_package.get):
	print " %-40s" % filename,
	action=""
	if succ_events[filename]==[False,False]:
	  action="accessed",
	elif succ_events[filename]==[True,False]:
	  action="readed",
	elif succ_events[filename]==[False,True]:
	  action="writed",
	elif succ_events[filename]==[True,True]:
	  action="readed and writed",
	  
	print "  %-21s  " % action,
	if filename in file_to_package:
	  print file_to_package[filename],
	print  
	
  for filename in sorted(fail_events, key=file_to_package.get):
	print " %-40s" % filename,
	action=""
	if fail_events[filename]==[True,False]:
	  action="file not found"
	elif fail_events[filename]==[True,True]:
	  action="blocked and not found"
	elif fail_events[filename]==[False,True]:
	  action="blocked"
	elif fail_events[filename]==[False,False]:
	  action="other error"
	print "  %-21s  " % action,
	if filename in file_to_package:
	  print file_to_package[filename],
	print  
	"""
##logfs.fstracer.getfsevents("emerge", ["emerge","--info"])