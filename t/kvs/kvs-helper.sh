#!/bin/sh
#

# KVS Watch helper functions

# Various loops to wait for conditions before moving on.  There is
# potential for racing between backgrounding processes and foreground
# activities.
#
# Loop on KVS_WAIT_ITERS is just to make sure we don't spin forever on
# error.

KVS_WAIT_ITERS=50

loophandlereturn() {
    index=$1
    if [ "$index" -eq "${KVS_WAIT_ITERS}" ]
    then
        return 1
    fi
    return 0
}

# arg1 - key to retrieve
# arg2 - expected value
test_kvs_key() {
	flux kvs get --json "$1" >output
	echo "$2" >expected
	test_cmp expected output
}

# arg1 - key to retrieve
# arg2 - value to wait for
wait_watch_put() {
        i=0
        while [ "$(flux kvs get --json $1 2> /dev/null)" != "$2" ] && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

# arg1 - key to retrieve
wait_watch_empty() {
        i=0
        while flux kvs get --json $1 2> /dev/null && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}

# arg1 - file to watch
# arg2 - value to wait for
wait_watch_file() {
        i=0
        while [ "$(tail -n 1 $1 2> /dev/null)" != "$2" ] && [ $i -lt ${KVS_WAIT_ITERS} ]
        do
                sleep 0.1
                i=$((i + 1))
        done
        return $(loophandlereturn $i)
}
