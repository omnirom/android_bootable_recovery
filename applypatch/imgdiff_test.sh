#!/bin/bash
#
# A script for testing imgdiff/applypatch.  It takes two full OTA
# packages as arguments.  It generates (on the host) patches for all
# the zip/jar/apk files they have in common, as well as boot and
# recovery images.  It then applies the patches on the device (or
# emulator) and checks that the resulting file is correct.

EMULATOR_PORT=5580

# set to 0 to use a device instead
USE_EMULATOR=0

# where on the device to do all the patching.
WORK_DIR=/data/local/tmp

START_OTA_PACKAGE=$1
END_OTA_PACKAGE=$2

# ------------------------

tmpdir=$(mktemp -d)

if [ "$USE_EMULATOR" == 1 ]; then
  emulator -wipe-data -noaudio -no-window -port $EMULATOR_PORT &
  pid_emulator=$!
  ADB="adb -s emulator-$EMULATOR_PORT "
else
  ADB="adb -d "
fi

echo "waiting to connect to device"
$ADB wait-for-device

# run a command on the device; exit with the exit status of the device
# command.
run_command() {
  $ADB shell "$@" \; echo \$? | awk '{if (b) {print a}; a=$0; b=1} END {exit a}'
}

testname() {
  echo
  echo "$1"...
  testname="$1"
}

fail() {
  echo
  echo FAIL: $testname
  echo
  [ "$open_pid" == "" ] || kill $open_pid
  [ "$pid_emulator" == "" ] || kill $pid_emulator
  exit 1
}

sha1() {
  sha1sum $1 | awk '{print $1}'
}

size() {
  stat -c %s $1 | tr -d '\n'
}

cleanup() {
  # not necessary if we're about to kill the emulator, but nice for
  # running on real devices or already-running emulators.
  testname "removing test files"
  run_command rm $WORK_DIR/applypatch
  run_command rm $WORK_DIR/source
  run_command rm $WORK_DIR/target
  run_command rm $WORK_DIR/patch

  [ "$pid_emulator" == "" ] || kill $pid_emulator

  rm -rf $tmpdir
}

$ADB push $ANDROID_PRODUCT_OUT/system/bin/applypatch $WORK_DIR/applypatch

patch_and_apply() {
  local fn=$1
  shift

  unzip -p $START_OTA_PACKAGE $fn > $tmpdir/source
  unzip -p $END_OTA_PACKAGE $fn > $tmpdir/target
  imgdiff "$@" $tmpdir/source $tmpdir/target $tmpdir/patch
  bsdiff $tmpdir/source $tmpdir/target $tmpdir/patch.bs
  echo "patch for $fn is $(size $tmpdir/patch) [of $(size $tmpdir/target)] ($(size $tmpdir/patch.bs) with bsdiff)"
  echo "$fn $(size $tmpdir/patch) of $(size $tmpdir/target) bsdiff $(size $tmpdir/patch.bs)" >> /tmp/stats.txt
  $ADB push $tmpdir/source $WORK_DIR/source || fail "source push failed"
  run_command rm /data/local/tmp/target
  $ADB push $tmpdir/patch $WORK_DIR/patch || fail "patch push failed"
  run_command /data/local/tmp/applypatch /data/local/tmp/source \
    /data/local/tmp/target $(sha1 $tmpdir/target) $(size $tmpdir/target) \
    $(sha1 $tmpdir/source):/data/local/tmp/patch \
    || fail "applypatch of $fn failed"
  $ADB pull /data/local/tmp/target $tmpdir/result
  diff -q $tmpdir/target $tmpdir/result || fail "patch output not correct!"
}

# --------------- basic execution ----------------------

for i in $((zipinfo -1 $START_OTA_PACKAGE; zipinfo -1 $END_OTA_PACKAGE) | \
           sort | uniq -d | egrep -e '[.](apk|jar|zip)$'); do
  patch_and_apply $i -z
done
patch_and_apply boot.img
patch_and_apply system/recovery.img


# --------------- cleanup ----------------------

cleanup

echo
echo PASS
echo

