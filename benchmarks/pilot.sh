testdir=$1
user=$3
ram=$2

bench run_program --pi "throughput,KB/s,0,1,1" -- ./bonnie-pilot.sh $testdir $ram $user
