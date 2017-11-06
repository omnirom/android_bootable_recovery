/*
    Copyright 2017 TeamWin
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <dirent.h>
#include <fnmatch.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <string>
#include <vector>
#include <sstream>

#include "../../partitions.hpp"
#include "../../twrp-functions.hpp"

using namespace std;

extern "C" {
	#include <cutils/properties.h>
}

#include "vold_decrypt.h"


namespace {

/* Timeouts as defined by ServiceManager */

/* The maximum amount of time to wait for a service to start or stop,
 * in micro-seconds (really an approximation) */
#define  SLEEP_MAX_USEC     2000000  /* 2 seconds */
/* The minimal sleeping interval between checking for the service's state
 * when looping for SLEEP_MAX_USEC */
#define  SLEEP_MIN_USEC      200000  /* 200 msec */


/* vold response codes defined in ResponseCode.h */
// 200 series - Requested action has been successfully completed
#define COMMAND_OKAY           200
#define PASSWORD_TYPE_RESULT   213


#define LOGINFO(...)  do { printf(__VA_ARGS__); if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]I:" __VA_ARGS__); fflush(fp_kmsg); } } while (0)
#define LOGKMSG(...)  do { if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]K:" __VA_ARGS__); fflush(fp_kmsg); } } while (0)
#define LOGERROR(...) do { printf(__VA_ARGS__); if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]E:" __VA_ARGS__); fflush(fp_kmsg); } } while (0)

#define PATH_EXISTS(path) (access(path, F_OK) >= 0)

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
typedef struct {
	string service_name;
	string twrp_svc_name;
	bool is_running;
	bool resume;
} AdditionalService;
#endif

FILE *fp_kmsg = NULL;


/* Debugging Functions */
#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG

#define STRACE "/sbin/strace"

bool has_strace = false;
pid_t pid_strace = 0;

void Strace_init_Start(void) {
	has_strace = TWFunc::Path_Exists(STRACE);
	if (!has_strace) {
		LOGINFO("strace binary not found, disabling strace in vold_decrypt!\n");
		return;
	}

	pid_t pid;
	switch(pid = fork())
	{
		case -1:
			LOGKMSG("forking strace_init failed: %d (%s)!\n", errno, strerror(errno));
			return;
		case 0: // child
			execl(STRACE, "strace", "-q", "-tt", "-ff", "-v", "-y", "-s", "1000", "-o", "/tmp/strace_init.log", "-p", "1" , NULL);
			LOGKMSG("strace_init fork failed: %d (%s)!\n", errno, strerror(errno));
			exit(-1);
		default:
			LOGKMSG("Starting strace_init (pid=%d)\n", pid);
			pid_strace = pid;
			return;
	}
}

void Strace_init_Stop(void) {
	if (pid_strace > 0) {
		LOGKMSG("Stopping strace_init (pid=%d)\n", pid_strace);
		int timeout;
		int status;
		pid_t retpid = waitpid(pid_strace, &status, WNOHANG);

		kill(pid_strace, SIGTERM);
		for (timeout = 5; retpid == 0 && timeout; --timeout) {
			sleep(1);
			retpid = waitpid(pid_strace, &status, WNOHANG);
		}
		if (retpid)
			LOGKMSG("strace_init terminated successfully\n");
		else {
			// SIGTERM didn't work, kill it instead
			kill(pid_strace, SIGKILL);
			for (timeout = 5; retpid == 0 && timeout; --timeout) {
				sleep(1);
				retpid = waitpid(pid_strace, &status, WNOHANG);
			}
			if (retpid)
				LOGKMSG("strace_init killed successfully\n");
			else
				LOGKMSG("strace_init took too long to kill, may be a zombie process\n");
		}
	}
}
#endif // TW_CRYPTO_SYSTEM_VOLD_DEBUG

void Output_dmesg_To_RecoveryLog(void) {
	TWFunc::Exec_Cmd(
		"echo \"---- DMESG LOG FOLLOWS ----\";"
		"dmesg | grep 'DECRYPT\\|vold\\|qseecom\\|QSEECOM\\|keymaste\\|keystore\\|cmnlib';"
		"echo \"---- DMESG LOG ENDS ----\""
	);
}


