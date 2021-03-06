#!/bin/bash
killall hotstuff-app
cd /Users/soubhikdeb/Downloads/PhD\ Research\ Extn/FairStuff/libhotstuff
./examples/hotstuff-app --conf ./hotstuff-sec0.conf > log0 2>&1 &
leader_pid="$!"
rep=({1..3})
if [[ $# -gt 0 ]]; then
    rep=($@)
fi
for i in "${rep[@]}"; do
    echo "starting replica $i"
    #valgrind --leak-check=full ./examples/hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
    #gdb -ex r -ex bt -ex q --args ./examples/hotstuff-app --conf hotstuff-sec${i}.conf > log${i} 2>&1 &
    cd /Users/soubhikdeb/Downloads/PhD\ Research\ Extn/FairStuff/libhotstuff
    ./examples/hotstuff-app --conf ./hotstuff-sec${i}.conf > log${i} 2>&1 &
done
echo "All replicas started. Let's issue some commands to be replicated (in 5 sec)..."
sleep 5
echo "Start issuing commands and the leader will be killed in 5 seconds"
cd /Users/soubhikdeb/Downloads/PhD\ Research\ Extn/FairStuff/libhotstuff
./examples/hotstuff-client --idx 0 --iter 50 --max-async 50 &
cli_pid=$!
sleep 30
kill "$leader_pid"
echo "Leader is dead. Let's try to restart our clients (because the simple clients don't timeout/retry some lost requests)."
kill "$cli_pid"
cd /Users/soubhikdeb/Downloads/PhD\ Research\ Extn/FairStuff/libhotstuff
./examples/hotstuff-client --idx 0 --iter 50 --max-async 50
