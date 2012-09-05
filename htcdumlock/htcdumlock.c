/*
 * This binary is a workaround for HTC's unlock method that doesn't let
 * you flash boot while booted to recovery.  It is designed to dump
 * recovery and boot to the sdcard then flash recovery to boot. When
 * used with a supported recovery, you can reflash the dumped copy of
 * boot once you enter the recovery.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * The code was written from scratch by Dees_Troy dees_troy at
 * yahoo
 *
 * Copyright (c) 2012
 *
 * Note that this all could probably be done as a shell script, but
 * I am much better at C than I am at scripting. :)
 */
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

// Number of bytes in the ramdisks to compare
#define SCAN_SIZE 60

#define CMDLINE_SERIALNO        "androidboot.serialno="
#define CMDLINE_SERIALNO_LEN    (strlen(CMDLINE_SERIALNO))
#define CPUINFO_SERIALNO        "Serial"
#define CPUINFO_SERIALNO_LEN    (strlen(CPUINFO_SERIALNO))
#define CPUINFO_HARDWARE        "Hardware"
#define CPUINFO_HARDWARE_LEN    (strlen(CPUINFO_HARDWARE))

char device_id[255];
int verbose = 0, java = 0;

void get_device_id(void)
{
	FILE *fp;
    char line[2048];
	char hardware_id[32];
	char* token;

    // Assign a blank device_id to start with
    device_id[0] = 0;

    // First, try the cmdline to see if the serial number was supplied
	fp = fopen("/proc/cmdline", "rt");
	if (fp != NULL)
    {
        // First step, read the line. For cmdline, it's one long line
        fgets(line, sizeof(line), fp);
        fclose(fp);

        // Now, let's tokenize the string
        token = strtok(line, " ");

        // Let's walk through the line, looking for the CMDLINE_SERIALNO token
        while (token)
        {
            // We don't need to verify the length of token, because if it's too short, it will mismatch CMDLINE_SERIALNO at the NULL
            if (memcmp(token, CMDLINE_SERIALNO, CMDLINE_SERIALNO_LEN) == 0)
            {
                // We found the serial number!
                strcpy(device_id, token + CMDLINE_SERIALNO_LEN);
                return;
            }
            token = strtok(NULL, " ");
        }
    }

	// Now we'll try cpuinfo for a serial number
	fp = fopen("/proc/cpuinfo", "rt");
	if (fp != NULL)
    {
		while (fgets(line, sizeof(line), fp) != NULL) { // First step, read the line.
			if (memcmp(line, CPUINFO_SERIALNO, CPUINFO_SERIALNO_LEN) == 0)  // check the beginning of the line for "Serial"
			{
				// We found the serial number!
				token = line + CPUINFO_SERIALNO_LEN; // skip past "Serial"
				while (((int)*token > 0 && (int)*token <= 32 ) || (int)*token == ':') token++; // skip over all spaces and the colon
				if (*token != NULL) {
                    token[30] = 0;
					if (token[strlen(token)-1] == 10) { // checking for endline chars and dropping them from the end of the string if needed
						memset(device_id, 0, sizeof(device_id));
						strncpy(device_id, token, strlen(token) - 1);
					} else {
						strcpy(device_id, token);
					}
					if (verbose)
						printf("serial from cpuinfo: '%s'\n", device_id);
					fclose(fp);
					return;
				}
			} else if (memcmp(line, CPUINFO_HARDWARE, CPUINFO_HARDWARE_LEN) == 0) {// We're also going to look for the hardware line in cpuinfo and save it for later in case we don't find the device ID
				// We found the hardware ID
				token = line + CPUINFO_HARDWARE_LEN; // skip past "Hardware"
				while (((int)*token > 0 && (int)*token <= 32 ) || (int)*token == ':')  token++; // skip over all spaces and the colon
				if (*token != NULL) {
                    token[30] = 0;
					if (token[strlen(token)-1] == 10) { // checking for endline chars and dropping them from the end of the string if needed
                        memset(hardware_id, 0, sizeof(hardware_id));
						strncpy(hardware_id, token, strlen(token) - 1);
					} else {
						strcpy(hardware_id, token);
					}
					if (verbose)
						printf("hardware id from cpuinfo: '%s'\n", hardware_id);
				}
			}
		}
		fclose(fp);
    }
	
	if (hardware_id[0] != 0) {
		if (verbose)
			printf("using hardware id for device id: '%s'\n", hardware_id);
		strcpy(device_id, hardware_id);
		return;
	}

    strcpy(device_id, "serialno");
	if (verbose)
		printf("device id not found, using '%s'.", device_id);
    return;
}

