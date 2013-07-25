/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bootloader.h"
#include "common.h"
extern "C" {
#include "mtdutils/mtdutils.h"
}

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "partitions.hpp"

static int get_bootloader_message_mtd(struct bootloader_message *out, const TWPartition* Partition);
static int set_bootloader_message_mtd(const struct bootloader_message *in, const TWPartition* Partition);
static int get_bootloader_message_block(struct bootloader_message *out, const TWPartition* Partition);
static int set_bootloader_message_block(const struct bootloader_message *in, const TWPartition* Partition);

int get_bootloader_message(struct bootloader_message *out) {
	TWPartition* Part = PartitionManager.Find_Partition_By_Path("/misc");
	if (Part == NULL) {
	  //LOGE("Cannot load volume /misc!\n");
	  return -1;
	}

	if (Part->Current_File_System == "mtd")
		return get_bootloader_message_mtd(out, Part);
	else if (Part->Current_File_System == "emmc")
		return get_bootloader_message_block(out, Part);

	LOGE("unknown misc partition fs_type \"%s\"\n", Part->Current_File_System.c_str());
	return -1;
}

int set_bootloader_message(const struct bootloader_message *in) {
	TWPartition* Part = PartitionManager.Find_Partition_By_Path("/misc");
	if (Part == NULL) {
	  //LOGE("Cannot load volume /misc!\n");
	  return -1;
	}

	if (Part->Current_File_System == "mtd")
		return set_bootloader_message_mtd(in, Part);
	else if (Part->Current_File_System == "emmc")
		return set_bootloader_message_block(in, Part);

	LOGE("unknown misc partition fs_type \"%s\"\n", Part->Current_File_System.c_str());
	return -1;
}

// ------------------------------
// for misc partitions on MTD
// ------------------------------

static const int MISC_PAGES = 3;         // number of pages to save
static const int MISC_COMMAND_PAGE = 1;  // bootloader command is this page

static int get_bootloader_message_mtd(struct bootloader_message *out,
                                      const TWPartition* Partition) {
	size_t write_size;
	mtd_scan_partitions();
	const MtdPartition *part = mtd_find_partition_by_name(Partition->MTD_Name.c_str());
	if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
		LOGE("Can't find %s\n", Partition->MTD_Name.c_str());
		return -1;
	}

	MtdReadContext *read = mtd_read_partition(part);
	if (read == NULL) {
		LOGE("Can't open %s\n(%s)\n", Partition->MTD_Name.c_str(), strerror(errno));
		return -1;
	}

	const ssize_t size = write_size * MISC_PAGES;
	char data[size];
	ssize_t r = mtd_read_data(read, data, size);
	if (r != size) LOGE("Can't read %s\n(%s)\n", Partition->MTD_Name.c_str(), strerror(errno));
	mtd_read_close(read);
	if (r != size) return -1;

	memcpy(out, &data[write_size * MISC_COMMAND_PAGE], sizeof(*out));
	return 0;
}
static int set_bootloader_message_mtd(const struct bootloader_message *in,
                                      const TWPartition* Partition) {
	size_t write_size;
	mtd_scan_partitions();
	const MtdPartition *part = mtd_find_partition_by_name(Partition->MTD_Name.c_str());
	if (part == NULL || mtd_partition_info(part, NULL, NULL, &write_size)) {
		LOGE("Can't find %s\n", Partition->MTD_Name.c_str());
		return -1;
	}

	MtdReadContext *read = mtd_read_partition(part);
	if (read == NULL) {
		LOGE("Can't open %s\n(%s)\n", Partition->MTD_Name.c_str(), strerror(errno));
		return -1;
	}

	ssize_t size = write_size * MISC_PAGES;
	char data[size];
	ssize_t r = mtd_read_data(read, data, size);
	if (r != size) LOGE("Can't read %s\n(%s)\n", Partition->MTD_Name.c_str(), strerror(errno));
	mtd_read_close(read);
	if (r != size) return -1;

	memcpy(&data[write_size * MISC_COMMAND_PAGE], in, sizeof(*in));

	MtdWriteContext *write = mtd_write_partition(part);
	if (write == NULL) {
		LOGE("Can't open %s\n(%s)\n", Partition->MTD_Name.c_str(), strerror(errno));
		return -1;
	}
	if (mtd_write_data(write, data, size) != size) {
		LOGE("Can't write %s\n(%s)\n", Partition->MTD_Name.c_str(), strerror(errno));
		mtd_write_close(write);
		return -1;
	}
	if (mtd_write_close(write)) {
		LOGE("Can't finish %s\n(%s)\n", Partition->MTD_Name.c_str(), strerror(errno));
		return -1;
	}

	LOGI("Set boot command \"%s\"\n", in->command[0] != 255 ? in->command : "");
	return 0;
}

// ------------------------------------
// for misc partitions on block devices
// ------------------------------------

static void wait_for_device(const char* fn) {
	int tries = 0;
	int ret;
	struct stat buf;

	do {
		++tries;
		ret = stat(fn, &buf);
		if (ret) {
			printf("stat %s try %d: %s\n", fn, tries, strerror(errno));
			sleep(1);
		}
	} while (ret && tries < 10);

	if (ret)
		printf("failed to stat %s\n", fn);
}

static int get_bootloader_message_block(struct bootloader_message *out,
                                        const TWPartition* Partition) {
	wait_for_device(Partition->Actual_Block_Device.c_str());

	FILE* f = fopen(Partition->Actual_Block_Device.c_str(), "rb");
	if (f == NULL) {
		LOGE("Can't open %s\n(%s)\n", Partition->Actual_Block_Device.c_str(), strerror(errno));
		return -1;
	}

	struct bootloader_message temp;
	int count = fread(&temp, sizeof(temp), 1, f);
	if (count != 1) {
		LOGE("Failed reading %s\n(%s)\n", Partition->Actual_Block_Device.c_str(), strerror(errno));
		return -1;
	}

	if (fclose(f) != 0) {
		LOGE("Failed closing %s\n(%s)\n", Partition->Actual_Block_Device.c_str(), strerror(errno));
		return -1;
	}

	memcpy(out, &temp, sizeof(temp));
	return 0;
}

static int set_bootloader_message_block(const struct bootloader_message *in,
                                        const TWPartition* Partition) {
	wait_for_device(Partition->Actual_Block_Device.c_str());

	FILE* f = fopen(Partition->Actual_Block_Device.c_str(), "wb");
	if (f == NULL) {
		LOGE("Can't open %s\n(%s)\n", Partition->Actual_Block_Device.c_str(), strerror(errno));
		return -1;
	}

	int count = fwrite(in, sizeof(*in), 1, f);
	if (count != 1) {
		LOGE("Failed writing %s\n(%s)\n", Partition->Actual_Block_Device.c_str(), strerror(errno));
		return -1;
	}

	if (fclose(f) != 0) {
		LOGE("Failed closing %s\n(%s)\n", Partition->Actual_Block_Device.c_str(), strerror(errno));
		return -1;
	}
	return 0;
}
