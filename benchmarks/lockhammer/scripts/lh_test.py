#!/usr/bin/env python3
import sys
import os
import sh
import platform
import unittest
import yaml
import random
import multiprocessing
import pprint


LH_CFG = "lh_cfg.yaml"
ARGU_LIST = ['a', 'c', 'p']


@unittest.skipUnless(sys.platform.startswith('linux'), "require Linux")
@unittest.skipUnless(platform.processor() in ['aarch64', 'x86_64'], "require aarch64 or x86_64")
class TestLockHammer(unittest.TestCase):
    """Lockhammer integration tests with default parameters """
    pass

def read_config(lhCfgStr):
    cfg = None
    with open(lhCfgStr, 'r') as fd:
        try:
            cfg = yaml.load(fd)
        except yaml.YAMLError as exc:
            print(exc)
    return cfg

def default_value(dictCfg, key, dft):
    if key in dictCfg:
        var = dictCfg[key]
    else:
        var = dft
    return var

def construct_func(fullCmd, fullArg):
    def test(self):
        cmdObj = sh.Command(fullCmd)
        stdOut = str(cmdObj(fullArg, _err_to_out=True))
        regEx = "[0-9]*, [0-9]*\.?[0-9]*, [0-9]*\.?[0-9]*, [0-9]*\.?[0-9]*, [0-9]*\.?[0-9]*"
        self.assertRegex(stdOut, regEx, "This program has not run to completion.")
    return test

def insert_safe_flag(paramList):
    if '--' in paramList:
        paramList.insert(paramList.index('--'), '-s')
    else:
        paramList.append('-s')
    return paramList

def unittest_base_param(testCfg):
    outParam = []
    if 't' in testCfg:
        if isinstance(testCfg['t'], list):
            for thread in testCfg['t']:
                if thread == 0:
                    outParam.append(['-t', multiprocessing.cpu_count()])
                else:
                    outParam.append(['-t', thread])
        elif isinstance(testCfg['t'], int):
            if testCfg['t'] == 0:
                outParam.append(['-t', multiprocessing.cpu_count()])
            else:
                outParam.append(['-t', testCfg['t']])
        else:
            outParam.append(['-t', testCfg['t']])
    else:
        outParam = [['-t', multiprocessing.cpu_count()]]

    return outParam

def expand_param(ctrl, valueList):
    outParam = []
    if isinstance(valueList, list):
        for value in valueList:
            outParam.append(['-' + ctrl, value])
    elif isinstance(valueList, type(None)):
        outParam.append(['-' + ctrl])
    else:
        outParam.append(['-' + ctrl, valueList])
    return outParam

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

def prepare_param(arguList):
    arguLL = []
    if isinstance(arguList, list):
        for elem in arguList:
            paramList = []
            for key in elem:
                paramList.extend(['-'+key, elem[key]])
            arguLL.append(paramList)
    return arguLL

def generate_param(testCfg):
    origLL = unittest_base_param(testCfg)
    prodCount = len(origLL)
    for key in testCfg:
        if key in ARGU_LIST:
            paramLL = expand_param(key, testCfg[key])
            prodCount = prodCount * len(paramLL)
            origLL.extend(list_product(origLL, paramLL))
        elif key == 'extra':
            origLL = [x+y for x,y in zip(origLL, [['--']]*len(origLL))]
            for extKey in testCfg['extra']:
                paramLL = expand_param(extKey, testCfg['extra'][extKey])
                prodCount = prodCount * len(paramLL)
                origLL.extend(list_product(origLL, paramLL))

    return origLL[len(origLL) - prodCount:]

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

def calc_sweep_list(maxV, offsetV, skipV, stepV):
    if maxV == 0:
        maxV = multiprocessing.cpu_count()

    if skipV < maxV:
        sweepList = [x+1 for x in list(range(skipV))]
        sweepList += list(range(skipV+stepV, maxV+stepV, stepV))
        # make sure half-socket, single-socket, full-socket core count are covered
        sweepList += [int(maxV/4), int(maxV/2), int(maxV/4*3), maxV]
        # remove duplicates
        sweepList = list(set(sweepList))
    else:
        sweepList = [x+1 for x in list(range(maxV))]

    return [v+offsetV for v in sweepList]

