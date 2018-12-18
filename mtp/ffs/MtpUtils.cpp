/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "MtpUtils"

#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "MtpUtils.h"

using namespace std;

constexpr unsigned long FILE_COPY_SIZE = 262144;

static void access_ok(const char *path) {
	if (access(path, F_OK) == -1) {
		// Ignore. Failure could be common in cases of delete where
		// the metadata was updated through other paths.
	}
}

/*
DateTime strings follow a compatible subset of the definition found in ISO 8601, and
take the form of a Unicode string formatted as: "YYYYMMDDThhmmss.s". In this
representation, YYYY shall be replaced by the year, MM replaced by the month (01-12),
DD replaced by the day (01-31), T is a constant character 'T' delimiting time from date,
hh is replaced by the hour (00-23), mm is replaced by the minute (00-59), and ss by the
second (00-59). The ".s" is optional, and represents tenths of a second.
This is followed by a UTC offset given as "[+-]zzzz" or the literal "Z", meaning UTC.
*/

bool parseDateTime(const char* dateTime, time_t& outSeconds) {
	int year, month, day, hour, minute, second;
	if (sscanf(dateTime, "%04d%02d%02dT%02d%02d%02d",
			   &year, &month, &day, &hour, &minute, &second) != 6)
		return false;

	// skip optional tenth of second
	const char* tail = dateTime + 15;
	if (tail[0] == '.' && tail[1]) tail += 2;

	// FIXME: "Z" means UTC, but non-"Z" doesn't mean local time.
	// It might be that you're in Asia/Seoul on vacation and your Android
	// device has noticed this via the network, but your camera was set to
	// America/Los_Angeles once when you bought it and doesn't know where
	// it is right now, so the camera says "20160106T081700-0800" but we
	// just ignore the "-0800" and assume local time which is actually "+0900".
	// I think to support this (without switching to Java or using icu4c)
	// you'd want to always use timegm(3) and then manually add/subtract
	// the UTC offset parsed from the string (taking care of wrapping).
	// mktime(3) ignores the tm_gmtoff field, so you can't let it do the work.
	bool useUTC = (tail[0] == 'Z');

	struct tm tm = {};
	tm.tm_sec = second;
	tm.tm_min = minute;
	tm.tm_hour = hour;
	tm.tm_mday = day;
	tm.tm_mon = month - 1;	// mktime uses months in 0 - 11 range
	tm.tm_year = year - 1900;
	tm.tm_isdst = -1;
	outSeconds = useUTC ? timegm(&tm) : mktime(&tm);

	return true;
}

