#!/sbin/sh
#
# Warning: if you want to run this script in cm-recovery change the above to #!/sbin/sh
#
# fix_permissions - fixes permissions on Android data directories after upgrade
# shade@chemlab.org
#
# original concept: http://blog.elsdoerfer.name/2009/05/25/android-fix-package-uid-mismatches/
# implementation by: Cyanogen
# improved by: ankn, smeat, thenefield, farmatito, rikupw, Kastro
#
# v1.1-v1.31r3 - many improvements and concepts from XDA developers.
# v1.34 through v2.00 -  A lot of frustration [by Kastro]
# v2.01	- Completely rewrote the script for SPEED, thanks for the input farmatito
#         /data/data depth recursion is tweaked;
#         fixed single mode;
#         functions created for modularity;
#         logging can be disabled via CLI for more speed;
#         runtime computation added to end (Runtime: mins secs);
#         progress (current # of total) added to screen;
#         fixed CLI argument parsing, now you can have more than one option!;
#         debug cli option;
#         verbosity can be disabled via CLI option for less noise;;
#         [by Kastro, (XDA: k4str0), twitter;mattcarver]
# v2.02 - ignore com.htc.resources.apk if it exists and minor code cleanups,
#         fix help text, implement simulated run (-s) [farmatito]
# v2.03 - fixed chown group ownership output [Kastro]
# v2.04 - replaced /system/sd with $SD_EXT_DIRECTORY [Firerat]
VERSION="2.04"

# Defaults
DEBUG=0 # Debug off by default
LOGGING=1 # Logging on by default
VERBOSE=1 # Verbose on by default

# Messages
UID_MSG="Changing user ownership for:"
GID_MSG="Changing group ownership for:"
PERM_MSG="Changing permissions for:"

# Programs needed
ECHO="busybox echo"
GREP="busybox grep"
EGREP="busybox egrep"
CAT="busybox cat"
CHOWN="busybox chown"
CHMOD="busybox chmod"
MOUNT="busybox mount"
UMOUNT="busybox umount"
CUT="busybox cut"
FIND="busybox find"
LS="busybox ls"
TR="busybox tr"
TEE="busybox tee"
TEST="busybox test"
SED="busybox sed"
RM="busybox rm"
WC="busybox wc"
EXPR="busybox expr"
DATE="busybox date"

# Initialise vars
CODEPATH=""
UID=""
GID=""
PACKAGE=""
REMOVE=0
NOSYSTEM=0
ONLY_ONE=""
SIMULATE=0
SYSREMOUNT=0
SYSMOUNT=0
DATAMOUNT=0
SYSSDMOUNT=0
FP_STARTTIME=$( $DATE +"%m-%d-%Y %H:%M:%S" )
FP_STARTEPOCH=$( $DATE +%s )
if $TEST "$SD_EXT_DIRECTORY" = ""; then
    #check for mount point, /system/sd included in tests for backward compatibility
    for MP in /sd-ext /system/sd;do
        if $TEST -d $MP; then
            SD_EXT_DIRECTORY=$MP
            break
        fi
    done
fi
fp_usage()
{
   $ECHO "Usage $0 [OPTIONS] [APK_PATH]"
   $ECHO "      -d         turn on debug"
   $ECHO "      -f         fix only package APK_PATH"
   $ECHO "      -l         disable logging for this run (faster)"
   $ECHO "      -r         remove stale data directories"
   $ECHO "                 of uninstalled packages while fixing permissions"
   $ECHO "      -s         simulate only"
   $ECHO "      -u         check only non-system directories"
   $ECHO "      -v         disable verbosity for this run (less output)"
   $ECHO "      -V         print version"
   $ECHO "      -h         this help"
}

