#!/bin/sh
# Script to write a string in a file

if [ $# -lt 2 ]
then
    echo "Error: a parameter is missing - either path or string were not specified"
    exit 1
fi

writefile=$1
writestr=$2

if [ ! -e "$writefile" ]
then
    writedir=${writefile%'/'*}
    mkdir -p "$writedir"
fi

echo $writestr > $writefile

if [ $? -ne 0 ]
then
    echo "An error ocurred"
    exit 1
fi