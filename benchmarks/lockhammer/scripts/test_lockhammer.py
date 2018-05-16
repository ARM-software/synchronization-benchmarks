#!/usr/bin/env python3
import sys
import os
import sh
import platform
import unittest

ALL_TESTS = []


def find_lh_tests():
    for fname in os.listdir("../build"):
        if fname.startswith("lh_"):
            ALL_TESTS.append(fname)

def test_generator_default(lhBin):
    def test(self):
        testBin = os.path.join("..", "build", lhBin)
        cmd = sh.Command(testBin)
        out = str(cmd(_err_to_out=True))
        self.assertRegex(out, "ns elapsed", "Cannot find 'ns elapsed' keywords.")
    return test

def test_sequencer_default(className):
    find_lh_tests()
    for lhBin in ALL_TESTS:
        test_lh = test_generator_default(lhBin)
        setattr(className, "test_" + lhBin, test_lh)


@unittest.skipUnless(sys.platform.startswith('linux'), "require Linux")
@unittest.skipUnless(platform.processor() in ['aarch64', 'x86_64'], "require aarch64 or x86_64")
class TestLockHammer(unittest.TestCase):
    """Lockhammer integration tests with default parameters """
    pass


if __name__ == "__main__":
    test_sequencer_default(TestLockHammer)
    unittest.main(verbosity=2)