/* Convert a binary key of specified length into an ascii hex string equivalent,
 * without the leading 0x and with null termination
 *
 * Original code from cryptfs.c
 */
string convert_key_to_hex_ascii(const string& master_key) {
	size_t i;
	unsigned char nibble;
	string master_key_ascii = "";

	for (i = 0; i < master_key.size(); ++i) {
		nibble = (master_key[i] >> 4) & 0xf;
		nibble += nibble > 9 ? 0x57 : 0x30;
		master_key_ascii += nibble;

		nibble = master_key[i] & 0xf;
		nibble += nibble > 9 ? 0x57 : 0x30;
		master_key_ascii += nibble;
	}

	return master_key_ascii;
}


/* Helper Functions */
int vrename(const string& oldname, const string& newname, bool verbose = false) {
	const char *old_name = oldname.c_str();
	const char *new_name = newname.c_str();

	if (access(old_name, F_OK < 0))
		return 0;

	if (rename(old_name, new_name) < 0) {
		LOGERROR("Moving %s to %s failed: %d (%s)\n", old_name, new_name, errno, strerror(errno));
		return -1;
	} else if (verbose)
		LOGINFO("Renamed %s to %s\n", old_name, new_name);
	else
		LOGKMSG("Renamed %s to %s\n", old_name, new_name);
	return 0;
}

int vsymlink(const string& oldname, const string& newname, bool verbose = false) {
	const char *old_name = oldname.c_str();
	const char *new_name = newname.c_str();

	if (access(old_name, F_OK < 0))
		return 0;

	if (symlink(old_name, new_name) < 0) {
		LOGERROR("Symlink %s -> %s failed: %d (%s)\n", new_name, old_name, errno, strerror(errno));
		return -1;
	} else if (verbose)
		LOGINFO("Symlinked %s -> %s\n", new_name, old_name);
	else
		LOGKMSG("Symlinked %s -> %s\n", new_name, old_name);
	return 0;
}


/* Properties and Services Functions */
string Wait_For_Property(const string& property_name, int utimeout = SLEEP_MAX_USEC, const string& expected_value = "not_empty") {
	char prop_value[PROPERTY_VALUE_MAX];

	if (expected_value == "not_empty") {
		while (utimeout > 0) {
			property_get(property_name.c_str(), prop_value, "error");
			if (strcmp(prop_value, "error") != 0)
				break;
			LOGKMSG("waiting for %s to get set\n", property_name.c_str());
			utimeout -= SLEEP_MIN_USEC;
			usleep(SLEEP_MIN_USEC);;
		}
	}
	else {
		while (utimeout > 0) {
			property_get(property_name.c_str(), prop_value, "error");
			if (strcmp(prop_value, expected_value.c_str()) == 0)
				break;
			LOGKMSG("waiting for %s to change from '%s' to '%s'\n", property_name.c_str(), prop_value, expected_value.c_str());
			utimeout -= SLEEP_MIN_USEC;
			usleep(SLEEP_MIN_USEC);;
		}
	}
	property_get(property_name.c_str(), prop_value, "error");

	return prop_value;
}

string Get_Service_State(const string& initrc_svc) {
	char prop_value[PROPERTY_VALUE_MAX];
	string init_svc = "init.svc." + initrc_svc;
	property_get(init_svc.c_str(), prop_value, "error");
	return prop_value;
}

bool Service_Exists(const string& initrc_svc) {
	return (Get_Service_State(initrc_svc) != "error");
}

bool Is_Service_Running(const string& initrc_svc) {
	return (Get_Service_State(initrc_svc) == "running");
}

bool Is_Service_Stopped(const string& initrc_svc) {
	return (Get_Service_State(initrc_svc) == "stopped");
}

bool Start_Service(const string& initrc_svc, int utimeout = SLEEP_MAX_USEC) {
	string res = "error";
	string init_svc = "init.svc." + initrc_svc;

	property_set("ctl.start", initrc_svc.c_str());

	res = Wait_For_Property(init_svc, utimeout, "running");

	LOGINFO("Start service %s: %s.\n", initrc_svc.c_str(), res.c_str());

	return (res == "running");
}

