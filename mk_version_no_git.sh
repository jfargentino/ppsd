#!/bin/bash

BRANCH="dev"
COMMIT_HASH="DEADBEEF"
COMMIT_DATE=`date`

echo "#ifndef VERSION_H"
echo "#define VERSION_H"

echo "#define BRANCH \"$BRANCH\""
echo "#define COMMIT_HASH \"$COMMIT_HASH\""
echo "#define COMMIT_DATE \"$COMMIT_DATE\""
echo "#define VERSION BRANCH\".\"COMMIT_HASH"

echo "#endif // VERSION_H"
echo 
