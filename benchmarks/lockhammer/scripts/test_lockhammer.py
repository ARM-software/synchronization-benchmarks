#!/usr/bin/env python3

# Copyright (c) 2018, ARM Limited. All rights reserved.
#
# SPDX-License-Identifier:    BSD-3-Clause
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or
# other materials provided with the distribution.
#
# Neither the name of ARM Limited nor the names of its contributors may be used
# to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# The views and conclusions contained in the software and documentation are those
# of the authors and should not be interpreted as representing official policies,
# either expressed or implied, of this project.

import sys
import os
import sh
import errno
import platform
import unittest
import yaml
import random
import multiprocessing
import socket
import datetime
import pprint


# config file should be in the same directory of this script
LH_CFG = "lh_test_cfg.yaml"
# lockhammer.c has these parameters
LH_ARGU_LIST = ['t', 'a', 'c', 'p']


# python unittest framework container class
@unittest.skipUnless(sys.platform.startswith('linux'), "require Linux")
@unittest.skipUnless(platform.processor() in ['aarch64', 'x86_64'], "require aarch64 or x86_64")
class TestLockHammer(unittest.TestCase):
    """Lockhammer integration tests with default parameters """
    pass

# local logging function
def write_output(stdOut, logFile, paramList):
    if isinstance(logFile, str):
        with open(logFile, 'a+') as fd:
            # make sure key results' position are fixed
            fd.write(stdOut.rstrip())
            for param in paramList:
                fd.write(', ' + str(param))
            fd.write(os.linesep)

# python unittest framework function constructor
def construct_func(fullCmd, fullArg, logFile):
    def test(self):
        cmdObj = sh.Command(fullCmd)
        stdOut = str(cmdObj(fullArg))
        write_output(stdOut, logFile, [str(datetime.datetime.now()), socket.getfqdn(), fullCmd] + fullArg)
        regEx = "[0-9]*, [0-9]*\.?[0-9]*, [0-9]*\.?[0-9]*, [0-9]*\.?[0-9]*, [0-9]*\.?[0-9]*"
        self.assertRegex(stdOut, regEx, "This program has not run to completion.")
    return test

# local config loader
def read_config(lhCfgStr):
    cfg = None
    with open(lhCfgStr, 'r') as fd:
        try:
            cfg = yaml.load(fd)
        except yaml.YAMLError as exc:
            print(exc)
    return cfg

def default_value(dictCfg, key, dft):
    var = dft
    if isinstance(dictCfg, dict):
        if key in dictCfg:
            var = dictCfg[key]
    return var

def prepare_logfile(logFile):
    if isinstance(logFile, str):
        try:
            open(logFile, 'w')
            os.remove(logFile)
        except OSError as exc:
            if exc.errno != errno.ENOENT:  # no such file or directory
                print("Cannot create or locate logfile.")
                sys.exit(3)

# -s safemode flag should be inserted before --
def insert_safe_flag(paramList):
    if '--' in paramList:
        paramList.insert(paramList.index('--'), '-s')
    else:
        paramList.append('-s')
    return paramList

# convert cmdName + paramList to a string
def full_func_name(cmdName, paramList, fillZero):
    fullName = cmdName
    if isinstance(paramList, list):
        for argument in paramList:
            if fillZero and isinstance(argument, int):
                fullName += str(argument).zfill(3)
            else:
                fullName += str(argument)
    elif isinstance(paramList, str):
        fullName += paramList
    else:
        fullName += str(random.random())
    return fullName

# convert parameter from {key:value} to string list
def expand_param(ctrl, valueList):
    outParam = []
    if isinstance(valueList, list):
        for value in valueList:
            if ctrl == 't' and value == 0:
                outParam.append(['-t', multiprocessing.cpu_count()])
            else:
                outParam.append(['-' + ctrl, value])
    elif isinstance(valueList, type(None)):
        outParam.append(['-' + ctrl])
    elif isinstance(valueList, int):
        if ctrl == 't' and valueList == 0:
            outParam.append(['-t', multiprocessing.cpu_count()])
        else:
            outParam.append(['-' + ctrl, valueList])
    else:
        outParam.append(['-' + ctrl, valueList])
    return outParam