bool Stop_Service(const string& initrc_svc, int utimeout = SLEEP_MAX_USEC) {
	string res = "error";

	if (Service_Exists(initrc_svc)) {
		string init_svc = "init.svc." + initrc_svc;
		property_set("ctl.stop", initrc_svc.c_str());
		res = Wait_For_Property(init_svc, utimeout, "stopped");
		LOGINFO("Stop service %s: %s.\n", initrc_svc.c_str(), res.c_str());
	}

	return (res == "stopped");
}

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
vector<AdditionalService> Get_List_Of_Additional_Services (void) {
	vector<AdditionalService> services;

	vector<string> service_names = TWFunc::Split_String(TW_CRYPTO_SYSTEM_VOLD_SERVICES, " ");

	for (size_t i = 0; i < service_names.size(); ++i) {
		AdditionalService svc;
		svc.service_name = service_names[i];
		services.push_back(svc);
	}

	return services;
}
#endif


/* Vendor, Firmware and fstab symlink Functions */
bool Symlink_Vendor_Folder(void) {
	bool is_vendor_symlinked = false;

	if (PartitionManager.Is_Mounted_By_Path("/vendor")) {
		LOGINFO("vendor partition mounted, skipping /vendor substitution\n");
	}
	else if (TWFunc::Path_Exists("/system/vendor")) {
		LOGINFO("Symlinking vendor folder...\n");
		if (!TWFunc::Path_Exists("/vendor") || vrename("/vendor", "/vendor-orig") == 0) {
			TWFunc::Recursive_Mkdir("/vendor/firmware/keymaster");
			vsymlink("/system/vendor/lib64", "/vendor/lib64");
			vsymlink("/system/vendor/lib", "/vendor/lib");
			vsymlink("/system/vendor/bin", "/vendor/bin");
			is_vendor_symlinked = true;
			property_set("vold_decrypt.symlinked_vendor", "1");
		}
	}
	return is_vendor_symlinked;
}

void Restore_Vendor_Folder(void) {
	property_set("vold_decrypt.symlinked_vendor", "0");
	TWFunc::removeDir("/vendor", false);
	vrename("/vendor-orig", "/vendor");
}

bool Symlink_Firmware_Folder(void) {
	bool is_firmware_symlinked = false;

	if (PartitionManager.Is_Mounted_By_Path("/firmware")) {
		LOGINFO("firmware partition mounted, skipping /firmware substitution\n");
	}
	else {
		LOGINFO("Symlinking firmware folder...\n");
		if (!TWFunc::Path_Exists("/firmware") || vrename("/firmware", "/firmware-orig") == 0) {
			TWFunc::Recursive_Mkdir("/firmware/image");
			is_firmware_symlinked = true;
			property_set("vold_decrypt.symlinked_firmware", "1");
		}
	}
	return is_firmware_symlinked;
}

void Restore_Firmware_Folder(void) {
	property_set("vold_decrypt.symlinked_firmware", "0");
	TWFunc::removeDir("/firmware", false);
	vrename("/firmware-orig", "/firmware");
}

int Find_Firmware_Files(const string& Path, vector<string> *FileList) {
	int ret;
	DIR* d;
	struct dirent* de;
	string FileName;

	d = opendir(Path.c_str());
	if (d == NULL) {
		closedir(d);
		return -1;
	}
	while ((de = readdir(d)) != NULL) {
		if (de->d_type == DT_DIR) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;
			FileName = Path + "/" + de->d_name;
			ret = Find_Firmware_Files(FileName, FileList);
			if (ret < 0)
				return -1;
		} else if (de->d_type == DT_REG) {
			if (fnmatch("keymaste*.*", de->d_name, 0) == 0 || fnmatch("cmnlib.*", de->d_name, 0) == 0) {
				FileName = Path + "/" + de->d_name;
				FileList->push_back(FileName);
			}
		}
	}
	closedir(d);
	return 0;
}

