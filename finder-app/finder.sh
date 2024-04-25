#!/bin/sh
# Script to find a string in files within a path

if [ $# -lt 2 ]
then
    echo "Error: a parameter is missing - either path or string were not specified"
    exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "$filesdir" ]
then
    echo "Error: $filesdir ist not a valid directory"
    exit 1
fi

number_of_files=$(ls "$filesdir" | wc -l)
matching_lines=$(grep -r $searchstr $filesdir/* | wc -l)

echo "The number of files are "$number_of_files" and the number of matching lines are "$matching_lines""
