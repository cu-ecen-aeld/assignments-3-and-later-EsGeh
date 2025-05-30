#!/bin/bash

print_usage() {
	echo "usage: $0 FILE STR"
}


if [[ "$#" -ne 2 ]]; then
	print_usage
	exit 1
fi

writefile=$1
writestr=$2

if [[ -f "$filesdir" ]]; then
	echo "ERROR: '$filesdir' is not a file" >&2
	exit 1
fi

mkdir --parents $(dirname $writefile)

echo "$writestr" > $writefile
if [[ $? -ne 0 ]]; then
	# echo "ERROR: failed to write to '$writefile'" >&2
	exit 1
fi
