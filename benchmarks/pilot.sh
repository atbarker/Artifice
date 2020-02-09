testdir=$1
user=$3
ram=$2
type=$4

#9 for write throughput, 15 for read throughput
if [[ "$type" == "w" ]]; then
    bench run_program --pi "throughput,KB/s,9,1,1" -- sudo bonnie++ -d $testdir -r $ram -u $user -q 2>/dev/null
elif [[ "$type" == "r" ]]; then
    bench run_program --pi "throughput,KB/s,15,1,1" -- sudo bonnie++ -d $testdir -r $ram -u $user -q 2>/dev/null
else
    echo "invalid metric, choose either r (read) or w (write)"
fi
