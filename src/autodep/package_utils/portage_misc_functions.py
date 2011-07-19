#!/usr/bin/env python2
# Thanks to Zac Medico <zmedico@gentoo.org> for working example of using an api

import portage
from portage.dbapi._expand_new_virt import expand_new_virt

# to not use own emerge option parser. Options may change but I hope 
# parse_opts function will always be there
from _emerge.main import parse_opts


class portage_api:
  def __init__(self):
	self.settings=portage.config(clone=portage.settings)
	self.vartree=portage.db[portage.root]['vartree']
	self.vardb=self.vartree.dbapi
	self.portdb=portage.portdb
	self.metadata_keys = [k for k in portage.auxdbkeys if not k.startswith("UNUSED_")]
	self.use=self.settings["USE"]

  # recursive dependency getter
  def get_deps(self,pkg,dep_type=["RDEPEND","DEPEND"]):
	#pkg="kde-meta"
	#print self.vardb.match("<sys-apps/paludis-0.26.0_alpha5")
	#metadata = dict(zip(self.metadata_keys, self.vardb.aux_get(pkg, self.metadata_keys)))
	
	ret=set()

	pkg = self.portdb.xmatch("bestmatch-visible", pkg)	
	if not pkg:
	  return ret

	#print pkg

	known_packages=set()
	unknown_packages={pkg}
	
	while unknown_packages:
	  p=unknown_packages.pop()
	  #print "proceeding "+p
	  if p in known_packages:
		continue
	  known_packages.add(p)

	  #print self.metadata_keys, p,self.portdb.aux_get(p, self.metadata_keys)
	  metadata = dict(zip(self.metadata_keys, self.vardb.aux_get(p, self.metadata_keys)))
	  #print "proceeding2 "+p

	  dep_str = " ".join(metadata[k] for k in dep_type)
	  
	  success, atoms = portage.dep_check(dep_str, None, self.settings, myuse=self.use.split(),
		trees=portage.db, myroot=self.settings["ROOT"])

	  #print atoms
	  if not success:
		continue

	  for atom in atoms:
		atomname = self.vartree.dep_bestmatch(atom)
		#print atomname
		if not atomname:
		  continue
		
		for unvirt_pkg in expand_new_virt(self.vartree.dbapi,'='+atomname):
		  for pkg in self.vartree.dep_match(unvirt_pkg):
			ret.add(pkg)
			unknown_packages.add(pkg)
	return ret

  # returns all packages from system set. They are always implicit dependencies
  def get_system_packages_list(self):
	ret=[]
	for atom in self.settings.packages:
	  for pre_pkg in self.vartree.dep_match(atom):
		for unvirt_pkg in expand_new_virt(self.vartree.dbapi,'='+pre_pkg):
		  for pkg in self.vartree.dep_match(unvirt_pkg):
			ret.append(pkg)
	return ret

  # call emerge arguments parser
  def parse_emerge_args(self,args):
	action, opts, files = parse_opts(args, silent=True)
	return action, opts, files

