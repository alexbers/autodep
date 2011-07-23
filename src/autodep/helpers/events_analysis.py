#!/usr/bin/env python

# some heuristics here to cut few packets
def is_package_useful(pkg,stages,files):
  # test 1: package is not useful if all files are *.desktop or *.xml or *.m4
  for f in files:
	if not (f.endswith(".desktop") or f.endswith(".xml") or f.endswith(".m4")):
	  break
  else:
	return False
	
  return True
  
  