# formulate parameter combinations
def list_product(origListList, paramListList):
    newListList = []
    for orig in origListList:
        for param in paramListList:
            if isinstance(orig, list) and isinstance(param, list):
                newListList.append(orig + param)
    if newListList:
        return newListList
    else:
        return origListList

# generate all possible parameter list for unittest
def generate_param(testCfg):
    origLL = [[]]

    # lockhammer normal parameters
    for key in testCfg:
        if key in LH_ARGU_LIST:
            paramLL = expand_param(key, testCfg[key])
            origLL = list_product(origLL, paramLL)

    # make sure extra parameters are generated after normal parameter
    for key in testCfg:
        if key == 'extra':
            origLL = [x+y for x,y in zip(origLL, [['--']]*len(origLL))]
            for extKey in testCfg['extra']:
                paramLL = expand_param(extKey, testCfg['extra'][extKey])
                origLL = list_product(origLL, paramLL)

    return origLL

# generate all possible parameter list for sweeptest
def prepare_param(arguList):
    arguLL = []
    if isinstance(arguList, list):
        for elem in arguList:
            paramList = []
            for key in elem:
                paramList.extend(['-'+key, elem[key]])
            arguLL.append(paramList)
    return arguLL

# calculate sweeping values for sweeptest
def calc_sweep_list(maxV, skipV, stepV):
    if maxV == 0 or not isinstance(maxV, int):
        maxV = multiprocessing.cpu_count()
    if skipV <= 0 or not isinstance(skipV, int):
        skipV = 1
    if stepV <= 0 or not isinstance(stepV, int):
        stepV = maxV

    if skipV < maxV:
        # construct a list with range [1,skipV]
        sweepList = list(range(1, skipV+1))
        # append range from skipV(included) to maxV(included) stepped with stepV
        sweepList += list(range(skipV, maxV+1, stepV))
        # make sure half-socket, single-socket, full-socket core count are covered
        sweepList += [int(maxV/4), int(maxV/2), int(maxV/4*3), maxV]
        # remove duplicates and sort result list
        sweepList = list(set(sweepList))
        sweepList.sort()
    else:
        sweepList = list(range(1, maxV+1))

    return sweepList

# some workloads are only valid for certain architectures
def append_arch_cmd(testCfg, cmdList):
    if isinstance(testCfg, dict) and isinstance(cmdList, list):
        if 'cmd_aarch64' in testCfg and platform.processor() == 'aarch64':
            if isinstance(testCfg['cmd_aarch64'], list):
                cmdList.extend(testCfg['cmd_aarch64'])
            elif isinstance(testCfg['cmd_aarch64'], str):
                cmdList.append(testCfg['cmd_aarch64'])
        elif 'cmd_x86_64' in testCfg and platform.processor() == 'x86_64':
            if isinstance(testCfg['cmd_x86_64'], list):
                cmdList.extend(testCfg['cmd_x86_64'])
            elif isinstance(testCfg['cmd_x86_64'], str):
                cmdList.append(testCfg['cmd_x86_64'])

