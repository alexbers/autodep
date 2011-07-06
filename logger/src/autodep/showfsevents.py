#!/usr/bin/env python2

import optparse

import os
import sys

import logfs.fstracer
import logfs.portage_utils

args_parser=optparse.OptionParser("%prog [options] <command>")
args_parser.add_option("-b", "--block",action="store", type="string", 
  dest="packages", default="", help="block an access to files from this packages")
args_parser.add_option("-u", "--unknown", action="store_true", dest="show_unknown", 
  default=False, help="show unknown stage and files from unknown package")
args_parser.add_option("-v", action="store_true", dest="verbose", 
  default=False, help="show accessed files")
args_parser.add_option("-n","--notfound", action="store_true", dest="show_notfound", 
  default=False, help="show not founded files")


args_parser.add_option("--hooklib",action="store_const", dest="approach", 
  const="hooklib", help="use ld_preload logging approach(default)")
args_parser.add_option("--fusefs",action="store_const", dest="approach", 
  const="fusefs", help="use fuse logging approach(slow, but reliable)")
args_parser.set_defaults(approach="hooklib")

args_parser.epilog="Example: %s -b lsof,cowsay emerge bash" % (os.path.basename(sys.argv[0]))

args_parser.disable_interspersed_args()

(options, args) = args_parser.parse_args()
#print options
#print args

if len(args)==0:
  args_parser.print_help()
  exit(1) 

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

events=logfs.fstracer.getfsevents(args[0], args,approach=options.approach,filterproc=filter_function)
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
# old struct is also used during output

#events_converted_for_output={}
packagesinfo={}
#events_converted_for_output=packagesinfo
#otherfilesinfo={}
#events_converted_for_output["otherfilesinfo"]=otherfilesinfo

for stage in sorted(events):
  succ_events=events[stage][0]
  fail_events=events[stage][1]
  
  for filename in succ_events:
	if filename in file_to_package:
	  package=file_to_package[filename]
	else:
	  package="unknown"
	  
	if not package in packagesinfo:
	  packagesinfo[package]={}
	stageinfo=packagesinfo[package]
	if not stage in stageinfo:
	  stageinfo[stage]={}
#	else:
#	  stageinfo=otherfilesinfo
#	  if not stage in stageinfo:
#		stageinfo[stage]={}
	  
	filesinfo=stageinfo[stage]
	if not filename in filesinfo:
	  filesinfo[filename]={"found":[],"notfound":[]}
	filesinfo[filename]["found"]=succ_events[filename]
	
  for filename in fail_events:
	if filename in file_to_package:
	  package=file_to_package[filename]
	else:
	  package="unknown"
	if not package in packagesinfo:
	  packagesinfo[package]={}
	stageinfo=packagesinfo[package]
	if not stage in stageinfo:
	  stageinfo[stage]={}
	#else:
	#  stageinfo=otherfilesinfo
	#  if not stage in stageinfo:
	#	stageinfo[stage]={}
	  
	filesinfo=stageinfo[stage]
	if not filename in filesinfo:
	  filesinfo[filename]={"found":[],"notfound":[]}
	filesinfo[filename]["notfound"]=fail_events[filename]

#print events_converted_for_output

# explicit check for launching with non-emerge application
was_emerge_process=False
for package in packagesinfo:
  if len(packagesinfo[package].keys())>1:
	was_emerge_process=True
	break

# generating output
stagesorder={"clean":1,"setup":2,"unpack":3,"prepare":4,"configure":5,"compile":6,"test":7,
			 "install":8,"preinst":9,"postinst":10,"prerm":11,"postrm":12,"unknown":13}

# print information grouped by package	  
for package in sorted(packagesinfo):
  # not showing special directory package
  if package=="directory":
	continue
  
  if package=="unknown" and not options.show_unknown:
	continue
  
  stages=[]
  for stage in sorted(packagesinfo[package].keys(), key=stagesorder.get):
	if stage!="unknown" or options.show_unknown or not was_emerge_process:
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

	  # this is here for readability
	  action={
		(False,False):"accessed",
		(True,False):"readed",
		(False,True):"writed",
		(True,True):"readed and writed"
	  }

	  for filename in filenames:
		event_info=tuple(filenames[filename])
		print "  %-56s %-21s" % (filename,action[event_info])

# print not founded files with stages
if options.show_notfound:
  filenames={}
  print "\nNot founded files:"
  for stage in sorted(events, key=stagesorder.get):
	print "%s:" % stage
	
	action={
	  (True,False):"file not found",
	  (True,True):"blocked and not found",
	  (False,True):"blocked",
	  (False,False):"other error"
	}

	fail_events=events[stage][1]
	
	for filename in sorted(fail_events, key=file_to_package.get):
	  reason=tuple(fail_events[filename])
	  print "  %-56s %-21s" % (filename,action[reason])
  
