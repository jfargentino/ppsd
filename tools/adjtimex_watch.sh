#!/bin/bash

NOW=`date +%s`
if [ -z $1 ]; then
    OUTFILE="adjtimex."$NOW
else
    OUTFILE="$1".$NOW
fi

adjtimex --print | grep -v 'raw time' > "$OUTFILE"
FIRST="$OUTFILE"
PREV=.$NOW
cat $FIRST
cp $FIRST $PREV
while [ 1 ]; do
    sleep 1
    NOW=`date +%s`
    adjtimex --print | grep -v 'raw time' > .$NOW
    if [ ! -f $PREV ]; then
	echo "File $PREV does not exist anymore... exit"
	exit
    fi
    DIFF=`diff $PREV .$NOW`
    if [ ! -z "$DIFF" ]; then
        echo "### $NOW : Change detected ###" >> $FIRST
        echo "$DIFF" | grep "^>" >> $FIRST
        echo "$DIFF" | grep "^>"
        rm $PREV
        PREV=.$NOW
    else
        rm .$NOW
    fi
done    
rm $PREV
