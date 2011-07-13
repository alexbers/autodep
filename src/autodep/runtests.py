#!/usr/bin/env python2

#import test.test_fstracer

import unittest
import test.all_tests
testSuite = test.all_tests.create_test_suite()
text_runner = unittest.TextTestRunner().run(testSuite)