fp_parseargs()
{
   # Parse options
   while $TEST $# -ne 0; do
      case "$1" in
         -d)
            DEBUG=1
         ;;
         -f)
            if $TEST $# -lt 2; then
               $ECHO "$0: missing argument for option $1"
               exit 1
            else
               if $TEST $( $ECHO $2 | $CUT -c1 ) != "-"; then
                  ONLY_ONE=$2
                  shift;
               else
                  $ECHO "$0: missing argument for option $1"
                  exit 1
               fi
            fi
         ;;
         -r)
            REMOVE=1
         ;;
         -s)
            SIMULATE=1
         ;;
         -l)
            if $TEST $LOGGING -eq 0; then
               LOGGING=1
            else
               LOGGING=0
            fi
         ;;
         -v)
            if $TEST $VERBOSE -eq 0; then
               VERBOSE=1
            else
               VERBOSE=0
            fi
         ;;
         -u)
            NOSYSTEM=1
         ;;
         -V)
            $ECHO "$0 $VERSION"
            exit 0
         ;;
         -h)
            fp_usage
            exit 0
         ;;
         -*)
            $ECHO "$0: unknown option $1"
            $ECHO
            fp_usage
            exit 1
         ;;
      esac
      shift;
   done
}

fp_print()
{
   MSG=$@
   if $TEST $LOGGING -eq 1; then
      $ECHO $MSG | $TEE -a $LOG_FILE
   else
      $ECHO $MSG
   fi
}

fp_start()
{
   if $TEST $SIMULATE -eq 0 ; then
      if $TEST $( $GREP -c " /system " "/proc/mounts" ) -ne 0; then
         DEVICE=$( $GREP " /system " "/proc/mounts" | $CUT -d ' ' -f1 )
         if $TEST $DEBUG -eq 1; then
            fp_print "/system mounted on $DEVICE"
         fi
         if $TEST $( $GREP " /system " "/proc/mounts" | $GREP -c " ro " ) -ne 0; then
            $MOUNT -o remount,rw $DEVICE /system
            SYSREMOUNT=1
         fi
      else
         $MOUNT /system > /dev/null 2>&1
         SYSMOUNT=1
      fi
      
      if $TEST $( $GREP -c " /data " "/proc/mounts" ) -eq 0; then
         $MOUNT /data > /dev/null 2>&1
         DATAMOUNT=1
      fi
      
      if $TEST -e /dev/block/mmcblk0p2 && $TEST $( $GREP -c " $SD_EXT_DIRECTORY " "/proc/mounts" ) -eq 0; then
         $MOUNT $SD_EXT_DIRECTORY > /dev/null 2>&1
         SYSSDMOUNT=1
      fi
   fi
   if $TEST $( $MOUNT | $GREP -c /sdcard ) -eq 0; then
      LOG_FILE="/data/fix_permissions.log"
   else
      LOG_FILE="/sdcard/fix_permissions.log"
   fi
   if $TEST ! -e "$LOG_FILE"; then
      > $LOG_FILE
   fi
   
   fp_print "$0 $VERSION started at $FP_STARTTIME"
}

fp_chown_uid()
{
   FP_OLDUID=$1
   FP_UID=$2
   FP_FILE=$3
   
   #if user ownership doesn't equal then change them
   if $TEST "$FP_OLDUID" != "$FP_UID"; then
      if $TEST $VERBOSE -ne 0; then
         fp_print "$UID_MSG $FP_FILE from '$FP_OLDUID' to '$FP_UID'"
      fi
      if $TEST $SIMULATE -eq 0; then
         $CHOWN $FP_UID "$FP_FILE"
      fi
   fi
}

fp_chown_gid()
{
   FP_OLDGID=$1
   FP_GID=$2
   FP_FILE=$3
   
   #if group ownership doesn't equal then change them
   if $TEST "$FP_OLDGID" != "$FP_GID"; then
      if $TEST $VERBOSE -ne 0; then
         fp_print "$GID_MSG $FP_FILE from '$FP_OLDGID' to '$FP_GID'"
      fi
      if $TEST $SIMULATE -eq 0; then
         $CHOWN :$FP_GID "$FP_FILE"
      fi
   fi
}

