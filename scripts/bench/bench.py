#!/usr/bin/python3

#Written by Austen Barker and Yash Gupta

import subprocess
import numpy as np
import time
import sys
import argparse

dev = "/dev/mapper/artifice"
op = "w"
typ = "seq"

parser = argparse.ArgumentParser(description='Run benchmark on a block device')
parser.add_argument('--device', '-d', nargs='?', default='/dev/mapper/artifice', dest='dev', help='Device to benchmark.')
parser.add_argument('--op', '-o', nargs='?', default='w', choices=['w','r', 'rw'], dest='op', help='Operation to perform, read, write, or mixed.')
parser.add_argument('--type', '-t', nargs='?', default='seq', choices=['seq', 'rand'], dest='typ', help='Type of operation, sequential or random.')
parser.add_argument('--iterations', '-i', type=int, nargs='?', default=10, dest='iter', help='Number of times to run the test.')

def main(args):
    cmd = "./bench " + args.op + " " + args.typ + " " + args.dev

    throughput = []
    for i in range(args.iter):
        output = subprocess.check_output(cmd, shell=True)
        split_output = output.split(":")
        output = split_output[1]
        mbps = output.split("M")
        output = mbps[0].strip()
        output = float(output)
        throughput.append(output)

    print(throughput)
    print("Average:      {} MB/s".format(np.average(throughput)))
    print("Std Deviation: {}".format(np.std(throughput)))

if __name__ == "__main__":
    args = parser.parse_args()
    main(args)
