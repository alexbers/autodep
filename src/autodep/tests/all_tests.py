import glob
import unittest

# change it if you don't want get all tests runned
testsglob='tests/test_*.py'

def create_test_suite():
    test_file_strings = glob.glob(testsglob)
    module_strings = ['tests.'+str[6:len(str)-3] for str in test_file_strings]
    suites = [unittest.defaultTestLoader.loadTestsFromName(name) \
              for name in module_strings]
    testSuite = unittest.TestSuite(suites)
    return testSuite
    
