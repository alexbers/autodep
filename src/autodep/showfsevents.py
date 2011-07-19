#!/usr/bin/env python2

import optparse

import os
import sys
import time

import logfs.fstracer
import package_utils.portage_utils
import package_utils.portage_misc_functions
import package_utils.portage_log_parser

portage_api=package_utils.portage_misc_functions.portage_api()
portage_api=package_utils.portage_misc_functions.portage_api()

runtime_vars={} # This is here mainly for grouping. We are trying to 
				# get as much data about an environment as possible
runtime_vars["starttime"]=int(time.time())
#print package_utils.portage_log_parser.get_list_of_merged_packages(1244256830)

#quit(1)


#system_packages = deps_finder.get_system_packages_list()
#print "sys-libs/glibc-2.13-r2" in system_packages
#print deps_finder.get_deps('bash')

#print(runtime_vars["starttime"])
#quit(1)

args_parser=optparse.OptionParser("%prog [options] <command>")
args_parser.add_option("-b", "--block",action="store", type="string", 
  dest="packages", default="", help="block an access to files from this packages")
args_parser.add_option("-f","--files", action="store_true", dest="show_files", 
  default=False, help="show accessed files and not founded files")
args_parser.add_option("-v","--verbose", action="store_true", dest="verbose", 
  default=False, help="show non-important packages, "
	"show unknown package and unknown stage")
args_parser.add_option("-C","--nocolor",action="store_true", dest="nocolor", 
  default=False, help="don't output color")

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

if args[0]=="emerge":
  runtime_vars["is_emerge"]=True
  emergeaction ,emergeopts, emergefiles=portage_api.parse_emerge_args(args[1:])
  runtime_vars["emerge_parameters"]=(emergeaction ,emergeopts, emergefiles)
  if len(emergefiles)>1:
	print "Please, install packages one by one to get more accurate reports"
else:
  runtime_vars["is_emerge"]=False
  

filter_function=lambda eventname,filename,stage: True

# handling --block
if options.packages:
  packages=options.packages.split(",")
  files_to_block=[]
  for package in packages:
	files_in_package=package_utils.portage_utils.getfilesbypackage(package)
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
runtime_vars["endtime"]=int(time.time())

if runtime_vars["is_emerge"]:
  # try to get information about packages merged sucessfully
  try:
	pkgs=package_utils.portage_log_parser.get_list_of_merged_packages(
		  runtime_vars["starttime"],runtime_vars["endtime"]
		 )
	if len(pkgs) > 1:
	  print "Several packages were installed. The report will be inaccurate"
	runtime_vars["pkgs_installed"]=pkgs
	runtime_vars["deps_buildtime"]=[]
	runtime_vars["deps_all"]=[]
	for pkg in pkgs:
	  runtime_vars["deps_buildtime"]+=portage_api.get_deps(pkg,["DEPEND"])
	  runtime_vars["deps_all"]+=portage_api.get_deps(pkg,["DEPEND","RDEPEND"])
	
	print runtime_vars["deps_buildtime"]
	print runtime_vars["deps_all"]
  except:
	print "Non-critical error while parsing logfile of emerge"
	runtime_vars["is_emerge"]=False # shutting down all emerge handling logic
  pass

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
file_to_package=package_utils.portage_utils.getpackagesbyfiles(filenames)
#file_to_package={}
#print events

# This part is completly unreadable. 
# It converting one complex struct(returned by getfsevents) to another complex
# struct which good for generating output.
#
# Old struct is also used during output

packagesinfo={}

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
	  
	filesinfo=stageinfo[stage]
	if not filename in filesinfo:
	  filesinfo[filename]={"found":[],"notfound":[]}
	filesinfo[filename]["notfound"]=fail_events[filename]

#print events_converted_for_output

# generating output
stagesorder={"clean":1,"setup":2,"unpack":3,"prepare":4,"configure":5,"compile":6,"test":7,
			 "install":8,"preinst":9,"postinst":10,"prerm":11,"postrm":12,"unknown":13}

deps_finder=package_utils.portage_misc_functions.portage_api()
system_packages = deps_finder.get_system_packages_list()

# print information grouped by package	  
for package in sorted(packagesinfo):
  # not showing special directory package
  if package=="directory":
	continue
  
  if package=="unknown" and not options.verbose:
	continue

  if package in system_packages and not options.verbose:
	continue

  is_attention_pkg=runtime_vars["is_emerge"] and package not in runtime_vars["deps_all"]
  

  stages=[]
  for stage in sorted(packagesinfo[package].keys(), key=stagesorder.get):
	if stage!="unknown" or options.verbose or not runtime_vars["is_emerge"]:
	  stages.append(stage)

  if len(stages)!=0:
	
	print "%s %-40s: %s"%(is_attention_pkg,package,stages)
	# show information about accessed files
	if options.show_files:
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
if options.show_files:
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
  
