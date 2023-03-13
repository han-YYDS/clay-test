#!/usr/bin/env python
# -*- coding: utf-8 -*-
import os
import subprocess

paramSize = " 1 10 "
repairIdxs = " 0 1"
param1 = " 4 2 4 "
param2 = " 6 4 8 "
param3 = " 9 6 27 "
param4 = " 12 8 64 "
params = [param1, param2, param3, param4]
file = open("output.txt", "w")

for param in params:
    command = './build/Tester ' + param + paramSize + repairIdxs
    file.write("["+command + "] : ")
    print command
    print "decode time"
    for i in range(10):
        p = subprocess.Popen(['/bin/bash', '-c', command], stdout=subprocess.PIPE)
        output = p.communicate()
        status = p.returncode
        file.write(str(status) + " ")
        print("["+ str(i)+"]:"+ str(status))
    file.write("\n")
file.close()