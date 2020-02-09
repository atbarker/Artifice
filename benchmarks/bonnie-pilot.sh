testdir=$1
user=$3
ram=$2
runs=$4

#sudo bonnie++ -d $testdir -r $ram -u $user -q -x $runs | tail -n+2
#column 9 for write  throughput, 15 for read
sudo bonnie++ -d $testdir -r $ram -u $user -q 2>/dev/null

