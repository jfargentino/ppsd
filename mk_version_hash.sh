#!/bin/bash

BRANCH=`git branch --show-current`
#BRANCH=`git log -n 1 --format="%D" | awk -F'->' '{print $2}'`
COMMIT_HASH=`git log -n 1 --format="%h"`
COMMIT_DATE=`git log -n 1 --format="%ai"`
BUILD_DATE=`date`

echo "#ifndef VERSION_H"
echo "#define VERSION_H"

echo "#define BRANCH \"$BRANCH\""
echo "#define COMMIT_HASH \"$COMMIT_HASH\""
echo "#define COMMIT_DATE \"$COMMIT_DATE\""
echo "#define BUILD_DATE \"$BUILD_DATE\""
echo "#define VERSION BRANCH\".\"COMMIT_HASH"

echo "#endif // VERSION_H"
echo 