void formatDateTime(time_t seconds, char* buffer, int bufferLength) {
	struct tm tm;

	localtime_r(&seconds, &tm);
	snprintf(buffer, bufferLength, "%04d%02d%02dT%02d%02d%02d",
		tm.tm_year + 1900,
		tm.tm_mon + 1, // localtime_r uses months in 0 - 11 range
		tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

int makeFolder(const char *path) {
	mode_t mask = umask(0);
	int ret = mkdir((const char *)path, DIR_PERM);
	umask(mask);
	if (ret && ret != -EEXIST) {
		PLOG(ERROR) << "Failed to create folder " << path;
		ret = -1;
	} else {
		chown((const char *)path, getuid(), FILE_GROUP);
	}
	access_ok(path);
	return ret;
}

/**
 * Copies target path and all children to destination path.
 *
 * Returns 0 on success or a negative value indicating number of failures
 */
int copyRecursive(const char *fromPath, const char *toPath) {
	int ret = 0;
	string fromPathStr(fromPath);
	string toPathStr(toPath);

	DIR* dir = opendir(fromPath);
	if (!dir) {
		PLOG(ERROR) << "opendir " << fromPath << " failed";
		return -1;
	}
	if (fromPathStr[fromPathStr.size()-1] != '/')
		fromPathStr += '/';
	if (toPathStr[toPathStr.size()-1] != '/')
		toPathStr += '/';

	struct dirent* entry;
	while ((entry = readdir(dir))) {
		const char* name = entry->d_name;

		// ignore "." and ".."
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
			continue;
		}
		string oldFile = fromPathStr + name;
		string newFile = toPathStr + name;

		if (entry->d_type == DT_DIR) {
			ret += makeFolder(newFile.c_str());
			ret += copyRecursive(oldFile.c_str(), newFile.c_str());
		} else {
			ret += copyFile(oldFile.c_str(), newFile.c_str());
		}
	}
	return ret;
}

int copyFile(const char *fromPath, const char *toPath) {
	auto start = std::chrono::steady_clock::now();

	android::base::unique_fd fromFd(open(fromPath, O_RDONLY));
	if (fromFd == -1) {
		PLOG(ERROR) << "Failed to open copy from " << fromPath;
		return -1;
	}
	android::base::unique_fd toFd(open(toPath, O_CREAT | O_WRONLY, FILE_PERM));
	if (toFd == -1) {
		PLOG(ERROR) << "Failed to open copy to " << toPath;
		return -1;
	}
	off_t offset = 0;

	struct stat sstat = {};
	if (stat(fromPath, &sstat) == -1)
		return -1;

	off_t length = sstat.st_size;
	int ret = 0;

	while (offset < length) {
		ssize_t transfer_length = std::min(length - offset, (off_t) FILE_COPY_SIZE);
		ret = sendfile(toFd, fromFd, &offset, transfer_length);
		if (ret != transfer_length) {
			ret = -1;
			PLOG(ERROR) << "Copying failed!";
			break;
		}
	}
	auto end = std::chrono::steady_clock::now();
	std::chrono::duration<double> diff = end - start;
	LOG(DEBUG) << "Copied a file with MTP. Time: " << diff.count() << " s, Size: " << length <<
		", Rate: " << ((double) length) / diff.count() << " bytes/s";
	chown(toPath, getuid(), FILE_GROUP);
	access_ok(toPath);
	return ret == -1 ? -1 : 0;
}

void deleteRecursive(const char* path) {
	string pathStr(path);
	if (pathStr[pathStr.size()-1] != '/') {
		pathStr += '/';
	}

	DIR* dir = opendir(path);
	if (!dir) {
		PLOG(ERROR) << "opendir " << path << " failed";
		return;
	}

	struct dirent* entry;
	while ((entry = readdir(dir))) {
		const char* name = entry->d_name;

		// ignore "." and ".."
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
			continue;
		}
		string childPath = pathStr + name;
		int success;
		if (entry->d_type == DT_DIR) {
			deleteRecursive(childPath.c_str());
			success = rmdir(childPath.c_str());
		} else {
			success = unlink(childPath.c_str());
		}
		access_ok(childPath.c_str());
		if (success == -1)
			PLOG(ERROR) << "Deleting path " << childPath << " failed";
	}
	closedir(dir);
}

bool deletePath(const char* path) {
	struct stat statbuf;
	int success;
	if (stat(path, &statbuf) == 0) {
		if (S_ISDIR(statbuf.st_mode)) {
			// rmdir will fail if the directory is non empty, so
			// there is no need to keep errors from deleteRecursive
			deleteRecursive(path);
			success = rmdir(path);
		} else {
			success = unlink(path);
		}
	} else {
		PLOG(ERROR) << "deletePath stat failed for " << path;
		return false;
	}
	if (success == -1)
		PLOG(ERROR) << "Deleting path " << path << " failed";
	access_ok(path);
	return success == 0;
}

int renameTo(const char *oldPath, const char *newPath) {
	int ret = rename(oldPath, newPath);
	access_ok(oldPath);
	access_ok(newPath);
	return ret;
}

// Calls access(2) on the path to update underlying filesystems,
// then closes the fd.
void closeObjFd(int fd, const char *path) {
	close(fd);
	access_ok(path);
}
