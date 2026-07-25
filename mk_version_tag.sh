#!/bin/bash

COMMIT_HASH=`git log -n 1 --format="%h"`
COMMIT_DATE=`git log -n 1 --format="%ai"`

BUILD_DATE=`date`
MAJOR=1
MINOR=0
PATCH=0
RELEASE="-r"

echo "#ifndef VERSION_H"
echo "#define VERSION_H"
echo 
echo "#define COMMIT_DATE \"$COMMIT_DATE\""
echo 
echo "#define VERSION \"$MAJOR.$MINOR.$PATCH$RELEASE\""
echo 
echo "#endif // VERSION_H"
echo 