void Symlink_Firmware_Files(bool is_vendor_symlinked, bool is_firmware_symlinked) {
	if (!is_vendor_symlinked && !is_firmware_symlinked)
		return;

	LOGINFO("Symlinking firmware files...\n");

	vector<string> FirmwareFiles;
	Find_Firmware_Files("/system", &FirmwareFiles);

	for (size_t i = 0; i < FirmwareFiles.size(); ++i) {
		string base_name = TWFunc::Get_Filename(FirmwareFiles[i]);

		if (is_firmware_symlinked)
			vsymlink(FirmwareFiles[i], "/firmware/image/" + base_name);

		if (is_vendor_symlinked) {
			vsymlink(FirmwareFiles[i], "/vendor/firmware/" + base_name);
			vsymlink(FirmwareFiles[i], "/vendor/firmware/keymaster/" + base_name);
		}
	}
	LOGINFO("%d file(s) symlinked.\n", (int)FirmwareFiles.size());
}

// Android 8.0 fs_mgr checks for "/sbin/recovery", in which case it will use /etc/recovery.fstab -> symlink it temporarily
bool Symlink_Recovery_Fstab(void) {
	bool is_fstab_symlinked = false;

	if (vrename("/etc/recovery.fstab", "/etc/recovery-fstab-orig") == 0) {
		is_fstab_symlinked = true;

		// now attempt to symlink to /fstab.{ro.hardware}, but even if that
		// fails, keep TWRP's fstab hidden since it cannot be parsed by fs_mgr
		char prop_value[PROPERTY_VALUE_MAX];
		property_get("ro.hardware", prop_value, "error");
		if (strcmp(prop_value, "error")) {
			string fstab_device = "/fstab."; fstab_device += prop_value;
			vsymlink(fstab_device, "/etc/recovery.fstab");
		}
	}
	return is_fstab_symlinked;
}

void Restore_Recovery_Fstab(void) {
	unlink("/etc/recovery.fstab");
	vrename("/etc/recovery-fstab-orig", "/etc/recovery.fstab");
}


/* Misc Functions */
void Set_Needed_Properties(void) {
	// vold won't start without ro.storage_structure on Kitkat
	string sdkverstr = TWFunc::System_Property_Get("ro.build.version.sdk");
	int sdkver = 20;
	if (!sdkverstr.empty()) {
		sdkver = atoi(sdkverstr.c_str());
	}
	if (sdkver <= 19) {
		string ro_storage_structure = TWFunc::System_Property_Get("ro.storage_structure");
		if (!ro_storage_structure.empty())
			property_set("ro.storage_structure", ro_storage_structure.c_str());
	}
}


