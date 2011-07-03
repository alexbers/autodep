#!/usr/bin/env python2

import optparse

import os
import sys

import logfs.fstracer
import logfs.portage_utils

args_parser=optparse.OptionParser("%prog [options] <command>")
args_parser.add_option("-v", action="store_true", dest="verbose", 
  default=False, help="show accessed files")
args_parser.add_option("-u", "--unknown", action="store_true", dest="show_unknown_stage", 
  default=False, help="show unknown stage")
args_parser.add_option("-b", "--block",action="store", type="string", 
  dest="packages", default="", help="block an access to files from this packages")
args_parser.epilog="Example: %s -b lsof,cowsay emerge bash" % (os.path.basename(sys.argv[0]))

args_parser.disable_interspersed_args()

(options, args) = args_parser.parse_args()

if len(args)==0:
  args_parser.print_help()
  exit(1) 
#print args
#print options

filter_function=lambda eventname,filename,stage: True

# handling --block
if options.packages:
  packages=options.packages.split(",")
  files_to_block=[]
  for package in packages:
	files_in_package=logfs.portage_utils.getfilesbypackage(package)
	if len(files_in_package)==0:
	  print "Bad package name: %s. Exiting" % package
	  exit(1)
	files_to_block+=files_in_package
  files_to_block={}.fromkeys(files_to_block)
  # new filter function
  def filter(eventname,filename,stage):
	return not filename in files_to_block
  filter_function=filter

events=logfs.fstracer.getfsevents(args[0], args,approach="fusefs",filterproc=filter_function)
print "Program finished, analyzing dependencies"

# get unique filenames
filenames={}
for stage in events:
  succ_events=events[stage][0]
  fail_events=events[stage][1]
  for filename in succ_events:
	filenames[filename]=None
  for filename in fail_events:
	filenames[filename]=None
filenames=filenames.keys();

# temporary disabled
file_to_package=logfs.portage_utils.getpackagesbyfiles(filenames)
#file_to_package={}
#print events

# this part is completly unreadable. It converting one complex struct(returned by getfsevents) to
# another complex struct which good for generating output

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
  # not showing special directory package
  if package=="directory":
	continue
  
  stages=[]
  for stage in sorted(packagesinfo[package].keys(), key=stagesorder.get):
	if stage!="unknown" or options.show_unknown_stage:
	  stages.append(stage)

  if len(stages)!=0:
	print "%-40s: %s"%(package,stages)
	# show information about accessed files
	if options.verbose:
	  filenames={}
	  for stage in stages:
		for filename in packagesinfo[package][stage]:
		  if len(packagesinfo[package][stage][filename]["found"])!=0:
			was_readed,was_writed=packagesinfo[package][stage][filename]["found"]
			if not filename in filenames:
			  filenames[filename]=[was_readed,was_writed]
			else:
			  old_was_readed, old_was_writed=filenames[filename]
			  filenames[filename]=[old_was_readed | was_readed, old_was_writed | was_writed ]
			  
	  for filename in filenames:
		if filenames[filename]==[False,False]:
		  action="accessed"
		elif filenames[filename]==[True,False]:
		  action="readed"
		elif filenames[filename]==[False,True]:
		  action="writed"
		elif filenames[filename]==[True,True]:
		  action="readed and writed"
		print "  %-56s %-21s" % (filename,action)
			  
	
  
"""
for stage in sorted(events, key=stagesorder.get):
  succ_events=events[stage][0]-
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