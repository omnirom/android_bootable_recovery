/*
 * This binary allows you to back up the second (recovery) ramdisk on
 * typical Samsung boot images and to inject a new second ramdisk into
 * an existing boot image.
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
 */
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define INJECT_USE_TMP 1

int scan_file_for_data(char *filename, unsigned char *data, int data_size, unsigned long start_location, unsigned long *data_address) {
	FILE *pFile;
	unsigned long lSize;
	unsigned char *buffer, *last_needle = NULL;
	unsigned char needle[data_size];
	size_t result;
	int i, return_val = 0;

	pFile = fopen(filename, "rb");
	if(pFile==NULL){
		printf("Unabled to open file '%s'.\nFailed\n", filename);
		return -1;
	}

	fseek (pFile , 0 , SEEK_END);
	lSize = ftell(pFile);
	rewind(pFile);

	buffer = (unsigned char*)malloc(sizeof(unsigned char) * lSize);
	if(buffer == NULL){
		printf("Memory allocation error on '%s'!\nFailed\n", filename);
		return -1;
	}

	result = fread(buffer, 1, lSize, pFile);
	if (result != lSize) {
		printf("Error reading file '%s'\nFailed\n", filename);
		return -1;
	}

	for (i=0; i<data_size; i++) {
		needle[i] = *data;
		data++;
	}

	unsigned char *p = memmem(buffer + start_location, lSize - start_location, needle, data_size);

	if (!p) {
		return_val = -1;
	} else {
		*data_address = p - buffer + data_size;
	}

	fclose(pFile);
	free(buffer);
	return return_val;
}

int write_new_ramdisk(char *bootimage, char *newramdisk, unsigned long start_location, char *outputimage) {
	FILE *bFile; // bootimage
	FILE *rFile; // ramdisk
	FILE *oFile; // output file
	unsigned long blSize, rlSize, offset_table, ramdisk_len;
	unsigned char *bbuffer, *rbuffer;
	size_t result;
	int return_val;

	// Read the original boot image
	printf("Reading the original boot image...\n");
	bFile = fopen(bootimage, "rb");
	if(bFile==NULL){
		printf("Unabled to open original boot image '%s'.\nFailed\n", bootimage);
		exit(0);
	}

	fseek (bFile , 0 , SEEK_END);
	blSize = ftell(bFile);
	rewind(bFile);
	printf("Size of original boot is %lu\n", blSize);

	bbuffer = (unsigned char*)malloc(sizeof(unsigned char) * blSize);
	if(bbuffer == NULL){
		printf("File read error on original boot image '%s'!\nFailed\n", bootimage);
		exit(0);
	}

	result = fread(bbuffer, 1, blSize, bFile);
	if (result != blSize) {
		printf("Error reading original boot image '%s'\nFailed\n", bootimage);
		exit(0);
	}

	// Find the ramdisk offset table
	unsigned char needle[13] = "recovery_len=";
	return_val = scan_file_for_data(bootimage, &needle, 13, 0, &offset_table);
	if (return_val < 0) {
		fclose(bFile);
		printf("Ramdisk offset table not found in %s!\nFailed\n", bootimage);
		exit(0);
	}
	printf("Ramdisk offset table found at 0x%08x\n", offset_table);

	// Read the ramdisk to insert into the boot image
	printf("Reading the ramdisk...\n");
	rFile = fopen(newramdisk, "rb");
	if(rFile==NULL){
		printf("Unabled to open ramdisk image '%s'.\nFailed\n", newramdisk);
		exit(0);
	}

	fseek (rFile , 0 , SEEK_END);
	rlSize = ftell(rFile);
	rewind(rFile);
	printf("Size of new ramdisk is %lu\n", rlSize);
	ramdisk_len = rlSize / 512;
	if ((rlSize % 512) != 0)
		ramdisk_len++;
	printf("Ramdisk length for offset table: %lu\n", ramdisk_len);

	rbuffer = (unsigned char*)malloc(sizeof(unsigned char) * blSize);
	if(rbuffer == NULL){
		printf("File read error on ramdisk image '%s'!\nFailed\n", newramdisk);
		exit(0);
	}

	result = fread(rbuffer, 1, rlSize, rFile);
	if (result != rlSize) {
		printf("Error reading ramdisk image '%s'\nFailed\n", newramdisk);
		exit(0);
	}

	// Open the output image for writing
	printf("Opening the output image for writing...\n");
	oFile = fopen(outputimage, "wb");
	if(oFile==NULL){
		printf("Unabled to open output image '%s'.\nFailed\n", outputimage);
		exit(0);
	}

	printf("Writing kernel and first ramdisk...\n");
	result = fwrite(bbuffer, 1, start_location - 1, oFile);
	if (result != start_location - 1) {
		printf("Write count does not match! (1)\n");
	}
	fseek(oFile, start_location, SEEK_SET);
	printf("Writing new second ramdisk...\n");
	result = fwrite(rbuffer, 1, rlSize, oFile);
	if (result != rlSize) {
		printf("Write count does not match! (2)\n");
	} else {
		printf("Finished writing new boot image '%s'\n", outputimage);
	}

	// Write new ramdisk_len to offset table
	printf("Writing new ramdisk length to offset table...\n");
	fseek(oFile, offset_table, SEEK_SET);
	char ramdisk_lens[20];
	sprintf(ramdisk_lens, "%lu;\n\n", ramdisk_len);
	fwrite(ramdisk_lens, 1, strlen(ramdisk_lens), oFile);

	fclose(bFile);
	fclose(rFile);
	fclose(oFile);
	free(bbuffer);
	free(rbuffer);

	printf("All done writing new image: '%s'\n", outputimage);
	return 1;
}

