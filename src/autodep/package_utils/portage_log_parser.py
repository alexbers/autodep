#!/usr/bin/env python2
# parse log and get packages actually merged

import re
import time

# the log path seems to be always on that path
# see <portage_lib_path>/pym/_emerge/emergelog.py
log_path='/var/log/emerge.log'

def get_list_of_merged_packages(starttime=0,endtime=-1):
  ret=[]
  try:
	log=open(log_path)
	
	found_begining=False
	current_package=''
	current_package_num=0
	total_packages_num=0

	expect_start=True
	expect_end=False

	for line in log:
	  if ':' not in line: # skipping bad strings
		continue
	  
	  msgtime,msgtext=line.split(':',1)
	  msgtime,msgtext=int(msgtime),msgtext.strip()
	  if msgtime<starttime:
		continue
	  if endtime!=-1 and msgtime>endtime:
		continue
	  
	  if msgtext.startswith("Started emerge on: "):
		# doing an additional check: we have msg like:
		# 1310909507: Started emerge on: Jul 17, 2011 13:31:47
		# we want to make sure that two variants of time is not
		# distinguish more than on 2 days(local timezone may be changed)
		# we protect self from malformed and broken log files
		msg, date = msgtext.split(": ",1)
		msgtime2=time.mktime(time.strptime(date,"%b %d, %Y %H:%M:%S"))
		if abs(msgtime-msgtime2)>60*60*24*2:
		  continue
		found_begining=True
	  if not found_begining:
		continue
	  
	  if expect_start and msgtext.startswith(">>> emerge "):
		# string example: >>> emerge (1 of 1) sys-process/lsof-4.84 to /
		m=re.search(r">>> emerge \((\d+) of (\d+)\) (\S+) to",msgtext)
		if m:
		  pkgnum,total_pkgnum,pkgname=m.group(1),m.group(2), m.group(3)
		  if total_packages_num==0:
			total_packages_num=total_pkgnum
		  elif total_packages_num!=total_pkgnum:
			continue
		  current_package_num=pkgnum
		  current_package=pkgname
		  expect_start=False
		  expect_end=True
	  elif expect_end and msgtext.startswith(
		"::: completed emerge (" + current_package_num + " of " + 
		total_packages_num + ") " + current_package):
		ret.append(current_package)
		if total_packages_num==current_package_num:
		  break
		expect_start=True
		expect_end=False

	log.close()
	return ret
	
  except IOError,e:
	print "Error while opening logfile. %s" % e
  return []
  