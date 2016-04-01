/*
        Copyright 2016 bigbiff/Dees_Troy TeamWin
        This file is part of TWRP/TeamWin Recovery Project.

        TWRP is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        TWRP is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

// Progress tracking class for tracking backup progess and updating the progress bar as appropriate


#include "progresstracking.hpp"
#include "twcommon.h"
#ifndef BUILD_TWRPTAR_MAIN
#include "gui/gui.hpp"
#include "data.hpp"
#endif
#include "twrp-functions.hpp"
#include <time.h>

const int32_t update_interval_ms = 200; // Update interval in ms

ProgressTracking::ProgressTracking(const unsigned long long backup_size) {
	total_backup_size = backup_size;
	partition_size = 0;
	file_count = 0;
	current_size = 0;
	current_count = 0;
	previous_partitions_size = 0;
	display_file_count = false;
	clock_gettime(CLOCK_MONOTONIC, &last_update);
}

void ProgressTracking::SetPartitionSize(const unsigned long long part_size) {
	previous_partitions_size += partition_size;
	partition_size = part_size;
	UpdateDisplayDetails(true);
}

void ProgressTracking::SetSizeCount(const unsigned long long part_size, unsigned long long f_count) {
	previous_partitions_size += partition_size;
	partition_size = part_size;
	file_count = f_count;
	display_file_count = (file_count != 0);
	UpdateDisplayDetails(true);
}

void ProgressTracking::UpdateSize(const unsigned long long size) {
	current_size = size;
	UpdateDisplayDetails(false);
}

void ProgressTracking::UpdateSizeCount(const unsigned long long size, const unsigned long long count) {
	current_size = size;
	current_count = count;
	UpdateDisplayDetails(false);
}

void ProgressTracking::DisplayFileCount(const bool display) {
	display_file_count = display;
	UpdateDisplayDetails(true);
}

void ProgressTracking::UpdateDisplayDetails(const bool force) {
#ifndef BUILD_TWRPTAR_MAIN
	if (!force) {
		// Do something to check the time frame and only update periodically to reduce the total number of GUI updates
		timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);

		int32_t diff = TWFunc::timespec_diff_ms(last_update, now);
		if (diff < update_interval_ms)
			return;
	}
	clock_gettime(CLOCK_MONOTONIC, &last_update);
	double display_percent = 0.0, progress_percent;
	string size_prog = gui_lookup("size_progress", "%lluMB of %lluMB, %i%%");
	char size_progress[1024];

	if (total_backup_size != 0) // prevent division by 0
		display_percent = (double)(current_size + previous_partitions_size) / (double)(total_backup_size) * 100;
	sprintf(size_progress, size_prog.c_str(), (current_size + previous_partitions_size) / 1048576, total_backup_size / 1048576, (int)(display_percent));
	DataManager::SetValue("tw_size_progress", size_progress);
	progress_percent = (display_percent / 100);
	DataManager::SetProgress((float)(progress_percent));

	if (!display_file_count || file_count == 0) {
		DataManager::SetValue("tw_file_progress", "");
	} else {
		string file_prog = gui_lookup("file_progress", "%llu of %llu files, %i%%");
		char file_progress[1024];

		display_percent = (double)(current_count) / (double)(file_count) * 100;
		sprintf(file_progress, file_prog.c_str(), current_count, file_count, (int)(display_percent));
		DataManager::SetValue("tw_file_progress", file_progress);
	}
#endif
}
