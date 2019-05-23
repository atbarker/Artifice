import subprocess
import numpy as np
import time

dev = "/dev/dm-0"
op = "w"
typ = "seq"
cmd = "./bench " + op + " " + typ + " " + dev

throughput = []
for i in range(2):
    output = subprocess.check_output(cmd, shell=True)
    split_output = output.split(":")
    output = split_output[1]
    mbps = output.split("M")
    output = mbps[0].strip()
    output = float(output)
    throughput.append(output)

print throughput
print "Average:      ", np.average(throughput), "MB/s"
print "Std Deviation:", np.std(throughput)
