#!/sbin/sh
#
# Use vold to decrypt device in TWRP
#

# TODO
# * Change back to C to avoid dependencies on busybox/toolbox/toybox
# * Add error checking

OUPUT_DMESG_TO_LOG=0;

# =============================================================================
# Functions
# =============================================================================
wait_for_property() {
	RETURN_VALUE=$1
	local l_PROPERTY_NAME=$2
	local l_EXPECTED_VALUE=$3
	local l_TIMEOUT=$4

	while [ $l_TIMEOUT -gt 0 ]; do
		if [ "$(getprop $l_PROPERTY_NAME)" == "$l_EXPECTED_VALUE" ]; then
			break
		fi
		sleep 1
		let l_TIMEOUT-=1
	done

	eval $RETURN_VALUE="$(getprop $l_PROPERTY_NAME)"
}

convert_key_to_hex_ascii() {
	RETURN_VALUE=$1
	local l_PASSWD=$2

	local l_HEXSTRING=""
	local l_i=0
	while [ $l_i -lt ${#l_PASSWD} ]; do
		l_HEXSTRING=$l_HEXSTRING$(echo -n "${l_PASSWD:$l_i:1}" | hexdump -e '"%02x"')
		let l_i+=1
	done

	eval $RETURN_VALUE="$l_HEXSTRING"
}

get_property_from_buildprop() {
	RETURN_VALUE=$1
	local l_PROPERTY=$2
	local l_DEFAULT_VALUE=$3
	local l_FILE="/system/build.prop"

	local l_PROP_VALUE="$(sed '/^\#/d' "$l_FILE" | grep "$l_PROPERTY" | tail -n 1 | sed 's/^.*=//')"

	if [ "$l_PROP_VALUE" == "" ]; then
		eval $RETURN_VALUE="$l_DEFAULT_VALUE"
	else
		eval $RETURN_VALUE="$l_PROP_VALUE"
	fi
}

echo_vold_services_status() {
	# Check status of services after running vdc
	echo "qseecomd service status:" $(getprop init.svc.qseecomd)
	echo "vold service status:" $(getprop init.svc.vold)
}

echo_dmesg_to_recoverylog() {
	echo "---- DMESG LOG FOLLOWS ----"
	dmesg | grep 'DECRYPT\|vold\|qseecom\|QSEECOM\|keymaste\|keystore\|cmnlib'
	echo "---- DMESG LOG ENDS ----"
}


# =============================================================================
# Main Program
# =============================================================================

if [ ! -f "/system/bin/vold" ]; then
	echo "ERROR: /system/bin/vold not found, aborting."
	exit 1
fi
if [ ! -f "/system/bin/vdc" ]; then
	echo "ERROR: /system/bin/vdc not found, aborting."
	exit 1
fi


# Check if TWRPs sbinqseecomd is running
SBINQSEECOMD_STATE="$(getprop init.svc.sbinqseecomd)"
if [ "$SBINQSEECOMD_STATE" != "" ] && [ "$SBINQSEECOMD_STATE" != "stopped" ]; then
	echo "sbinqseecomd is running, stopping it..."
	setprop ctl.stop sbinqseecomd
	wait_for_property RES "init.svc.sbinqseecomd" "stopped" 5
	echo "$RES"
fi

# Rename vendor and firmware dirs if they exist
echo "Setting up folders and permissions..."
echo "[DECRYPT]Setting up folders and permissions..."  >>/dev/kmsg
if [ -d /system/vendor ]; then
	# Link /vendor to /system/vendor for devices without a vendor partition.
	echo "Symlinking vendor folder..."
	echo "[DECRYPT]Symlinking vendor folder..."   >>/dev/kmsg
	mv vendor vendor-orig                         2>/dev/kmsg
	mkdir -p /vendor/firmware/keymaster           2>/dev/kmsg
	ln -s /system/vendor/lib64 /vendor/lib64      2>/dev/kmsg
	ln -s /system/vendor/lib /vendor/lib          2>/dev/kmsg
	VENDOR_LINK=true
else
	VENDOR_LINK=false
fi

echo "Symlinking firmware files..."
mv firmware firmware-orig   2>/dev/kmsg
mkdir -p /firmware/image    2>/dev/kmsg
find /system -name keymaste*.* -o -name cmnlib.* | while read fwfile; do
	echo "[DECRYPT]Symlinking '$(ls -l $fwfile)'"   >>/dev/kmsg
	ln -s $fwfile /firmware/image/                  2>/dev/kmsg
	if $VENDOR_LINK ; then
		ln -s $fwfile /vendor/firmware/             2>/dev/kmsg
		ln -s $fwfile /vendor/firmware/keymaster/   2>/dev/kmsg
	fi
done

# vold won't start without ro.storage_structure on Kitkat
get_property_from_buildprop API_LEVEL "ro.build.version.sdk" 20
if [ $API_LEVEL -le 19 ]; then
	get_property_from_buildprop PROP_VALUE "ro.storage_structure" ""
	if [ "$PROP_VALUE" != "" ]; then
		setprop ro.storage_structure "$PROP_VALUE"
	fi
fi

# Start services needed for vold decrypt
echo "Starting services..."
echo "[DECRYPT]Starting services..." >>/dev/kmsg
setprop ctl.start qseecomd
setprop ctl.start vold

wait_for_property RES "init.svc.vold" "running" 5
if [ "$RES" == "running" ]; then

	# This is needed, even if vold is running, the socket may not be ready
	# and result in "Error connecting to cryptd: Connection refused"
	sleep 1
	# Alternatives:
	#    * add a function to check for the socket
	#    * use timeout with vdc --wait (but not all vdc's have --wait)
	#         eg: timeout -t 30 vdc --wait cryptfs checkpw "$1"
	#         - you have to use timeout (or some kind of kill loop), otherwise if the socket never becomes
	#           available we'll be stuck here! (TWRP splash)

	echo "[DECRYPT]About to run vdc..." >>/dev/kmsg

	# Input password from GUI, or default password
	VDC_RES_A="$(LD_LIBRARY_PATH=/system/lib64:/system/lib /system/bin/vdc cryptfs checkpw "$1")"
	RES=$?
	echo "vdc cryptfs result (passwd): $VDC_RES_A (ret=$RES)"
	echo "[DECRYPT]vdc cryptfs result (passwd): $VDC_RES_A (ret=$RES)" >>/dev/kmsg

	VDC_R3="$(echo $VDC_RES_A | cut -f3 -d ' ')"

	if [ "$VDC_R3" != "0" ]; then
		#~ // try falling back to Lollipop hex passwords
		convert_key_to_hex_ascii HEX_PASSWD "$1"

		VDC_RES_B="$(LD_LIBRARY_PATH=/system/lib64:/system/lib /system/bin/vdc cryptfs checkpw "$HEX_PASSWD")"
		RES=$?
		echo "vdc cryptfs result (hex_pw): $VDC_RES_B (ret=$RES)"
		echo "[DECRYPT]vdc cryptfs result (hex_pw): $VDC_RES_B (ret=$RES)" >>/dev/kmsg
	fi

	if [ $RES != 0 ]; then
		echo "[DECRYPT]vdc returned an error $RES" >>/dev/kmsg
		OUPUT_DMESG_TO_LOG=1
	fi

	sleep 1 # doesn't seem needed, but maybe we should keep it to make sure vold has finished before stopping it
else
	echo "Failed to start vold:"
	echo "$(getprop | grep init.svc)"
	echo "[DECRYPT]Failed to start vold" >>/dev/kmsg
	OUPUT_DMESG_TO_LOG=1
fi

# Stop services needed for vold decrypt so /system can be unmounted
echo "[DECRYPT]Stopping services..." >>/dev/kmsg
setprop ctl.stop vold
setprop ctl.stop qseecomd

# Restore original firmware and vendor
rm -rf firmware             2>/dev/kmsg
mv firmware-orig firmware   2>/dev/kmsg
if $VENDOR_LINK ; then
	rm -rf vendor           2>/dev/kmsg
	mv vendor-orig vendor   2>/dev/kmsg
fi

# Start sbinqseecomd if it was previously running
if [ "$SBINQSEECOMD_STATE" != "" ] && [ "$SBINQSEECOMD_STATE" != "stopped" ]; then
	setprop ctl.start sbinqseecomd
fi

echo "[DECRYPT]Finished." >>/dev/kmsg

if [ $OUPUT_DMESG_TO_LOG == 1 ]; then
	echo_dmesg_to_recoverylog
fi

exit 0