void reboot_device() {
	// Reboot
	printf("Rebooting!\n");
	system("reboot system");
}

void scan_for_ramdisk_data(char *filename, char *ramdisk) {
	FILE *pFile;
	unsigned long lSize;
	unsigned char *buffer;
	size_t result;
	int i;

	pFile = fopen(filename, "rb");
	if(pFile==NULL){
		printf("Unabled to open image.\nFailed\n");
		exit(1);
	}

	fseek (pFile , 0 , SEEK_END);
	lSize = ftell(pFile);
	rewind(pFile);

	//printf("\n\nFile is %ld bytes big\n\n", lSize);

	buffer = (unsigned char*)malloc(sizeof(unsigned char) * lSize);
	if(buffer == NULL){
		printf("File read error!\nFailed\n");
		exit(2);
	}

	result = fread (buffer, 1, lSize, pFile);
	if (result != lSize) {
		printf("Error reading file '%s'\nFailed\n", filename);
		exit(3);
	}

	unsigned char needle[6] = {0x00, 0x00, 0x00, 0x00, 0x1f, 0x8b};
	unsigned char *last_needle = NULL;
	//char *p = memmem(needle, lSize, buffer, sizeof(needle));
	unsigned char *p = memmem(buffer + 2048, lSize - 2048, needle, sizeof(needle));
	if (!p) {
		fclose(pFile);
		printf("Ramdisk not found in '%s', error!\nFailed\n", filename);
		exit(4);
	} else {
		//printf("Ramdisk found in '%s'!\n", filename);
	}

	memcpy(ramdisk, p, sizeof(char) * SCAN_SIZE);
	fclose(pFile);
	free(buffer);
}

int compare_ramdisks(char *boot_path, char *recovery_path) {
	char boot_data[SCAN_SIZE], recovery_data[SCAN_SIZE];

	scan_for_ramdisk_data(boot_path, (char*)&boot_data);
	scan_for_ramdisk_data(recovery_path, (char*)&recovery_data);
	if (memcmp(boot_data, recovery_data, sizeof(boot_data)) == 0) {
		if (verbose)
			printf("Boot and recovery are the same.\n");
		return 0;
	} else {
		if (verbose)
			printf("Boot and recovery are NOT the same.\n");
		return 1;
	}
}

void flash_recovery_to_boot(int no_flash, int no_reboot) {
	char twrp_device_path[255], recovery_path[255], boot_path[255],
		exec[255], md5recovery[255], md5boot[255],
		recoveryimg[255], bootimg[255], tempimg[255];
	int ret_val = 0;
	FILE *fp;
	char* token;

	// Create folders
	if (verbose)
		printf("Making '/sdcard/TWRP'\n");
	mkdir("/sdcard/TWRP", 0777);
	if (verbose)
		printf("Making folder '/sdcard/TWRP/htcdumlock'\n");
	mkdir("/sdcard/TWRP/htcdumlock", 0777);
	strcpy(twrp_device_path, "/sdcard/TWRP/htcdumlock/");
	strcat(twrp_device_path, device_id);
	if (verbose)
		printf("Making folder '%s'\n", twrp_device_path);
	mkdir(twrp_device_path, 0777);
	// Make folder for recovery
	strcpy(recovery_path, twrp_device_path);
	strcat(recovery_path, "/recovery");
	if (verbose)
		printf("Making folder '%s'\n", recovery_path);
	mkdir(recovery_path, 0777);
	strcat(recovery_path, "/");
	// Make folder for boot
	strcpy(boot_path, twrp_device_path);
	strcat(boot_path, "/boot");
	if (verbose)
		printf("Making folder '%s'\n", boot_path);
	mkdir(boot_path, 0777);
	strcat(boot_path, "/");

	// Set up file locations
	strcpy(recoveryimg, recovery_path);
	strcat(recoveryimg, "recovery.img");
	strcpy(bootimg, boot_path);
	strcat(bootimg, "boot.img");
	strcpy(tempimg, twrp_device_path);
	strcat(tempimg, "/temp.img");

	// Dump recovery
	strcpy(exec, "dump_image recovery ");
	strcat(exec, recoveryimg);
	if (verbose)
		printf("Running command: '%s'\n", exec);
	ret_val = system(exec);
	if (ret_val != 0) {
		printf("Unable to dump recovery.\nFailed\n");
		return;
	}

	// Dump boot (kernel)
	strcpy(exec, "dump_image boot ");
	strcat(exec, tempimg);
	if (verbose)
		printf("Running command: '%s'\n", exec);
	ret_val = system(exec);
	if (ret_val != 0) {
		printf("Unable to dump recovery.\nFailed\n");
		return;
	}

	// Compare the ramdisks of the images from boot and recovery to make sure they are different
	// If they are the same, then recovery is already flashed to boot and we don't want to wipe
	// out our existing backup of boot
	if (compare_ramdisks(tempimg, recoveryimg) != 0) {
		if (verbose)
			printf("Boot and recovery do not match so recovery is not flashed to boot yet...\n");
		strcpy(exec, "mv ");
		strcat(exec, tempimg);
		strcat(exec, " ");
		strcat(exec, bootimg);
		if (verbose)
			printf("Moving temporary boot.img: '%s'\n", exec);
		ret_val = system(exec);
		if (ret_val != 0) {
			printf("Unable to move temporary boot image.\nFailed\n");
			return;
		}
	} else {
		if (!java)
			printf("Ramdisk recovery and boot matches! Recovery is already flashed to boot!\n");
		if (!no_reboot)
			reboot_device();
		return;
	}

	// Flash recovery to boot
	strcpy(exec, "flash_image boot ");
	strcat(exec, recoveryimg);
	if (no_flash) {
		if (verbose)
			printf("NOT flashing recovery to boot due to argument 'noflash', command is '%s'\n", exec);
	} else {
		if (verbose)
			printf("Running command: '%s'\n", exec);
		ret_val = system(exec);
		if (ret_val != 0) {
			printf("Unable to flash recovery to boot.\nFailed\n");
			return;
		}
	}

	if (!no_reboot && !ret_val)
		reboot_device();
}