def generate_unittest(className, lhCfg, testCfg):
    globalCfg = default_value(lhCfg, 'globalcfg', {})
    unitCfg = default_value(lhCfg, 'unittest', {})

    execDir = default_value(globalCfg, 'execdir', os.path.join("..", "build"))
    logFile = default_value(globalCfg, 'logfile', None)
    safeMode = default_value(unitCfg, 'safemode', True)

    prepare_logfile(logFile)

    allCmd = []
    if isinstance(testCfg['cmd'], list):
        allCmd = testCfg['cmd']
    elif isinstance(testCfg['cmd'], str):
        allCmd.append(testCfg['cmd'])
    else:
        print("Command name in unittest should be either a string or a list of strings.")
        sys.exit(2)

    append_arch_cmd(testCfg, allCmd)

    for oneCmd in allCmd:
        for oneParam in generate_param(testCfg):
            if safeMode:
                oneParam = insert_safe_flag(oneParam)
            testExec = os.path.join(os.path.dirname(os.path.abspath(__file__)), execDir, oneCmd)
            testFunc = construct_func(testExec, oneParam, logFile)
            fullCmdName = full_func_name(oneCmd, oneParam, False)
            setattr(className, "test_" + fullCmdName, testFunc)

def build_unit_test(lhCfg):
    if not isinstance(lhCfg, dict):
        print("Error, cannot parse lockhammer configuration yaml file.")
        sys.exit(2)

    unitCfg = default_value(lhCfg, 'unittest', {'enabled': False})

    if unitCfg['enabled']:
        if isinstance(unitCfg['testcase'], list):
            for oneCase in unitCfg['testcase']:
                generate_unittest(TestLockHammer, lhCfg, oneCase)
        elif isinstance(unitCfg['testcase'], dict):
            generate_unittest(TestLockHammer, lhCfg, unitCfg['testcase'])
        else:
            print("Cannot extract any testcase from unittest dict.")
            sys.exit(2)

def generate_sweeptest(className, lhCfg):
    globalCfg = default_value(lhCfg, 'globalcfg', {})
    sweepCfg = default_value(lhCfg, 'sweeptest', {'enabled': False})

    execDir = default_value(globalCfg, 'execdir', os.path.join("..", "build"))
    logFile = default_value(globalCfg, 'logfile', None)
    safeMode = default_value(sweepCfg, 'safemode', False)
    repeatCnt = default_value(sweepCfg, 'repeat', 7)
    sweepArgu = default_value(sweepCfg, 'sweepargu', 't')
    arguMax = default_value(sweepCfg, 'argumax', 0)
    skipSince = default_value(sweepCfg, 'skipsince', 48)
    skipStep = default_value(sweepCfg, 'skipstep', 8)
    lhCommand = default_value(sweepCfg, 'cmd', [])
    lhArgument = default_value(sweepCfg, 'argulist', [{}])

    prepare_logfile(logFile)
    sweepList = calc_sweep_list(arguMax, skipSince, skipStep)
    append_arch_cmd(sweepCfg, lhCommand)

    for oneCmd in lhCommand:
        for oneParam in prepare_param(lhArgument):
            for sweepParam in sweepList:
                newOneParam = ['-'+sweepArgu, sweepParam] + oneParam
                if safeMode:
                    newOneParam = insert_safe_flag(newOneParam)
                testExec = os.path.join(os.path.dirname(os.path.abspath(__file__)), execDir, oneCmd)
                for rep in range(repeatCnt):
                    testFunc = construct_func(testExec, newOneParam, logFile)
                    fullCmdName = full_func_name(oneCmd, newOneParam + ['-'+str(rep)], True)
                    setattr(className, "test_" + fullCmdName, testFunc)

def build_sweep_test(lhCfg):
    if not isinstance(lhCfg, dict):
        print("Error, cannot parse lockhammer configuration yaml file.")
        sys.exit(2)

    sweepCfg = default_value(lhCfg, 'sweeptest', {'enabled': False})

    if sweepCfg['enabled']:
        if isinstance(sweepCfg, dict):
            generate_sweeptest(TestLockHammer, lhCfg)
        else:
            print("Error, sweeptest should be a dict.")
            sys.exit(2)


# main function
if __name__ == "__main__":
    lhConfig = read_config(os.path.join(os.path.dirname(os.path.abspath(__file__)), LH_CFG))
    pprint.pprint(lhConfig)
    build_unit_test(lhConfig)
    build_sweep_test(lhConfig)
    unittest.main(verbosity=2)
