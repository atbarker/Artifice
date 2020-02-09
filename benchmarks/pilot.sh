testdir=$1
user=$3
ram=$2

#9 for write throughput, 15 for read throughput
bench run_program --pi "throughput,KB/s,9,1,1" -- ./bonnie-pilot.sh $testdir $ram $user