/* vdc Functions */
int Exec_vdc_cryptfs(const string& command, const string& argument, string& result) {
	pid_t pid;
	int status;
	int pipe_fd[2];

	if (pipe(pipe_fd)) {
		LOGINFO("exec_vdc_cryptfs: pipe() error!\n");
		return -1;
	}

	const char *cmd[] = { "/system/bin/vdc", "cryptfs" };
	const char *env[] = { "LD_LIBRARY_PATH=/system/lib64:/system/lib", NULL };

#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG
	string log_name = "/tmp/strace_vdc_" + command;
#endif

	switch(pid = fork())
	{
		case -1:
			LOGINFO("exec_vdc_cryptfs: fork failed: %d (%s)!\n", errno, strerror(errno));
			return -1;

		case 0: // child
			fflush(stdout); fflush(stderr);
			close(pipe_fd[0]);
			dup2(pipe_fd[1], STDOUT_FILENO);
			dup2(pipe_fd[1], STDERR_FILENO);
			close(pipe_fd[1]);

#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG
			if (has_strace) {
				if (argument.empty())
					execl(STRACE, "strace", "-q", "-tt", "-ff", "-v", "-y", "-s", "1000", "-o", log_name.c_str(),
						"-E", env[0], cmd[0], cmd[1], command.c_str(), NULL);
				else
					execl(STRACE, "strace", "-q", "-tt", "-ff", "-v", "-y", "-s", "1000", "-o", log_name.c_str(),
						  "-E", env[0], cmd[0], cmd[1], command.c_str(), argument.c_str(), NULL);
			} else
#endif
			if (argument.empty())
				execle(cmd[0], cmd[0], cmd[1], command.c_str(), NULL, env);
			else
				execle(cmd[0], cmd[0], cmd[1], command.c_str(), argument.c_str(), NULL, env);
			_exit(127);
			break;

		default:
		{
			int timeout = 30*100;
			result.clear();

			close(pipe_fd[1]);

			// Non-blocking read loop with timeout
			int flags = fcntl(pipe_fd[0], F_GETFL, 0);
			fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);

			char buffer[128];
			ssize_t count;
			pid_t retpid = waitpid(pid, &status, WNOHANG);
			while (1) {
				count = read(pipe_fd[0], buffer, sizeof(buffer));
				if (count == -1) {
					if (errno == EINTR)
						continue;
					else if (errno != EAGAIN)
						LOGINFO("exec_vdc_cryptfs: read() error %d (%s)\n!", errno, strerror(errno));
				} else if (count > 0) {
					result.append(buffer, count);
				}

				retpid = waitpid(pid, &status, WNOHANG);
				if (retpid == 0 && --timeout)
					usleep(10000);
				else
					break;
			};

			close(pipe_fd[0]);

			// Error handling
			if (retpid == 0 && timeout == 0) {
				LOGINFO("exec_vdc_cryptfs: took too long, killing process\n");
				kill(pid, SIGKILL);
				for (timeout = 5; retpid == 0 && timeout; --timeout) {
					sleep(1);
					retpid = waitpid(pid, &status, WNOHANG);
				}
				if (retpid)
					LOGINFO("exec_vdc_cryptfs: process killed successfully\n");
				else
					LOGINFO("exec_vdc_cryptfs: process took too long to kill, may be a zombie process\n");
				return VD_ERR_VOLD_OPERATION_TIMEDOUT;
			} else if (retpid > 0) {
				if (WIFSIGNALED(status)) {
					LOGINFO("exec_vdc_cryptfs: process ended with signal: %d\n", WTERMSIG(status)); // Seg fault or some other non-graceful termination
					return -1;
				}
			} else if (retpid < 0) { // no PID returned
				if (errno == ECHILD)
					LOGINFO("exec_vdc_cryptfs: no child process exist\n");
				else {
					LOGINFO("exec_vdc_cryptfs: Unexpected error %d (%s)\n", errno, strerror(errno));
					return -1;
				}
			}
			return 0;
		}
	}
}

