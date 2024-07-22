#!/bin/sh

run() {
	kind=$1
	echo Testing ${kind}
	/usr/bin/time ./test --cons-type=${kind} --prod-count=2 --buf-size=4
}

run mc
run sc
run peek
run peek-clear
run mc-mt
