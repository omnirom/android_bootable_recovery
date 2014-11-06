#!/bin/bash
#
# A test suite for recovery's package signature verifier.  Run in a
# client where you have done envsetup, lunch, etc.
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
  shift
  run_command $WORK_DIR/verifier_test "$@" $WORK_DIR/package.zip || fail
}

expect_fail() {
  testname "$1 (should fail)"
  $ADB push $DATA_DIR/$1 $WORK_DIR/package.zip
  shift
  run_command $WORK_DIR/verifier_test "$@" $WORK_DIR/package.zip && fail
}

# not signed at all
expect_fail unsigned.zip
# signed in the pre-donut way
expect_fail jarsigned.zip

# success cases
expect_succeed otasigned.zip -e3
expect_succeed otasigned_f4.zip -f4
expect_succeed otasigned_sha256.zip -e3 -sha256
expect_succeed otasigned_f4_sha256.zip -f4 -sha256
expect_succeed otasigned_ecdsa_sha256.zip -ec -sha256

# success with multiple keys
expect_succeed otasigned.zip -f4 -e3
expect_succeed otasigned_f4.zip -ec -f4
expect_succeed otasigned_sha256.zip -ec -e3 -e3 -sha256
expect_succeed otasigned_f4_sha256.zip -ec -sha256 -e3 -f4 -sha256
expect_succeed otasigned_ecdsa_sha256.zip -f4 -sha256 -e3 -ec -sha256

# verified against different key
expect_fail otasigned.zip -f4
expect_fail otasigned_f4.zip -e3
expect_fail otasigned_ecdsa_sha256.zip -e3 -sha256

# verified against right key but wrong hash algorithm
expect_fail otasigned.zip -e3 -sha256
expect_fail otasigned_f4.zip -f4 -sha256
expect_fail otasigned_sha256.zip
expect_fail otasigned_f4_sha256.zip -f4
expect_fail otasigned_ecdsa_sha256.zip

# various other cases
expect_fail random.zip
expect_fail fake-eocd.zip
expect_fail alter-metadata.zip
expect_fail alter-footer.zip

# --------------- cleanup ----------------------

cleanup

echo
echo PASS
echo
