#!/bin/bash

TMP_ONE=`mktemp`
TMP_TWO=`mktemp`
COMPARE=`mktemp`

FILE_ONE="LOCAL.RESULTS"
DESC_ONE="local"
FILE_TWO="LGUEST.RESULTS"
DESC_TWO="lguest"

while [ -n "${1}" ] ; do
	case "$1" in
		-f1)	FILE_ONE=${2}
			shift
			;;
		-d1)	DESC_ONE=${2}
			shift
			;;
		-f2)	FILE_TWO=${2}
			shift
			;;
		-d2)	DESC_TWO=${2}
			shift
			;;
	esac

	shift
done

sed 's/nsec//' $FILE_ONE > $TMP_ONE
cut -d: -f2- $FILE_TWO| awk '{print $1}' > $TMP_TWO
paste $TMP_ONE $TMP_TWO > $COMPARE

echo Comparing $DESC_ONE with $DESC_TWO
while read LINE ; do
	MSG=`echo $LINE | cut -d: -f1`
	TIMES=`echo $LINE | cut -d: -f2`
	ONE=`echo $TIMES | awk '{print $1}'`
	TWO=`echo $TIMES | awk '{print $2}'`
	#RATIO=`echo "scale=2;$ONE / $TWO"| bc`
	RATIO=$(( $ONE / $TWO ))
	printf "%s:\t%01.3f\n" "$MSG" $RATIO
done < $COMPARE

rm $TMP_ONE $TMP_TWO $COMPARE
