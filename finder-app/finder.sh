#!/bin/bash

print_usage() {
	echo "usage: $0 DIR SEARCH_STR"
}


if [[ "$#" -ne 2 ]]; then
	print_usage
	exit 1
fi

filesdir=$1
searchstr=$2

if [[ ! -d "$filesdir" ]]; then
	echo "ERROR: '$filesdir' is not a directory" >&2
	exit 1
fi

finder() {
	res=( $(grep --recursive --count --no-filename "$2" "$1") )
	file_count=${#res[@]}
	result_count=$(awk '{ s+=$1 } END { print s}' <( for line in "${res[@]}"; do echo "$line"; done ))
	echo "The number of files are $file_count and the number of matching lines are $result_count"
}

finder $1 $2