int backup_recovery_ramdisk(char *bootimage, unsigned long start_location, char *outputimage) {
	FILE *bFile; // bootimage
	FILE *rFile; // ramdisk
	FILE *oFile; // output file
	unsigned long blSize, offset_table, ramdisk_len;
	unsigned char *bbuffer;
	size_t result;
	unsigned char ramdisk_lens[4], *p;
	int return_val, i;

	// Read the original boot image
	printf("Reading the original boot image...\n");
	bFile = fopen(bootimage, "rb");
	if(bFile==NULL){
		printf("Unabled to open original boot image '%s'.\nFailed\n", bootimage);
		exit(0);
	}

	fseek (bFile , 0 , SEEK_END);
	blSize = ftell(bFile);
	rewind(bFile);
	printf("Size of original boot is %lu\n", blSize);

	bbuffer = (unsigned char*)malloc(sizeof(unsigned char) * blSize);
	if(bbuffer == NULL){
		printf("File read error on original boot image '%s'!\nFailed\n", bootimage);
		exit(0);
	}

	result = fread(bbuffer, 1, blSize, bFile);
	if (result != blSize) {
		printf("Error reading original boot image '%s'\nFailed\n", bootimage);
		exit(0);
	}

	// Find the ramdisk offset table
	unsigned char needle[13] = "recovery_len=";
	return_val = scan_file_for_data(bootimage, &needle, 13, 0, &offset_table);
	if (return_val < 0) {
		fclose(bFile);
		printf("Ramdisk offset table not found in %s!\nFailed\n", bootimage);
		exit(0);
	}
	printf("Ramdisk offset table found at 0x%08x\n", offset_table);
	for (i=0; i<4; i++) {
		p = bbuffer + offset_table + i;
		if (*p == ';') {
			ramdisk_lens[i] = 0;
		} else {
			ramdisk_lens[i] = *p;
		}
	}
	ramdisk_len = atoi(ramdisk_lens);
	ramdisk_len *= 512;
	printf("Ramdisk length: %lu\n", ramdisk_len);

	// Open the output image for writing
	printf("Opening the output image for writing...\n");
	oFile = fopen(outputimage, "wb");
	if(oFile==NULL){
		printf("Unabled to open output image '%s'.\nFailed\n", outputimage);
		exit(0);
	}

	printf("Writing backup ramdisk...\n");
	result = fwrite(bbuffer + start_location, 1, ramdisk_len, oFile);
	if (result != ramdisk_len) {
		printf("Write count does not match! (1)\n");
	} else {
		printf("Finished backing up ramdisk image '%s'\n", outputimage);
	}

	fclose(bFile);
	fclose(oFile);
	free(bbuffer);
	return 1;
}

int find_gzip_recovery_ramdisk(char *boot_image, unsigned long *ramdisk_address) {
	unsigned char gzip_ramdisk[6] = {0x00, 0x00, 0x00, 0x00, 0x1f, 0x8b};
	unsigned long address1, address2;
	int return_val;

	// Find the first ramdisk
	return_val = scan_file_for_data(boot_image, &gzip_ramdisk, 6, 0, &address1);
	if (return_val < 0) {
		printf("No ramdisk found in '%s'\nFailed\n", boot_image);
		printf("This boot image may not be using gzip compression.\n");
		return -1;
	}
	address1 -= 2;
	printf("Ramdisk found in '%s' at offset 0x%08x\n", boot_image, address1);

	// Find the second (recovery) ramdisk
	return_val = scan_file_for_data(boot_image, &gzip_ramdisk, 6, address1 + 50, &address2);
	if (return_val < 0) {
		printf("No recovery ramdisk found in '%s'\nFailed\n", boot_image, address2);
		return -1;
	}
	address2 -= 2;
	printf("Recovery ramdisk found in '%s' at offset 0x%08x\n", boot_image, address2);

	*ramdisk_address = address2;
	return 0;
}