fp_chmod()
{
   FP_OLDPER=$1
   FP_OLDPER=$( $ECHO $FP_OLDPER | cut -c2-10 )
   FP_PERSTR=$2
   FP_PERNUM=$3
   FP_FILE=$4
   
   #if the permissions are not equal
   if $TEST "$FP_OLDPER" != "$FP_PERSTR"; then
      if $TEST $VERBOSE -ne 0; then
         fp_print "$PERM_MSG $FP_FILE from '$FP_OLDPER' to '$FP_PERSTR' ($FP_PERNUM)"
      fi
      #change the permissions
      if $TEST $SIMULATE -eq 0; then
         $CHMOD $FP_PERNUM "$FP_FILE"
      fi
   fi
}

fp_all()
{
   FP_NUMS=$( $CAT /data/system/packages.xml | $EGREP "^<package.*serId" | $GREP -v framework-res.apk | $GREP -v com.htc.resources.apk | $WC -l )
   I=0
   $CAT /data/system/packages.xml | $EGREP "^<package.*serId" | $GREP -v framework-res.apk | $GREP -v com.htc.resources.apk | while read all_line; do
      I=$( $EXPR $I + 1 )
      fp_package "$all_line" $I $FP_NUMS
   done
}

fp_single()
{
   FP_SFOUND=$( $CAT /data/system/packages.xml | $EGREP "^<package.*serId" | $GREP -v framework-res.apk | $GREP -v com.htc.resources.apk | $GREP -i $ONLY_ONE | wc -l )
   if $TEST $FP_SFOUND -gt 1; then
      fp_print "Cannot perform single operation on $FP_SFOUND matched package(s)."
      elif $TEST $FP_SFOUND = "" -o $FP_SFOUND -eq 0; then
      fp_print "Could not find the package you specified in the packages.xml file."
   else
      FP_SPKG=$( $CAT /data/system/packages.xml | $EGREP "^<package.*serId" | $GREP -v framework-res.apk | $GREP -v com.htc.resources.apk | $GREP -i $ONLY_ONE )
      fp_package "${FP_SPKG}" 1 1
   fi
}

