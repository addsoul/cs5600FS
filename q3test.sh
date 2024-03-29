#!/bin/bash
#
# file:        q3test.sh
# description: test script for FUSE version of on-disk FS
#
# CS 7600, Intensive Computer Systems, Northeastern CCIS
# Peter Desnoyers, April 2010
# $Id: q4test.sh 163 2010-04-20 19:51:46Z pjd $
#

MNT=/tmp/mnt-$$
failedtests=
TMP=/tmp/tmp-$$

DISK=/tmp/disk.$$.img
./mkfs-cs5600fs --create 1m $DISK

umask 022

echo Question 3 - FUSE mode

mkdir -p $MNT
trap "fusermount -u $MNT; rm -rf $MNT; rm -f $TMP $DISK" 0

# need to get group ID to compare results properly
#
set $(id | tr '()' ' ')
GROUP=$4

${CMD:-./homework} $DISK $MNT

testperm(){
    line=$(ls -l $MNT/$1)
    val=$2
    set x$line
    if [ $1 != x$val ] ; then 
	echo incorrect permissions: $line
	echo $1 should be x$val
	fail=1
    fi
}

checktest(){
    if [ x"$fail" = x1 ] ; then
	echo "  FAILED"
	failedtests="$failedtests $1"
    else
	echo "  SUCCESS"
    fi
    fail=
}

# test 1 - mkdir, ls
echo Test 1 - mkdir, ls
DATE=$(date +'%Y-%m-%d %H:%M')
mkdir $MNT/file1
ls -ld --time-style=long-iso $MNT/file1 > $TMP
diff -u $TMP - <<EOF
drwxr-xr-x 1 $USER $GROUP 0 $DATE $MNT/file1
EOF
[ $? = 0 ] || fail=1
checktest 1

# test 2 - chmod
echo Test 2 - chmod
echo > $MNT/file2
chmod 777 $MNT/file2
testperm file2 -rwxrwxrwx
chmod 321 $MNT/file2 || fail=1
testperm file2 --wx-w---x
chmod 444 $MNT/file2 || fail=1
testperm file2 -r--r--r--
chmod 740 $MNT/file2 || fail=1
testperm file2 -rwxr-----
checktest 2

echo Test 3 - directory nesting
DATE=$(date +'%Y-%m-%d %H:%M')
mkdir $MNT/dir1
mkdir $MNT/dir1/dir1.1
mkdir $MNT/dir1/dir1.2
mkdir $MNT/dir1/dir1.1/dir1.1.1
mkdir $MNT/dir2
mkdir $MNT/dir2/dir2.1
ls --time-style=long-iso -lR $MNT/dir* > $TMP
diff -u $TMP - <<EOF
$MNT/dir1:
total 0
drwxr-xr-x 1 $USER $GROUP 0 $DATE dir1.1
drwxr-xr-x 1 $USER $GROUP 0 $DATE dir1.2

$MNT/dir1/dir1.1:
total 0
drwxr-xr-x 1 $USER $GROUP 0 $DATE dir1.1.1

$MNT/dir1/dir1.1/dir1.1.1:
total 0

$MNT/dir1/dir1.2:
total 0

$MNT/dir2:
total 0
drwxr-xr-x 1 $USER $GROUP 0 $DATE dir2.1

$MNT/dir2/dir2.1:
total 0
EOF
[ $? = 0 ] || fail=1
checktest 3

cksumtest(){
    val=$2
    set $(cat $MNT/$1 | cksum)
    if [ "$val" != "$1 $2" ] ; then
	echo cksum $MNT/$1: $1 $2
	echo should be $val
	fail=1
    fi
}

echo Test 4 - large and small reads+writes
ls /usr/bin > $TMP
dd if=$TMP of=$MNT/dir1/file1.out bs=17 > /dev/null 2> /dev/null || fail=1
cmp $MNT/dir1/file1.out $TMP || fail=1
dd if=$TMP of=$MNT/dir1/file2.out bs=1571 > /dev/null 2> /dev/null || fail=1
cmp $MNT/dir1/file2.out $TMP || fail=1
yes this is a test | fmt | head -40 > $MNT/test.out
cksumtest test.out "1971346563 2800"
checktest 4

# truncate
echo Test 5 - truncate
echo foo > $MNT/test.out
cksumtest test.out "3915528286 4" 
checktest 5

# read / modify / write
echo Test 6 - read/modify/write
yes test 6 data file | fmt | head -40 > $MNT/test-6.dat
#ls -l $MNT/test.out
#cksumtest test.out "3915528286 4"
cksumtest test-6.dat "3581607758 2890"
yes a | dd bs=64 count=1 conv=notrunc of=$MNT/test-6.dat > /dev/null 2> /dev/null
cksumtest test-6.dat "2437960129 2890"
yes a | dd bs=100 seek=10 count=1 conv=notrunc of=$MNT/test-6.dat > /dev/null 2> /dev/null
cksumtest test-6.dat "4078974259 2890"
checktest 6

# mkdir, rmdir
echo Test 7 - mkdir, rmdir
mkdir $MNT/dir-17 || fail=1
cp $MNT/dir1/file1.out $MNT/dir-17 || fail=1
cmp $MNT/dir1/file1.out $MNT/dir-17/file1.out || fail=1
if rmdir $MNT/dir-17; then fail=1; fi
if mkdir $MNT/dir1/file1.out ; then fail=1; fi
if mkdir $MNT/dir-17; then fail=1; fi
rm $MNT/dir-17/file1.out || fail=1
rmdir $MNT/dir-17 || fail=1
checktest 7

# rename
echo Test 8 - rename
sleep 1
yes this is a test | fmt | head -40 > $MNT/test.out
mv $MNT/test.out $MNT/file7.txt || fail=1
cksumtest file7.txt "1971346563 2800"
checktest 8

# utime
echo Test 9 - utime
set x$(ls --full-time $MNT/dir1/file2.out)
oldtime=$7
sleep 1
touch $MNT/dir1/file2.out
set x$(ls --full-time $MNT/dir1/file2.out)
newtime=$7
if [ $oldtime = $newtime ]; then
    echo utime failed: $oldtime $newtime
    fail=1
fi
checktest 9

if [ "$failedtests" = "" ] ; then
    echo '*** ALL TESTS PASSED ***'
else
    echo "FAILING TESTS: $failedtests"
fi