int Run_vdc(const string&  Password) {
	int res;
	struct timeval t1, t2;
	string vdc_res;
	int vdc_r1, vdc_r2, vdc_r3;

	LOGINFO("About to run vdc...\n");

	// Wait for vold connection
	gettimeofday(&t1, NULL);
	t2 = t1;
	while ((t2.tv_sec - t1.tv_sec) < 5) {
		vdc_res.clear();
		// cryptfs getpwtype returns: R1=213(PasswordTypeResult)   R2=?   R3="password", "pattern", "pin", "default"
		res = Exec_vdc_cryptfs("getpwtype", "", vdc_res);
		std::replace(vdc_res.begin(), vdc_res.end(), '\n', ' '); // remove newline(s)
		vdc_r1 = vdc_r2 = vdc_r3 = -1;
		sscanf(vdc_res.c_str(), "%d", &vdc_r1);
		if (vdc_r1 == PASSWORD_TYPE_RESULT) {
			char str_res[sizeof(int) + 1];
			snprintf(str_res, sizeof(str_res), "%d", res);
			vdc_res += "ret=";
			vdc_res += str_res;
			res = 0;
			break;
		}
		LOGKMSG("Connect returned '%s' vdc_r1=%d\n", vdc_res.c_str(), vdc_r1);
		LOGINFO("Retrying connection to vold\n");
		usleep(SLEEP_MIN_USEC); // vdc usually usleep(10000), but that causes too many unnecessary attempts
		gettimeofday(&t2, NULL);
	}

	if (res == 0 && (t2.tv_sec - t1.tv_sec) < 5)
		LOGINFO("Connected to vold (%s)\n", vdc_res.c_str());
	else if (res == VD_ERR_VOLD_OPERATION_TIMEDOUT)
		return VD_ERR_VOLD_OPERATION_TIMEDOUT; // should never happen for getpwtype
	else if (res)
		return VD_ERR_FORK_EXECL_ERROR;
	else if (vdc_r1 != -1)
		return VD_ERR_VOLD_UNEXPECTED_RESPONSE;
	else
		return VD_ERR_VDC_FAILED_TO_CONNECT;


	// Input password from GUI, or default password
	vdc_res.clear();

	res = Exec_vdc_cryptfs("checkpw", Password, vdc_res);
	if (res == VD_ERR_VOLD_OPERATION_TIMEDOUT)
		return VD_ERR_VOLD_OPERATION_TIMEDOUT;
	else if (res)
		return VD_ERR_FORK_EXECL_ERROR;

	std::replace(vdc_res.begin(), vdc_res.end(), '\n', ' '); // remove newline(s)
	LOGINFO("vdc cryptfs result (passwd): %s (ret=%d)\n", vdc_res.c_str(), res);
	vdc_r1 = vdc_r2 = vdc_r3 = -1;
	sscanf(vdc_res.c_str(), "%d %d %d", &vdc_r1, &vdc_r2, &vdc_r3);
	/*
	if (res == 0 && vdc_r1 != COMMAND_OKAY)
		return VD_ERR_VOLD_UNEXPECTED_RESPONSE;
	*/

	if (vdc_r3 != 0) {
		// try falling back to Lollipop hex passwords
		string hexPassword = convert_key_to_hex_ascii(Password);
		vdc_res.clear();

		res = Exec_vdc_cryptfs("checkpw", hexPassword, vdc_res);
		if (res == VD_ERR_VOLD_OPERATION_TIMEDOUT)
			return VD_ERR_VOLD_OPERATION_TIMEDOUT;
		else if (res)
			return VD_ERR_FORK_EXECL_ERROR;

		std::replace(vdc_res.begin(), vdc_res.end(), '\n', ' '); // remove newline(s)
		LOGINFO("vdc cryptfs result (hex_pw): %s (ret=%d)\n", vdc_res.c_str(), res);
		vdc_r1 = vdc_r2 = vdc_r3 = -1;
		sscanf(vdc_res.c_str(), "%d %d %d", &vdc_r1, &vdc_r2, &vdc_r3);
		/*
		if (res == 0 && vdc_r1 != COMMAND_OKAY)
			return VD_ERR_VOLD_UNEXPECTED_RESPONSE;
		*/
	}

	// vdc's return value is dependant upon source origin, it will either
	// return 0 or vdc_r1, so disregard and focus on decryption instead
	if (vdc_r3 == 0) {
		// Decryption successful wait for crypto blk dev
		Wait_For_Property("ro.crypto.fs_crypto_blkdev");
		res = VD_SUCCESS;
	} else if (vdc_r1 != COMMAND_OKAY) {
		res = VD_ERR_VOLD_UNEXPECTED_RESPONSE;
	} else {
		res = VD_ERR_DECRYPTION_FAILED;
	}

	return res;
}