fp_package()
{
   pkgline=$1
   curnum=$2
   endnum=$3
   CODEPATH=$( $ECHO $pkgline | $SED 's%.* codePath="\(.*\)".*%\1%' |  $CUT -d '"' -f1 )
   PACKAGE=$( $ECHO $pkgline | $SED 's%.* name="\(.*\)".*%\1%' | $CUT -d '"' -f1 )
   UID=$( $ECHO $pkgline | $SED 's%.*serId="\(.*\)".*%\1%' |  $CUT -d '"' -f1 )
   GID=$UID
   APPDIR=$( $ECHO $CODEPATH | $SED 's%^\(.*\)/.*%\1%' )
   APK=$( $ECHO $CODEPATH | $SED 's%^.*/\(.*\..*\)$%\1%' )
   
   #debug
   if $TEST $DEBUG -eq 1; then
      fp_print "CODEPATH: $CODEPATH APPDIR: $APPDIR APK:$APK UID/GID:$UID:$GID"
   fi
   
   #check for existence of apk
   if $TEST -e $CODEPATH;  then
      fp_print "Processing ($curnum of $endnum): $PACKAGE..."
      
      #lets get existing permissions of CODEPATH
      OLD_UGD=$( $LS -ln "$CODEPATH" )
      OLD_PER=$( $ECHO $OLD_UGD | $CUT -d ' ' -f1 )
      OLD_UID=$( $ECHO $OLD_UGD | $CUT -d ' ' -f3 )
      OLD_GID=$( $ECHO $OLD_UGD | $CUT -d ' ' -f4 )
      
      #apk source dirs
      if $TEST "$APPDIR" = "/system/app"; then
         #skip system apps if set
         if $TEST "$NOSYSTEM" = "1"; then
            fp_print "***SKIPPING SYSTEM APP ($PACKAGE)!"
            return
         fi
         fp_chown_uid $OLD_UID 0 "$CODEPATH"
         fp_chown_gid $OLD_GID 0 "$CODEPATH"
         fp_chmod $OLD_PER "rw-r--r--" 644 "$CODEPATH"
         elif $TEST "$APPDIR" = "/data/app" || $TEST "$APPDIR" = "/sd-ext/app"; then
         fp_chown_uid $OLD_UID 1000 "$CODEPATH"
         fp_chown_gid $OLD_GID 1000 "$CODEPATH"
         fp_chmod $OLD_PER "rw-r--r--" 644 "$CODEPATH"
         elif $TEST "$APPDIR" = "/data/app-private" || $TEST "$APPDIR" = "/sd-ext/app-private"; then
         fp_chown_uid $OLD_UID 1000 "$CODEPATH"
         fp_chown_gid $OLD_GID $GID "$CODEPATH"
         fp_chmod $OLD_PER "rw-r-----" 640 "$CODEPATH"
      fi
   else
      fp_print "$CODEPATH does not exist ($curnum of $endnum). Reinstall..."
      if $TEST $REMOVE -eq 1; then
         if $TEST -d /data/data/$PACKAGE ; then
            fp_print "Removing stale dir /data/data/$PACKAGE"
            if $TEST $SIMULATE -eq 0 ; then
               $RM -R /data/data/$PACKAGE
            fi
         fi
      fi
   fi
   
   #the data/data for the package
   if $TEST -d "/data/data/$PACKAGE"; then
      #find all directories in /data/data/$PACKAGE
      $FIND /data/data/$PACKAGE -type d -exec $LS -ldn {} \; | while read dataline; do
         #get existing permissions of that directory
         OLD_PER=$( $ECHO $dataline | $CUT -d ' ' -f1 )
         OLD_UID=$( $ECHO $dataline | $CUT -d ' ' -f3 )
         OLD_GID=$( $ECHO $dataline | $CUT -d ' ' -f4 )
         FILEDIR=$( $ECHO $dataline | $CUT -d ' ' -f9 )
         FOURDIR=$( $ECHO $FILEDIR | $CUT -d '/' -f5 )
         
         #set defaults for iteration
         ISLIB=0
         REVPERM=755
         REVPSTR="rwxr-xr-x"
         REVUID=$UID
         REVGID=$GID
         
         if $TEST "$FOURDIR" = ""; then
            #package directory, perms:755 owner:$UID:$GID
            fp_chmod $OLD_PER "rwxr-xr-x" 755 "$FILEDIR"
            elif $TEST "$FOURDIR" = "lib"; then
            #lib directory, perms:755 owner:1000:1000
            #lib files, perms:755 owner:1000:1000
            ISLIB=1
            REVPERM=755
            REVPSTR="rwxr-xr-x"
            REVUID=1000
            REVGID=1000
            fp_chmod $OLD_PER "rwxr-xr-x" 755 "$FILEDIR"
            elif $TEST "$FOURDIR" = "shared_prefs"; then
            #shared_prefs directories, perms:771 owner:$UID:$GID
            #shared_prefs files, perms:660 owner:$UID:$GID
            REVPERM=660
            REVPSTR="rw-rw----"
            fp_chmod $OLD_PER "rwxrwx--x" 771 "$FILEDIR"
            elif $TEST "$FOURDIR" = "databases"; then
            #databases directories, perms:771 owner:$UID:$GID
            #databases files, perms:660 owner:$UID:$GID
            REVPERM=660
            REVPSTR="rw-rw----"
            fp_chmod $OLD_PER "rwxrwx--x" 771 "$FILEDIR"
            elif $TEST "$FOURDIR" = "cache"; then
            #cache directories, perms:771 owner:$UID:$GID
            #cache files, perms:600 owner:$UID:GID
            REVPERM=600
            REVPSTR="rw-------"
            fp_chmod $OLD_PER "rwxrwx--x" 771 "$FILEDIR"
         else
            #other directories, perms:771 owner:$UID:$GID
            REVPERM=771
            REVPSTR="rwxrwx--x"
            fp_chmod $OLD_PER "rwxrwx--x" 771 "$FILEDIR"
         fi
         
         #change ownership of directories matched
         if $TEST "$ISLIB" = "1"; then
            fp_chown_uid $OLD_UID 1000 "$FILEDIR"
            fp_chown_gid $OLD_GID 1000 "$FILEDIR"
         else
            fp_chown_uid $OLD_UID $UID "$FILEDIR"
            fp_chown_gid $OLD_GID $GID "$FILEDIR"
         fi
         
         #if any files exist in directory with improper permissions reset them
         $FIND $FILEDIR -type f -maxdepth 1 ! -perm $REVPERM -exec $LS -ln {} \; | while read subline; do
            OLD_PER=$( $ECHO $subline | $CUT -d ' ' -f1 )
            SUBFILE=$( $ECHO $subline | $CUT -d ' ' -f9 )
            fp_chmod $OLD_PER $REVPSTR $REVPERM "$SUBFILE"
         done
         
         #if any files exist in directory with improper user reset them
         $FIND $FILEDIR -type f -maxdepth 1 ! -user $REVUID -exec $LS -ln {} \; | while read subline; do
            OLD_UID=$( $ECHO $subline | $CUT -d ' ' -f3 )
            SUBFILE=$( $ECHO $subline | $CUT -d ' ' -f9 )
            fp_chown_uid $OLD_UID $REVUID "$SUBFILE"
         done
         
         #if any files exist in directory with improper group reset them
         $FIND $FILEDIR -type f -maxdepth 1 ! -group $REVGID -exec $LS -ln {} \; | while read subline; do
            OLD_GID=$( $ECHO $subline | $CUT -d ' ' -f4 )
            SUBFILE=$( $ECHO $subline | $CUT -d ' ' -f9 )
            fp_chown_gid $OLD_GID $REVGID "$SUBFILE"
         done
      done
   fi
}

