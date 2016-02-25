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

#ifndef __PROGRESSTRACKING_HPP
#define __PROGRESSTRACKING_HPP

#include <time.h>

// Progress tracking class for tracking backup progess and updating the progress bar as appropriate
class ProgressTracking
{
public:
	ProgressTracking(const unsigned long long backup_size);

	void SetPartitionSize(const unsigned long long part_size);
	void SetSizeCount(const unsigned long long part_size, unsigned long long f_count);

	void UpdateSize(const unsigned long long size);
	void UpdateSizeCount(const unsigned long long size, const unsigned long long count);

	void DisplayFileCount(const bool display);
	void UpdateDisplayDetails(const bool force);

private:
	unsigned long long total_backup_size;              // Overall size (for the progress bar)

	unsigned long long partition_size;                 // Size of the current partition
	unsigned long long file_count;                     // Count of files for the current partition (tar backup only, not restore)

	unsigned long long current_size;                   // Size of the current partition's already backed up data
	unsigned long long current_count;                  // Count of files that have already been backed up for the current partition

	unsigned long long previous_partitions_size;       // Total data already backed up from previous partitions (for the progress bar)

	bool display_file_count;                           // Inidicates if we will display the file count text
	timespec last_update;                              // Tracks last update of the displayed progress (frequent updates tax the CPU and slow us down)
};

#endif //__PROGRESSTRACKING_HPP
