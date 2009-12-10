#!/bin/bash
#
# A test suite for applypatch.  Run in a client where you have done
# envsetup, choosecombo, etc.
#
# DO NOT RUN THIS ON A DEVICE YOU CARE ABOUT.  It will mess up your
# system partition.
#
#
# TODO: find some way to get this run regularly along with the rest of
# the tests.

EMULATOR_PORT=5580
DATA_DIR=$ANDROID_BUILD_TOP/bootable/recovery/testdata

WORK_DIR=/data/local/tmp

# set to 0 to use a device instead
USE_EMULATOR=0

# ------------------------

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
  echo "::: testing $1 :::"
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


cleanup() {
  # not necessary if we're about to kill the emulator, but nice for
  # running on real devices or already-running emulators.
  run_command rm $WORK_DIR/verifier_test
  run_command rm $WORK_DIR/package.zip

  [ "$pid_emulator" == "" ] || kill $pid_emulator
}

$ADB push $ANDROID_PRODUCT_OUT/system/bin/verifier_test \
          $WORK_DIR/verifier_test

expect_succeed() {
  testname "$1 (should succeed)"
  $ADB push $DATA_DIR/$1 $WORK_DIR/package.zip
  run_command $WORK_DIR/verifier_test $WORK_DIR/package.zip || fail
}

expect_fail() {
  testname "$1 (should fail)"
  $ADB push $DATA_DIR/$1 $WORK_DIR/package.zip
  run_command $WORK_DIR/verifier_test $WORK_DIR/package.zip && fail
}

expect_fail unsigned.zip
expect_fail jarsigned.zip
expect_succeed otasigned.zip
expect_fail random.zip
expect_fail fake-eocd.zip
expect_fail alter-metadata.zip
expect_fail alter-footer.zip

# --------------- cleanup ----------------------

cleanup

echo
echo PASS
echo