int Vold_Decrypt_Core(const string& Password)
{
	int res;
	bool output_dmesg_to_log = false;
	bool is_vendor_symlinked = false;
	bool is_firmware_symlinked = false;
	bool is_fstab_symlinked = false;
	bool is_vold_running = false;

	if (Password.empty()) {
		LOGINFO("vold_decrypt: password is empty!\n");
		return VD_ERR_PASSWORD_EMPTY;
	}

	// Mount system and check for vold and vdc
	if (!PartitionManager.Mount_By_Path("/system", true)) {
		return VD_ERR_UNABLE_TO_MOUNT_SYSTEM;
	} else if (!TWFunc::Path_Exists("/system/bin/vold")) {
		LOGINFO("ERROR: /system/bin/vold not found, aborting.\n");
		return VD_ERR_MISSING_VOLD;
	} else if (!TWFunc::Path_Exists("/system/bin/vdc")) {
		LOGINFO("ERROR: /system/bin/vdc not found, aborting.\n");
		return VD_ERR_MISSING_VDC;
	}

	fp_kmsg = fopen("/dev/kmsg", "a");

	LOGINFO("TW_CRYPTO_USE_SYSTEM_VOLD := true\n");
	LOGINFO("Attempting to use system's vold for decryption...\n");

#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG
	Strace_init_Start();
#endif

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
	vector<AdditionalService> Services = Get_List_Of_Additional_Services();

	// Check if TWRP is running any of the services
	for (size_t i = 0; i < Services.size(); ++i) {
		if (Service_Exists(Services[i].service_name))
			Services[i].twrp_svc_name = Services[i].service_name;
		else if (Service_Exists("sbin" + Services[i].service_name))
			Services[i].twrp_svc_name = "sbin" + Services[i].service_name;
		else
			Services[i].twrp_svc_name.clear();

		if (!Services[i].twrp_svc_name.empty() && !Is_Service_Stopped(Services[i].twrp_svc_name)) {
			Services[i].resume = true;
			Stop_Service(Services[i].twrp_svc_name);
		} else
			Services[i].resume = false;

		// vold_decrypt system services have to be named sys_{service} in the .rc files
		Services[i].service_name = "sys_" + Services[i].service_name;
	}
#endif

	LOGINFO("Setting up folders and permissions...\n");
	is_fstab_symlinked = Symlink_Recovery_Fstab();
	is_vendor_symlinked = Symlink_Vendor_Folder();
	is_firmware_symlinked = Symlink_Firmware_Folder();
	Symlink_Firmware_Files(is_vendor_symlinked, is_firmware_symlinked);

	Set_Needed_Properties();

	// Start services needed for vold decrypt
	LOGINFO("Starting services...\n");
#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
	for (size_t i = 0; i < Services.size(); ++i) {
		Services[i].is_running = Start_Service(Services[i].service_name);
	}
#endif
	is_vold_running = Start_Service("sys_vold");

	if (is_vold_running) {
#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
		for (size_t i = 0; i < Services.size(); ++i) {
			if (!Is_Service_Running(Services[i].service_name) && Services[i].resume) {
				// if system_service has died restart the twrp_service
				LOGINFO("%s is not running, resuming %s!\n", Services[i].service_name.c_str(), Services[i].twrp_svc_name.c_str());
				Start_Service(Services[i].twrp_svc_name);
			}
		}
#endif
		res = Run_vdc(Password);

		if (res != 0) {
			LOGINFO("Decryption failed\n");
			output_dmesg_to_log = true;
		}
	} else {
		LOGINFO("Failed to start vold\n");
		output_dmesg_to_log = true;
		res = VD_ERR_VOLD_FAILED_TO_START;
	}

	// Stop services needed for vold decrypt so /system can be unmounted
	LOGINFO("Stopping services...\n");
	Stop_Service("sys_vold");
#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
	for (size_t i = 0; i < Services.size(); ++i) {
		if (!Is_Service_Running(Services[i].service_name) && Services[i].resume)
			Stop_Service(Services[i].twrp_svc_name);
		else
			Stop_Service(Services[i].service_name);
	}
#endif

	if (is_firmware_symlinked)
		Restore_Firmware_Folder();
	if (is_vendor_symlinked)
		Restore_Vendor_Folder();
	if (is_fstab_symlinked)
		Restore_Recovery_Fstab();

	if (!PartitionManager.UnMount_By_Path("/system", true)) {
		// PartitionManager failed to unmount /system, this should not happen,
		// but in case it does, do a lazy unmount
		LOGINFO("WARNING: system could not be unmounted normally!\n");
		umount2("/system", MNT_DETACH);
	}

	LOGINFO("Finished.\n");

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
	// Restart previously running services
	for (size_t i = 0; i < Services.size(); ++i) {
		if (Services[i].resume)
			Start_Service(Services[i].twrp_svc_name);
	}
#endif

#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG
	Strace_init_Stop();
	output_dmesg_to_log = true;
#endif

	// Finish up and exit
	if (fp_kmsg) {
		fflush(fp_kmsg);
		fclose(fp_kmsg);
	}

	if (output_dmesg_to_log)
		Output_dmesg_To_RecoveryLog();

	return res;
}

} // namespace


/* Main function separated from core in case we want to return error info */
int vold_decrypt(const string& Password) {
	return Vold_Decrypt_Core(Password);
}