void restore_original_boot(int no_flash) {
	char boot_path[255], exec[255];

	// Restore original boot partition
	strcpy(boot_path, "/sdcard/TWRP/htcdumlock/");
	strcat(boot_path, device_id);
	strcat(boot_path, "/boot/");
	strcpy(exec, "flash_image boot ");
	strcat(exec, boot_path);
	strcat(exec, "boot.img");
	if (no_flash) {
		if (verbose)
			printf("NOT restoring boot due to argument 'noflash', command is '%s'\n", exec);
	} else {
		if (verbose)
			printf("Running command: '%s'\n", exec);
		system(exec);
	}
}

int main(int argc, char** argv)
{
	int recovery = 0, no_flash = 0, restore_boot = 0, arg_error = 0,
		no_reboot = 0, i;

	// Parse the arguments
	if (argc < 2)
		arg_error = 1;
	else {
		for (i=1; i<argc; i++) {
			if (strcmp(argv[i], "recovery") == 0) {
				// Check to see if restore option is already set
				// Do not allow user to do recovery and restore at the same time
				if (restore_boot)
					arg_error = 1;
				recovery = 1;
			} else if (strcmp(argv[i], "restore") == 0) {
				// Check to see if recovery option is already set
				// Do not allow user to do recovery and restore at the same time
				if (recovery)
					arg_error = 1;
				restore_boot = 1;
			} else if (strcmp(argv[i], "noflash") == 0)
				no_flash = 1;
			else if (strcmp(argv[i], "noreboot") == 0)
				no_reboot = 1;
			else if (strcmp(argv[i], "verbose") == 0)
				verbose = 1;
			else if (strcmp(argv[i], "java") == 0)
				java = 1;
			else
				arg_error = 1;
		}
	}
	if (arg_error) {
		printf("Invalid argument given.\n");
		printf("Valid arguments are:\n");
		printf("  recovery -- backs up boot and recovery and flashes recovery to boot\n");
		printf("  restore  -- restores the most recent backup of boot made by this utility\n");
		printf("  noflash  -- same as 'recovery' but does not flash boot or reboot at the end\n");
		printf("  noreboot -- does not reboot after flashing boot during 'recovery'\n");
		printf("  verbose  -- show extra debug information\n");
		printf("\nNOTE: You cannot do 'recovery' and 'restore' in the same operation.\nFailed\n");
		return 0;
	}

	get_device_id();
	if (verbose)
		printf("Device ID is: '%s'\n", device_id);
	if (strcmp(device_id, "0000000000000000") == 0) {
		printf("Error, device ID is all zeros!\n");
		printf("Did you 'su' first? HTC Dumlock requires root access.\nFailed\n");
		return 0;
	}

	if (recovery) {
		if (!java)
			printf("Flashing recovery to boot, this may take a few minutes . . .\n");
		flash_recovery_to_boot(no_flash, no_reboot);
	}
	if (restore_boot) {
		printf("Restoring boot, this may take a few minutes . . .\n");
		restore_original_boot(no_flash);
	}

	return 0;
}