date_diff()
{
   if $TEST $# -ne 2; then
      FP_DDM="E"
      FP_DDS="E"
      return
   fi
   FP_DDD=$( $EXPR $2 - $1 )
   FP_DDM=$( $EXPR $FP_DDD / 60 )
   FP_DDS=$( $EXPR $FP_DDD % 60 )
}

fp_end()
{
   if $TEST $SYSREMOUNT -eq 1; then
      $MOUNT -o remount,ro $DEVICE /system > /dev/null 2>&1
   fi
   
   if $TEST $SYSSDMOUNT -eq 1; then
      $UMOUNT $SD_EXT_DIRECTORY > /dev/null 2>&1
   fi
   
   if $TEST $SYSMOUNT -eq 1; then
      $UMOUNT /system > /dev/null 2>&1
   fi
   
   if $TEST $DATAMOUNT -eq 1; then
      $UMOUNT /data > /dev/null 2>&1
   fi
   
   FP_ENDTIME=$( $DATE +"%m-%d-%Y %H:%M:%S" )
   FP_ENDEPOCH=$( $DATE +%s )
   
   date_diff $FP_STARTEPOCH $FP_ENDEPOCH
   
   fp_print "$0 $VERSION ended at $FP_ENDTIME (Runtime:${FP_DDM}m${FP_DDS}s)"
}

#MAIN SCRIPT

fp_parseargs $@
fp_start
if $TEST "$ONLY_ONE" != "" -a "$ONLY_ONE" != "0" ; then
   fp_single "$ONLY_ONE"
else
   fp_all
fi
fp_end
