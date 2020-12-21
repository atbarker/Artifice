#!/bin/bash

testdir=$1
ram=$2
user=$3
runs=$4

#sudo bonnie++ -d $testdir -r $ram -u $user -q -x $runs | tail -n+2
#column 9 for write  throughput, 15 for read
#sudo bonnie++ -d $testdir -r $ram -u $user -q 2>/dev/null
sudo bonnie++ -d $testdir -r $ram -u $user -x $runs -q 2>/dev/null

