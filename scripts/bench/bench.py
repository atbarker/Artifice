import subprocess
import numpy as np
import time

dev = "/dev/dm-0"
op = "r"
typ = "rand"
cmd = "./bench " + op + " " + typ + " " + dev

throughput = []
for i in range(10):
    output = subprocess.check_output(cmd, shell=True)
    output = output.split(":")
    output = output[1]
    output = output.split("M")
    output = output[0].strip()
    output = float(output)
    throughput.append(output)

print throughput
print "Average:      ", np.average(throughput), "MB/s"
print "Std Deviation:", np.std(throughput)