int main(int argc, char** argv) {
	int arg_error = 0, delete_ind = 0, return_val;
	unsigned long address2;
	unsigned char regular_check[8] = "ANDROID!";
	char boot_image[512], backup_image[512];

	printf("-- InjectTWRP Recovery Ramdisk Injection Tool for Samsung devices. --\n");
	printf("--                  by Dees_Troy and Team Win                      --\n");
	printf("--                       http://teamw.in                           --\n");
	printf("--                Bringing some win to Samsung!                    --\n");
	printf("--        This tool comes with no warranties whatsoever!           --\n");
	printf("--        Use at your own risk and always keep a backup!           --\n\n");
	printf("Version 0.1 beta\n\n");

	// Parse the arguments
	if (argc < 2 || argc > 5)
		arg_error = 1;
	else {
		if ((argc == 2 || argc == 3) && (strcmp(argv[1], "-b") == 0 || strcmp(argv[1], "--backup") == 0)) {
			// Backup existing boot image
			printf("Dumping boot image...\n");
#ifdef INJECT_USE_TMP
			system("dump_image boot /tmp/original_boot.img");
			strcpy(boot_image, "/tmp/original_boot.img");

			if (argc == 2)
				strcpy(backup_image, "/tmp/recovery_ramdisk.img");
			else
				strcpy(backup_image, argv[2]);
#else
			system("mount /cache");
			system("dump_image boot /cache/original_boot.img");
			strcpy(boot_image, "/cache/original_boot.img");

			if (argc == 2)
				strcpy(backup_image, "/cache/recovery_ramdisk.img");
			else
				strcpy(backup_image, argv[2]);
#endif

			// Check if this is a normal Android image or a Samsung image
			return_val = scan_file_for_data(boot_image, &regular_check, 8, 0, &address2);
			if (return_val >= 0) {
				printf("This is not a properly formatted Samsung boot image!\nFailed\n");
				return 1;
			}
			
			// Find the ramdisk
			return_val = find_gzip_recovery_ramdisk(boot_image, &address2);
			if (return_val < 0) {
				return 1;
			}

			backup_recovery_ramdisk(boot_image, address2, backup_image);
			return 0;
		} else {
			// Inject new ramdisk
			if (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--dump") == 0) {
				printf("Dumping boot image...\n");
#ifdef INJECT_USE_TMP
				system("dump_image boot /tmp/original_boot.img");
				strcpy(boot_image, "/tmp/original_boot.img");
#else
				system("mount /cache");
				system("dump_image boot /cache/original_boot.img");
				strcpy(boot_image, "/cache/original_boot.img");
#endif
				delete_ind = -1;
			} else
				strcpy(boot_image, argv[1]);

			// Check if this is a normal Android image or a Samsung image
			return_val = scan_file_for_data(boot_image, &regular_check, 8, 0, &address2);
			if (return_val >= 0) {
				printf("This is not a properly formatted Samsung boot image!\nFailed\n");
				return 1;
			}
			
			// Find the ramdisk
			return_val = find_gzip_recovery_ramdisk(boot_image, &address2);
			if (return_val < 0) {
				return 1;
			}

			// Write the new image
			write_new_ramdisk(boot_image, argv[2], address2, argv[3]);

			// Delete --dump image if needed
			if (delete_ind) {
				printf("Deleting dumped boot image from /cache\n");
				system("rm /cache/original_boot.img");
			}

			if (argc == 5 && (strcmp(argv[4], "-f") == 0 || strcmp(argv[4], "--flash") == 0)) {
				char command[512];

				printf("Flashing new image...\n");
				system("erase_image boot"); // Needed because flash_image checks the header and the header sometimes is the same while the ramdisks are different
				strcpy(command, "flash_image boot ");
				strcat(command, argv[3]);
				system(command);
				printf("Flash complete.\n");
			}
			return 0;
		}
	}

	if (arg_error) {
		printf("Invalid arguments supplied.\n");
		printf("Usage:\n\n");
		printf("Backup existing recovery ramdisk (requires dump_image):\n");
		printf("injecttwrp --backup [optionalbackuplocation.img]\n\n");
		printf("Inject new recovery ramdisk:\n");
		printf("injecttwrp originalboot.img ramdisk-recovery.img outputboot.img\n");
		printf("injecttwrp --dump ramdisk-recovery.img outputboot.img [--flash]\n");
		printf("--dump will use dump_image to dump your existing boot image\n");
		printf("--flash will use flash_image to flash the new boot image\n\n");
		printf("NOTE: dump_image, erase_image, and flash_image must already be installed!\n");
		return 0;
	}

	return 0;
}
