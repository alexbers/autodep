#!/usr/bin/env python2

import os

import logfs.fstracer

logfs.fstracer.getfsevents("/bin/sh", ["sh" , "-c", "/usr/bin/tac bay_success; /usr/bin/tac bay_god bay_god2"])
#logfs.fstracer.getfsevents("emerge", ["emerge","--info"])