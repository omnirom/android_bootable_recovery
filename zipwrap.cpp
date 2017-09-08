/*
 * Copyright (C) 2017 bigbiff/Dees_Troy TeamWin
 * This file is part of TWRP/TeamWin Recovery Project.
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

#include "zipwrap.hpp"
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef USE_MINZIP
#include "minzip/Zip.h"
#include "minzip/SysUtil.h"
#else
#include <ziparchive/zip_archive.h>
#include "otautil/ZipUtil.h"
#include "otautil/SysUtil.h"
#endif

ZipWrap::ZipWrap() {
	zip_open = false;
}

ZipWrap::~ZipWrap() {
	if (zip_open)
		Close();
}

bool ZipWrap::Open(const char* file, MemMapping* map) {
	if (zip_open) {
		printf("ZipWrap '%s' is already open\n", zip_file.c_str());
		return true;
	}
	zip_file = file;
#ifdef USE_MINZIP
	if (mzOpenZipArchive(map->addr, map->length, &Zip) != 0)
		return false;
#else
	if (OpenArchiveFromMemory(map->addr, map->length, file, &Zip) != 0)
		return false;
#endif
	zip_open = true;
	return true;
}

void ZipWrap::Close() {
	if (zip_open)
#ifdef USE_MINZIP
		mzCloseZipArchive(&Zip);
#else
		CloseArchive(Zip);
#endif
	zip_open = false;
}

bool ZipWrap::EntryExists(const string& filename) {
#ifdef USE_MINZIP
	const ZipEntry* file_location = mzFindZipEntry(&Zip, filename.c_str());
	if (file_location != NULL)
		return true;
	return false;
#else
	ZipString zip_string(filename.c_str());
	ZipEntry file_entry;

	if (FindEntry(Zip, zip_string, &file_entry) != 0)
		return false;
	return true;
#endif
}

bool ZipWrap::ExtractEntry(const string& source_file, const string& target_file, mode_t mode) {
	if (access(target_file.c_str(), F_OK) == 0 && unlink(target_file.c_str()) != 0)
		printf("Unable to unlink '%s': %s\n", target_file.c_str(), strerror(errno));
	
	int fd = creat(target_file.c_str(), mode);
	if (fd < 0) {
		printf("Failed to create '%s'\n", target_file.c_str());
		return false;
	}

#ifdef USE_MINZIP
	const ZipEntry* file_entry = mzFindZipEntry(&Zip, source_file.c_str());
	if (file_entry == NULL) {
		printf("'%s' does not exist in zip '%s'\n", source_file.c_str(), zip_file.c_str());
		return false;
	}
	int ret_val = mzExtractZipEntryToFile(&Zip, file_entry, fd);
	close(fd);

	if (!ret_val) {
		printf("Could not extract '%s'\n", target_file.c_str());
		return false;
	}
#else
	ZipString zip_string(source_file.c_str());
	ZipEntry file_entry;

	if (FindEntry(Zip, zip_string, &file_entry) != 0)
		return false;
	int32_t ret_val = ExtractEntryToFile(Zip, &file_entry, fd);
	close(fd);

	if (ret_val != 0) {
		printf("Could not extract '%s'\n", target_file.c_str());
		return false;
	}
#endif
	return true;
}

bool ZipWrap::ExtractRecursive(const string& source_dir, const string& target_dir) {
	struct utimbuf timestamp = { 1217592000, 1217592000 };  // 8/1/2008 default
#ifdef USE_MINZIP
	return mzExtractRecursive(&Zip, source_dir.c_str(), target_dir.c_str(), &timestamp, NULL, NULL, NULL);
#else
	return ExtractPackageRecursive(Zip, source_dir, target_dir, &timestamp, NULL);
#endif
}

long ZipWrap::GetUncompressedSize(const string& filename) {
#ifdef USE_MINZIP
	const ZipEntry* file_entry = mzFindZipEntry(&Zip, filename.c_str());
	if (file_entry == NULL) {
		printf("'%s' does not exist in zip '%s'\n", filename.c_str(), zip_file.c_str());
		return 0;
	}
	return file_entry->uncompLen;
#else
	ZipString zip_string(filename.c_str());
	ZipEntry file_entry;

	if (FindEntry(Zip, zip_string, &file_entry) != 0)
		return 0;
	return file_entry.uncompressed_length;
#endif
}

bool ZipWrap::ExtractToBuffer(const string& filename, uint8_t* buffer) {
#ifdef USE_MINZIP
	const ZipEntry* file_entry = mzFindZipEntry(&Zip, filename.c_str());
	if (file_entry == NULL) {
		printf("'%s' does not exist in zip '%s'\n", filename.c_str(), zip_file.c_str());
		return false;
	}
	if (!mzExtractZipEntryToBuffer(&Zip, file_entry, buffer)) {
		printf("Failed to read '%s'\n", filename.c_str());
		return false;
	}
#else
	ZipString zip_string(filename.c_str());
	ZipEntry file_entry;

	if (FindEntry(Zip, zip_string, &file_entry) != 0)
		return false;
	if (ExtractToMemory(Zip, &file_entry, buffer, file_entry.uncompressed_length) != 0) {
		printf("Failed to read '%s'\n", filename.c_str());
		return false;
	}
#endif
	return true;
}

#ifdef USE_MINZIP
loff_t ZipWrap::GetEntryOffset(const string& filename) {
	const ZipEntry* file_entry = mzFindZipEntry(&Zip, filename.c_str());
	if (file_entry == NULL) {
		printf("'%s' does not exist in zip '%s'\n", filename.c_str(), zip_file.c_str());
		return 0;
	}
	return mzGetZipEntryOffset(file_entry);
}
#else
off64_t ZipWrap::GetEntryOffset(const string& filename) {
	ZipString zip_string(filename.c_str());
	ZipEntry file_entry;

	if (FindEntry(Zip, zip_string, &file_entry) != 0) {
		printf("'%s' does not exist in zip '%s'\n", filename.c_str(), zip_file.c_str());
		return 0;
	}
	return file_entry.offset;
}

ZipArchiveHandle ZipWrap::GetZipArchiveHandle() {
	return Zip;
}
#endif