def generate_unittest(className, unitTests, testCfg, pathStr, safeMode):
    allCmd = []
    if isinstance(testCfg['cmd'], list):
        allCmd = testCfg['cmd']
    elif isinstance(testCfg['cmd'], str):
        allCmd.append(testCfg['cmd'])
    else:
        print("Command name in unittest should be either a string or a list of strings.")
        sys.exit(2)

    if 'cmd_aarch64' in testCfg and platform.processor() == 'aarch64':
        if not testCfg['cmd_aarch64']:
            allCmd.extend(testCfg['cmd_aarch64'])
    elif 'cmd_x86_64' in testCfg and platform.processor() == 'x86_64':
        if not testCfg['cmd_x86_64']:
            allCmd.extend(testCfg['cmd_x86_64'])

    for oneCmd in allCmd:
        for oneParam in generate_param(testCfg):
            if safeMode:
                oneParam = insert_safe_flag(oneParam)
            testExec = os.path.join(pathStr, oneCmd)
            testFunc = construct_func(testExec, oneParam)
            fullCmdName = full_func_name(oneCmd, oneParam, False)
            setattr(className, "test_" + fullCmdName, testFunc)

def generate_sweeptest(className, sweepTests, sweepCfg):
    safeMode = default_value(sweepCfg, 'safemode', False)
    execDir = default_value(sweepCfg, 'execdir', os.path.join("..", "build"))
    repeatCnt = default_value(sweepCfg, 'repeat', 7)
    sweepArgu = default_value(sweepCfg, 'sweepargu', 't')
    arguMax = default_value(sweepCfg, 'argumax', 0)
    arguOffset = default_value(sweepCfg, 'arguoffset', 0)
    skipSince = default_value(sweepCfg, 'skipsince', 48)
    skipStep = default_value(sweepCfg, 'skipstep', 8)
    workCommand = default_value(sweepCfg, 'workload', ['lh_empty'])
    workArgument = default_value(sweepCfg, 'argulist', [{}])

    sweepList = calc_sweep_list(arguMax, arguOffset, skipSince, skipStep)

    for oneCmd in workCommand:
        for oneParam in prepare_param(workArgument):
            for sweepParam in sweepList:
                newOneParam = oneParam + ['-'+sweepArgu, sweepParam]
                if safeMode:
                    newOneParam = insert_safe_flag(newOneParam)
                testExec = os.path.join(execDir, oneCmd)
                for rep in range(repeatCnt):
                    testFunc = construct_func(testExec, newOneParam)
                    fullCmdName = full_func_name(oneCmd, newOneParam + ['-'+str(rep)], True)
                    setattr(className, "test_" + fullCmdName, testFunc)

def build_unit_test(lhCfg):
    uTests = []
    if not isinstance(lhCfg, dict):
        print("Error, cannot parse lockhammer configuration yaml file.")
        sys.exit(2)
    if 'unittest' in lhCfg:
        unitCfg = lhCfg['unittest']
        if unitCfg['enabled']:
            safeMode = default_value(unitCfg, 'safemode', True)
            execDir = default_value(unitCfg, 'execdir', os.path.join("..", "build"))

            if isinstance(unitCfg['testcase'], list):
                for oneCase in unitCfg['testcase']:
                    uTests = generate_unittest(TestLockHammer, uTests, oneCase, execDir, safeMode)
            elif isinstance(unitCfg['testcase'], dict):
                uTests = generate_unittest(TestLockHammer, uTests, unitCfg['testcase'], execDir, safeMode)
            else:
                print("Cannot extract any testcase from unittest dict.")
                sys.exit(2)

def build_sweep_test(lhCfg):
    sTests = []
    if not isinstance(lhCfg, dict):
        print("Error, cannot parse lockhammer configuration yaml file.")
        sys.exit(2)
    if 'sweeptest' in lhCfg:
        sweepCfg = lhCfg['sweeptest']
        if sweepCfg['enabled']:
            sTests = generate_sweeptest(TestLockHammer, sTests, lhCfg['sweeptest'])


if __name__ == "__main__":
    lhConfig = read_config(LH_CFG)
    pprint.pprint(lhConfig)
    build_unit_test(lhConfig)
    build_sweep_test(lhConfig)
    unittest.main(verbosity=2)
