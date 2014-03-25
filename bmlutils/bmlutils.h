#ifndef BMLUTILS_H_
#define BMLUTILS_H_

int format_rfs_device (const char *device, const char *path);

#define BML_UNLOCK_ALL				0x8A29		///< unlock all partition RO -> RW

#ifndef BOARD_BML_BOOT
#define BOARD_BML_BOOT              "/dev/block/bml7"
#endif

#ifndef BOARD_BML_RECOVERY
#define BOARD_BML_RECOVERY          "/dev/block/bml8"
#endif

#endif // BMLUTILS_H_
