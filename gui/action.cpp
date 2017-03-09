/*update
	Copyright 2013 bigbiff/Dees_Troy TeamWin
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <dirent.h>
#include <private/android_filesystem_config.h>

#include <string>
#include <sstream>
#include "../partitions.hpp"
#include "../twrp-functions.hpp"
#include "../openrecoveryscript.hpp"

#include "../adb_install.h"
#include "../fuse_sideload.h"
#include "blanktimer.hpp"

extern "C" {
#include "../twcommon.h"
#include "../variables.h"
#include "../twinstall.h"
#include "cutils/properties.h"
#include "../adb_install.h"
};
#include "../set_metadata.h"
#include "../minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../tw_atomic.hpp"

#ifdef TARGET_RECOVERY_IS_MULTIROM
#include "../multirom/multirom.h"
#include "../multirom/mrominstaller.h"
#endif //TARGET_RECOVERY_IS_MULTIROM

GUIAction::mapFunc GUIAction::mf;
std::set<string> GUIAction::setActionsRunningInCallerThread;
static string zip_queue[10];
static int zip_queue_index;
pid_t sideload_child_pid;

static void *ActionThread_work_wrapper(void *data);

class ActionThread
{
public:
	ActionThread();
	~ActionThread();

	void threadActions(GUIAction *act);
	void run(void *data);
private:
	friend void *ActionThread_work_wrapper(void*);
	struct ThreadData
	{
		ActionThread *this_;
		GUIAction *act;
		ThreadData(ActionThread *this_, GUIAction *act) : this_(this_), act(act) {}
	};

	pthread_t m_thread;
	bool m_thread_running;
	pthread_mutex_t m_act_lock;
};

static ActionThread action_thread;	// for all kinds of longer running actions
static ActionThread cancel_thread;	// for longer running "cancel" actions

static void *ActionThread_work_wrapper(void *data)
{
	static_cast<ActionThread::ThreadData*>(data)->this_->run(data);
	return NULL;
}

ActionThread::ActionThread()
{
	m_thread_running = false;
	pthread_mutex_init(&m_act_lock, NULL);
}

ActionThread::~ActionThread()
{
	pthread_mutex_lock(&m_act_lock);
	if (m_thread_running) {
		pthread_mutex_unlock(&m_act_lock);
		pthread_join(m_thread, NULL);
	} else {
		pthread_mutex_unlock(&m_act_lock);
	}
	pthread_mutex_destroy(&m_act_lock);
}

void ActionThread::threadActions(GUIAction *act)
{
	pthread_mutex_lock(&m_act_lock);
	if (m_thread_running) {
		pthread_mutex_unlock(&m_act_lock);
		LOGERR("Another threaded action is already running -- not running %u actions starting with '%s'\n",
				act->mActions.size(), act->mActions[0].mFunction.c_str());
	} else {
		m_thread_running = true;
		pthread_mutex_unlock(&m_act_lock);
		ThreadData *d = new ThreadData(this, act);
		pthread_create(&m_thread, NULL, &ActionThread_work_wrapper, d);
	}
}

void ActionThread::run(void *data)
{
	ThreadData *d = (ThreadData*)data;
	GUIAction* act = d->act;

	std::vector<GUIAction::Action>::iterator it;
	for (it = act->mActions.begin(); it != act->mActions.end(); ++it)
		act->doAction(*it);

	pthread_mutex_lock(&m_act_lock);
	m_thread_running = false;
	pthread_mutex_unlock(&m_act_lock);
	delete d;
}

GUIAction::GUIAction(xml_node<>* node)
	: GUIObject(node)
{
	xml_node<>* child;
	xml_node<>* actions;
	xml_attribute<>* attr;

	if (!node)  return;

	if (mf.empty()) {
#define ADD_ACTION(n) mf[#n] = &GUIAction::n
#define ADD_ACTION_EX(name, func) mf[name] = &GUIAction::func
		// These actions will be run in the caller's thread
		ADD_ACTION(reboot);
		ADD_ACTION(home);
		ADD_ACTION(key);
		ADD_ACTION(page);
		ADD_ACTION(reload);
		ADD_ACTION(readBackup);
		ADD_ACTION(set);
		ADD_ACTION(clear);
		ADD_ACTION(mount);
		ADD_ACTION(unmount);
		ADD_ACTION_EX("umount", unmount);
		ADD_ACTION(restoredefaultsettings);
		ADD_ACTION(copylog);
		ADD_ACTION(compute);
		ADD_ACTION_EX("addsubtract", compute);
		ADD_ACTION(setguitimezone);
		ADD_ACTION(overlay);
		ADD_ACTION(queuezip);
		ADD_ACTION(cancelzip);
		ADD_ACTION(queueclear);
		ADD_ACTION(sleep);
		ADD_ACTION(sleepcounter);
		ADD_ACTION(appenddatetobackupname);
		ADD_ACTION(generatebackupname);
		ADD_ACTION(checkpartitionlist);
		ADD_ACTION(getpartitiondetails);
		ADD_ACTION(screenshot);
		ADD_ACTION(setbrightness);
		ADD_ACTION(fileexists);
		ADD_ACTION(killterminal);
		ADD_ACTION(checkbackupname);
		ADD_ACTION(adbsideloadcancel);
		ADD_ACTION(fixsu);
		ADD_ACTION(startmtp);
		ADD_ACTION(stopmtp);
		ADD_ACTION(cancelbackup);
		ADD_ACTION(checkpartitionlifetimewrites);
		ADD_ACTION(mountsystemtoggle);
#ifdef TARGET_RECOVERY_IS_MULTIROM
		ADD_ACTION(rotation);
		ADD_ACTION(multirom);
		ADD_ACTION(multirom_reset_roms_paths);
		ADD_ACTION(multirom_rename);
		ADD_ACTION(multirom_manage);
		ADD_ACTION(multirom_settings);
		ADD_ACTION(multirom_settings_save);
		ADD_ACTION(multirom_add);
		ADD_ACTION(multirom_add_second);
		ADD_ACTION(multirom_add_file_selected);
		ADD_ACTION(multirom_change_img_size);
		ADD_ACTION(multirom_change_img_size_act);
		ADD_ACTION(multirom_set_list_loc);
		ADD_ACTION(multirom_list_loc_selected);
		ADD_ACTION(multirom_exit_backup);
		ADD_ACTION(multirom_create_internal_rom_name);
		ADD_ACTION(multirom_list_roms_for_swap);
		ADD_ACTION(multirom_swap_calc_space);
#endif //TARGET_RECOVERY_IS_MULTIROM
		ADD_ACTION(setlanguage);
		ADD_ACTION(checkforapp);
		ADD_ACTION(togglebacklight);

		// remember actions that run in the caller thread
		for (mapFunc::const_iterator it = mf.begin(); it != mf.end(); ++it)
			setActionsRunningInCallerThread.insert(it->first);

		// These actions will run in a separate thread
		ADD_ACTION(flash);
		ADD_ACTION(wipe);
		ADD_ACTION(refreshsizes);
		ADD_ACTION(nandroid);
		ADD_ACTION(fixcontexts);
		ADD_ACTION(fixpermissions);
		ADD_ACTION(dd);
		ADD_ACTION(partitionsd);
		ADD_ACTION(installhtcdumlock);
		ADD_ACTION(htcdumlockrestoreboot);
		ADD_ACTION(htcdumlockreflashrecovery);
		ADD_ACTION(cmd);
		ADD_ACTION(terminalcommand);
		ADD_ACTION(reinjecttwrp);
		ADD_ACTION(decrypt);
		ADD_ACTION(adbsideload);
		ADD_ACTION(openrecoveryscript);
		ADD_ACTION(installsu);
		ADD_ACTION(decrypt_backup);
		ADD_ACTION(repair);
		ADD_ACTION(resize);
		ADD_ACTION(changefilesystem);
		ADD_ACTION(flashimage);
#ifdef TARGET_RECOVERY_IS_MULTIROM
		ADD_ACTION(multirom_delete);
		ADD_ACTION(multirom_flash_zip);
		ADD_ACTION(multirom_flash_zip_sailfish);
		ADD_ACTION(multirom_inject);
		ADD_ACTION(multirom_inject_curr_boot);
		ADD_ACTION(multirom_add_rom);
		ADD_ACTION(multirom_ubuntu_patch_init);
		ADD_ACTION(multirom_touch_patch_init);
		ADD_ACTION(multirom_wipe);
		ADD_ACTION(multirom_disable_flash_kernel);
		ADD_ACTION(multirom_rm_bootimg);
		ADD_ACTION(multirom_backup_rom);
		ADD_ACTION(multirom_sideload);
		ADD_ACTION(multirom_execute_swap);
		ADD_ACTION(multirom_set_fw);
		ADD_ACTION(multirom_remove_fw);
		ADD_ACTION(multirom_restorecon);
		ADD_ACTION(system_image_upgrader);
#endif //TARGET_RECOVERY_IS_MULTIROM
		ADD_ACTION(twcmd);
		ADD_ACTION(setbootslot);
		ADD_ACTION(installapp);
	}

	// First, get the action
	actions = FindNode(node, "actions");
	if (actions)	child = FindNode(actions, "action");
	else			child = FindNode(node, "action");

	if (!child) return;

	while (child)
	{
		Action action;

		attr = child->first_attribute("function");
		if (!attr)  return;

		action.mFunction = attr->value();
		action.mArg = child->value();
		mActions.push_back(action);

		child = child->next_sibling("action");
	}

	// Now, let's get either the key or region
	child = FindNode(node, "touch");
	if (child)
	{
		attr = child->first_attribute("key");
		if (attr)
		{
			std::vector<std::string> keys = TWFunc::Split_String(attr->value(), "+");
			for (size_t i = 0; i < keys.size(); ++i)
			{
				const int key = getKeyByName(keys[i]);
				mKeys[key] = false;
			}
		}
		else
		{
			attr = child->first_attribute("x");
			if (!attr)  return;
			mActionX = atol(attr->value());
			attr = child->first_attribute("y");
			if (!attr)  return;
			mActionY = atol(attr->value());
			attr = child->first_attribute("w");
			if (!attr)  return;
			mActionW = atol(attr->value());
			attr = child->first_attribute("h");
			if (!attr)  return;
			mActionH = atol(attr->value());
		}
	}
}

int GUIAction::NotifyTouch(TOUCH_STATE state, int x __unused, int y __unused)
{
	if (state == TOUCH_RELEASE)
		doActions();

	return 0;
}

int GUIAction::NotifyKey(int key, bool down)
{
	std::map<int, bool>::iterator itr = mKeys.find(key);
	if (itr == mKeys.end())
		return 1;

	bool prevState = itr->second;
	itr->second = down;

	// If there is only one key for this action, wait for key up so it
	// doesn't trigger with multi-key actions.
	// Else, check if all buttons are pressed, then consume their release events
	// so they don't trigger one-button actions and reset mKeys pressed status
	if (mKeys.size() == 1) {
		if (!down && prevState) {
			doActions();
			return 0;
		}
	} else if (down) {
		for (itr = mKeys.begin(); itr != mKeys.end(); ++itr) {
			if (!itr->second)
				return 1;
		}

		// Passed, all req buttons are pressed, reset them and consume release events
		HardwareKeyboard *kb = PageManager::GetHardwareKeyboard();
		for (itr = mKeys.begin(); itr != mKeys.end(); ++itr) {
			kb->ConsumeKeyRelease(itr->first);
			itr->second = false;
		}

		doActions();
		return 0;
	}

	return 1;
}

int GUIAction::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

	if (varName.empty() && !isConditionValid() && mKeys.empty() && !mActionW)
		doActions();
	else if ((varName.empty() || IsConditionVariable(varName)) && isConditionValid() && isConditionTrue())
		doActions();

	return 0;
}

void GUIAction::simulate_progress_bar(void)
{
	gui_msg("simulating=Simulating actions...");
	for (int i = 0; i < 5; i++)
	{
		if (PartitionManager.stop_backup.get_value()) {
			DataManager::SetValue("tw_cancel_backup", 1);
			gui_msg("backup_cancel=Backup Cancelled");
			DataManager::SetValue("ui_progress", 0);
			PartitionManager.stop_backup.set_value(0);
			return;
		}
		usleep(500000);
		DataManager::SetValue("ui_progress", i * 20);
	}
}

int GUIAction::flash_zip(std::string filename, int* wipe_cache)
{
	int ret_val = 0;

	DataManager::SetValue("ui_progress", 0);

	if (filename.empty())
	{
		LOGERR("No file specified.\n");
		return -1;
	}

	if (!TWFunc::Path_Exists(filename)) {
		if (!PartitionManager.Mount_By_Path(filename, true)) {
			return -1;
		}
		if (!TWFunc::Path_Exists(filename)) {
			gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")(filename));
			return -1;
		}
	}

	if (simulate) {
		simulate_progress_bar();
	} else {
		ret_val = TWinstall_zip(filename.c_str(), wipe_cache);

		// Now, check if we need to ensure TWRP remains installed...
		struct stat st;
		if (stat("/sbin/installTwrp", &st) == 0)
		{
			DataManager::SetValue("tw_operation", "Configuring TWRP");
			DataManager::SetValue("tw_partition", "");
			gui_msg("config_twrp=Configuring TWRP...");
			if (TWFunc::Exec_Cmd("/sbin/installTwrp reinstall") < 0)
			{
				gui_msg("config_twrp_err=Unable to configure TWRP with this kernel.");
			}
		}
	}

	// Done
	DataManager::SetValue("ui_progress", 100);
	DataManager::SetValue("ui_progress", 0);
	return ret_val;
}

GUIAction::ThreadType GUIAction::getThreadType(const GUIAction::Action& action)
{
	string func = gui_parse_text(action.mFunction);
	bool needsThread = setActionsRunningInCallerThread.find(func) == setActionsRunningInCallerThread.end();
	if (needsThread) {
		if (func == "cancelbackup")
			return THREAD_CANCEL;
		else
			return THREAD_ACTION;
	}
	return THREAD_NONE;
}

int GUIAction::doActions()
{
	if (mActions.size() < 1)
		return -1;

	// Determine in which thread to run the actions.
	// Do it for all actions at once before starting, so that we can cancel the whole batch if the thread is already busy.
	ThreadType threadType = THREAD_NONE;
	std::vector<Action>::iterator it;
	for (it = mActions.begin(); it != mActions.end(); ++it) {
		ThreadType tt = getThreadType(*it);
		if (tt == THREAD_NONE)
			continue;
		if (threadType == THREAD_NONE)
			threadType = tt;
		else if (threadType != tt) {
			LOGERR("Can't mix normal and cancel actions in the same list.\n"
				"Running the whole batch in the cancel thread.\n");
			threadType = THREAD_CANCEL;
			break;
		}
	}

	// Now run the actions in the desired thread.
	switch (threadType) {
		case THREAD_ACTION:
			action_thread.threadActions(this);
			break;

		case THREAD_CANCEL:
			cancel_thread.threadActions(this);
			break;

		default: {
			// no iterators here because theme reloading might kill our object
			const size_t cnt = mActions.size();
			for (size_t i = 0; i < cnt; ++i)
				doAction(mActions[i]);
		}
	}

	return 0;
}

int GUIAction::doAction(Action action)
{
	DataManager::GetValue(TW_SIMULATE_ACTIONS, simulate);

	std::string function = gui_parse_text(action.mFunction);
	std::string arg = gui_parse_text(action.mArg);

	// find function and execute it
	mapFunc::const_iterator funcitr = mf.find(function);
	if (funcitr != mf.end())
		return (this->*funcitr->second)(arg);

	LOGERR("Unknown action '%s'\n", function.c_str());
	return -1;
}

void GUIAction::operation_start(const string operation_name)
{
	LOGINFO("operation_start: '%s'\n", operation_name.c_str());
	time(&Start);
	DataManager::SetValue(TW_ACTION_BUSY, 1);
	DataManager::SetValue("ui_progress", 0);
	DataManager::SetValue("tw_operation", operation_name);
	DataManager::SetValue("tw_operation_state", 0);
	DataManager::SetValue("tw_operation_status", 0);
}

void GUIAction::operation_end(const int operation_status)
{
	time_t Stop;
	int simulate_fail;
	DataManager::SetValue("ui_progress", 100);
	if (simulate) {
		DataManager::GetValue(TW_SIMULATE_FAIL, simulate_fail);
		if (simulate_fail != 0)
			DataManager::SetValue("tw_operation_status", 1);
		else
			DataManager::SetValue("tw_operation_status", 0);
	} else {
		if (operation_status != 0) {
			DataManager::SetValue("tw_operation_status", 1);
		}
		else {
			DataManager::SetValue("tw_operation_status", 0);
		}
	}
	DataManager::SetValue("tw_operation_state", 1);
	DataManager::SetValue(TW_ACTION_BUSY, 0);
	blankTimer.resetTimerAndUnblank();
	property_set("twrp.action_complete", "1");
	time(&Stop);
	if ((int) difftime(Stop, Start) > 10)
		DataManager::Vibrate("tw_action_vibrate");
	LOGINFO("operation_end - status=%d\n", operation_status);
}

int GUIAction::reboot(std::string arg)
{
	sync();
	DataManager::SetValue("tw_gui_done", 1);
	DataManager::SetValue("tw_reboot_arg", arg);

	return 0;
}

int GUIAction::home(std::string arg __unused)
{
	gui_changePage("main");
	return 0;
}

int GUIAction::key(std::string arg)
{
	const int key = getKeyByName(arg);
	PageManager::NotifyKey(key, true);
	PageManager::NotifyKey(key, false);
	return 0;
}

int GUIAction::page(std::string arg)
{
	property_set("twrp.action_complete", "0");
	std::string page_name = gui_parse_text(arg);
	return gui_changePage(page_name);
}

int GUIAction::reload(std::string arg __unused)
{
	PageManager::RequestReload();
	// The actual reload is handled in pages.cpp in PageManager::RunReload()
	// The reload will occur on the next Update or Render call and will
	// be performed in the rendoer thread instead of the action thread
	// to prevent crashing which could occur when we start deleting
	// GUI resources in the action thread while we attempt to render
	// with those same resources in another thread.
#ifdef TARGET_RECOVERY_IS_MULTIROM
//TODO
//MultiROM uses this instead
//	return !TWFunc::reloadTheme();
#endif //TARGET_RECOVERY_IS_MULTIROM
	return 0;
}

int GUIAction::readBackup(std::string arg __unused)
{
	string Restore_Name;
	DataManager::GetValue("tw_restore", Restore_Name);
	PartitionManager.Set_Restore_Files(Restore_Name);
	return 0;
}

int GUIAction::set(std::string arg)
{
	if (arg.find('=') != string::npos)
	{
		string varName = arg.substr(0, arg.find('='));
		string value = arg.substr(arg.find('=') + 1, string::npos);

		DataManager::GetValue(value, value);
		DataManager::SetValue(varName, value);
	}
	else
		DataManager::SetValue(arg, "1");
	return 0;
}

int GUIAction::clear(std::string arg)
{
	DataManager::SetValue(arg, "0");
	return 0;
}

int GUIAction::mount(std::string arg)
{
	if (arg == "usb") {
		DataManager::SetValue(TW_ACTION_BUSY, 1);
		if (!simulate)
			PartitionManager.usb_storage_enable();
		else
			gui_msg("simulating=Simulating actions...");
	} else if (!simulate) {
		PartitionManager.Mount_By_Path(arg, true);
		PartitionManager.Add_MTP_Storage(arg);
	} else
		gui_msg("simulating=Simulating actions...");
	return 0;
}

int GUIAction::unmount(std::string arg)
{
	if (arg == "usb") {
		if (!simulate)
			PartitionManager.usb_storage_disable();
		else
			gui_msg("simulating=Simulating actions...");
		DataManager::SetValue(TW_ACTION_BUSY, 0);
	} else if (!simulate) {
		PartitionManager.UnMount_By_Path(arg, true);
	} else
		gui_msg("simulating=Simulating actions...");
	return 0;
}

int GUIAction::restoredefaultsettings(std::string arg __unused)
{
	operation_start("Restore Defaults");
	if (simulate) // Simulated so that people don't accidently wipe out the "simulation is on" setting
		gui_msg("simulating=Simulating actions...");
	else {
		DataManager::ResetDefaults();
		PartitionManager.Update_System_Details();
		PartitionManager.Mount_Current_Storage(true);
	}
	operation_end(0);
	return 0;
}

int GUIAction::copylog(std::string arg __unused)
{
	operation_start("Copy Log");
	if (!simulate)
	{
		string dst, curr_storage;
		int copy_kernel_log = 0;

		DataManager::GetValue("tw_include_kernel_log", copy_kernel_log);
		PartitionManager.Mount_Current_Storage(true);
		curr_storage = DataManager::GetCurrentStoragePath();
		dst = curr_storage + "/recovery.log";
		TWFunc::copy_file("/tmp/recovery.log", dst.c_str(), 0755);
		tw_set_default_metadata(dst.c_str());
		if (copy_kernel_log)
			TWFunc::copy_kernel_log(curr_storage);
		sync();
		gui_msg(Msg("copy_log=Copied recovery log to {1}")(dst));
	} else
		simulate_progress_bar();
	operation_end(0);
	return 0;
}


int GUIAction::compute(std::string arg)
{
	if (arg.find("+") != string::npos)
	{
		string varName = arg.substr(0, arg.find('+'));
		string string_to_add = arg.substr(arg.find('+') + 1, string::npos);
		int amount_to_add = atoi(string_to_add.c_str());
		int value;

		DataManager::GetValue(varName, value);
		DataManager::SetValue(varName, value + amount_to_add);
		return 0;
	}
	if (arg.find("-") != string::npos)
	{
		string varName = arg.substr(0, arg.find('-'));
		string string_to_subtract = arg.substr(arg.find('-') + 1, string::npos);
		int amount_to_subtract = atoi(string_to_subtract.c_str());
		int value;

		DataManager::GetValue(varName, value);
		value -= amount_to_subtract;
		if (value <= 0)
			value = 0;
		DataManager::SetValue(varName, value);
		return 0;
	}
	if (arg.find("*") != string::npos)
	{
		string varName = arg.substr(0, arg.find('*'));
		string multiply_by_str = gui_parse_text(arg.substr(arg.find('*') + 1, string::npos));
		int multiply_by = atoi(multiply_by_str.c_str());
		int value;

		DataManager::GetValue(varName, value);
		DataManager::SetValue(varName, value*multiply_by);
		return 0;
	}
	if (arg.find("/") != string::npos)
	{
		string varName = arg.substr(0, arg.find('/'));
		string divide_by_str = gui_parse_text(arg.substr(arg.find('/') + 1, string::npos));
		int divide_by = atoi(divide_by_str.c_str());
		int value;

		if (divide_by != 0)
		{
			DataManager::GetValue(varName, value);
			DataManager::SetValue(varName, value/divide_by);
		}
		return 0;
	}
	LOGERR("Unable to perform compute '%s'\n", arg.c_str());
	return -1;
}

int GUIAction::setguitimezone(std::string arg __unused)
{
	string SelectedZone;
	DataManager::GetValue(TW_TIME_ZONE_GUISEL, SelectedZone); // read the selected time zone into SelectedZone
	string Zone = SelectedZone.substr(0, SelectedZone.find(';')); // parse to get time zone
	string DSTZone = SelectedZone.substr(SelectedZone.find(';') + 1, string::npos); // parse to get DST component

	int dst;
	DataManager::GetValue(TW_TIME_ZONE_GUIDST, dst); // check wether user chose to use DST

	string offset;
	DataManager::GetValue(TW_TIME_ZONE_GUIOFFSET, offset); // pull in offset

	string NewTimeZone = Zone;
	if (offset != "0")
		NewTimeZone += ":" + offset;

	if (dst != 0)
		NewTimeZone += DSTZone;

	DataManager::SetValue(TW_TIME_ZONE_VAR, NewTimeZone);
	DataManager::update_tz_environment_variables();
	return 0;
}

int GUIAction::overlay(std::string arg)
{
	return gui_changeOverlay(arg);
}

int GUIAction::queuezip(std::string arg __unused)
{
	if (zip_queue_index >= 10) {
		gui_msg("max_queue=Maximum zip queue reached!");
		return 0;
	}
	DataManager::GetValue("tw_filename", zip_queue[zip_queue_index]);
	if (strlen(zip_queue[zip_queue_index].c_str()) > 0) {
		zip_queue_index++;
		DataManager::SetValue(TW_ZIP_QUEUE_COUNT, zip_queue_index);
	}
	return 0;
}

int GUIAction::cancelzip(std::string arg __unused)
{
	if (zip_queue_index <= 0) {
		gui_msg("min_queue=Minimum zip queue reached!");
		return 0;
	} else {
		zip_queue_index--;
		DataManager::SetValue(TW_ZIP_QUEUE_COUNT, zip_queue_index);
	}
	return 0;
}

int GUIAction::queueclear(std::string arg __unused)
{
	zip_queue_index = 0;
	DataManager::SetValue(TW_ZIP_QUEUE_COUNT, zip_queue_index);
	return 0;
}

int GUIAction::sleep(std::string arg)
{
	operation_start("Sleep");
	usleep(atoi(arg.c_str()));
	operation_end(0);
	return 0;
}

int GUIAction::sleepcounter(std::string arg)
{
	operation_start("SleepCounter");
	// Ensure user notices countdown in case it needs to be cancelled
	blankTimer.resetTimerAndUnblank();
	int total = atoi(arg.c_str());
	for (int t = total; t > 0; t--) {
		int progress = (int)(((float)(total-t)/(float)total)*100.0);
		DataManager::SetValue("ui_progress", progress);
		::sleep(1);
		DataManager::SetValue("tw_sleep", t-1);
	}
	DataManager::SetValue("ui_progress", 100);
	operation_end(0);
	return 0;
}

int GUIAction::appenddatetobackupname(std::string arg __unused)
{
	operation_start("AppendDateToBackupName");
	string Backup_Name;
	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	Backup_Name += TWFunc::Get_Current_Date();
	if (Backup_Name.size() > MAX_BACKUP_NAME_LEN)
		Backup_Name.resize(MAX_BACKUP_NAME_LEN);
	DataManager::SetValue(TW_BACKUP_NAME, Backup_Name);
	PageManager::NotifyKey(KEY_END, true);
	PageManager::NotifyKey(KEY_END, false);
	operation_end(0);
	return 0;
}

int GUIAction::generatebackupname(std::string arg __unused)
{
	operation_start("GenerateBackupName");
	TWFunc::Auto_Generate_Backup_Name();
	operation_end(0);
	return 0;
}

int GUIAction::checkpartitionlist(std::string arg)
{
	string List, part_path;
	int count = 0;

	if (arg.empty())
		arg = "tw_wipe_list";
	DataManager::GetValue(arg, List);
	LOGINFO("checkpartitionlist list '%s'\n", List.c_str());
	if (!List.empty()) {
		size_t start_pos = 0, end_pos = List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < List.size()) {
			part_path = List.substr(start_pos, end_pos - start_pos);
			LOGINFO("checkpartitionlist part_path '%s'\n", part_path.c_str());
			if (part_path == "/and-sec" || part_path == "DALVIK" || part_path == "INTERNAL") {
				// Do nothing
			} else {
				count++;
			}
			start_pos = end_pos + 1;
			end_pos = List.find(";", start_pos);
		}
		DataManager::SetValue("tw_check_partition_list", count);
	} else {
		DataManager::SetValue("tw_check_partition_list", 0);
	}
	return 0;
}

int GUIAction::getpartitiondetails(std::string arg)
{
	string List, part_path;

	if (arg.empty())
		arg = "tw_wipe_list";
	DataManager::GetValue(arg, List);
	LOGINFO("getpartitiondetails list '%s'\n", List.c_str());
	if (!List.empty()) {
		size_t start_pos = 0, end_pos = List.find(";", start_pos);
		part_path = List;
		while (end_pos != string::npos && start_pos < List.size()) {
			part_path = List.substr(start_pos, end_pos - start_pos);
			LOGINFO("getpartitiondetails part_path '%s'\n", part_path.c_str());
			if (part_path == "/and-sec" || part_path == "DALVIK" || part_path == "INTERNAL") {
				// Do nothing
			} else {
				DataManager::SetValue("tw_partition_path", part_path);
				break;
			}
			start_pos = end_pos + 1;
			end_pos = List.find(";", start_pos);
		}
		if (!part_path.empty()) {
			TWPartition* Part = PartitionManager.Find_Partition_By_Path(part_path);
			if (Part) {
				unsigned long long mb = 1048576;

				DataManager::SetValue("tw_partition_name", Part->Display_Name);
				DataManager::SetValue("tw_partition_mount_point", Part->Mount_Point);
				DataManager::SetValue("tw_partition_file_system", Part->Current_File_System);
				DataManager::SetValue("tw_partition_size", Part->Size / mb);
				DataManager::SetValue("tw_partition_used", Part->Used / mb);
				DataManager::SetValue("tw_partition_free", Part->Free / mb);
				DataManager::SetValue("tw_partition_backup_size", Part->Backup_Size / mb);
				DataManager::SetValue("tw_partition_removable", Part->Removable);
				DataManager::SetValue("tw_partition_is_present", Part->Is_Present);

				if (Part->Can_Repair())
					DataManager::SetValue("tw_partition_can_repair", 1);
				else
					DataManager::SetValue("tw_partition_can_repair", 0);
				if (Part->Can_Resize())
					DataManager::SetValue("tw_partition_can_resize", 1);
				else
					DataManager::SetValue("tw_partition_can_resize", 0);
				if (TWFunc::Path_Exists("/sbin/mkfs.fat"))
					DataManager::SetValue("tw_partition_vfat", 1);
				else
					DataManager::SetValue("tw_partition_vfat", 0);
				if (TWFunc::Path_Exists("/sbin/mkexfatfs"))
					DataManager::SetValue("tw_partition_exfat", 1);
				else
					DataManager::SetValue("tw_partition_exfat", 0);
				if (TWFunc::Path_Exists("/sbin/mkfs.f2fs"))
					DataManager::SetValue("tw_partition_f2fs", 1);
				else
					DataManager::SetValue("tw_partition_f2fs", 0);
				if (TWFunc::Path_Exists("/sbin/mke2fs"))
					DataManager::SetValue("tw_partition_ext", 1);
				else
					DataManager::SetValue("tw_partition_ext", 0);
				return 0;
			} else {
				LOGERR("Unable to locate partition: '%s'\n", part_path.c_str());
			}
		}
	}
	DataManager::SetValue("tw_partition_name", "");
	DataManager::SetValue("tw_partition_file_system", "");
	// Set this to 0 to prevent trying to partition this device, just in case
	DataManager::SetValue("tw_partition_removable", 0);
	return 0;
}

int GUIAction::screenshot(std::string arg __unused)
{
	time_t tm;
	char path[256];
	int path_len;
	uid_t uid = AID_MEDIA_RW;
	gid_t gid = AID_MEDIA_RW;

	const std::string storage = DataManager::GetCurrentStoragePath();
	if (PartitionManager.Is_Mounted_By_Path(storage)) {
		snprintf(path, sizeof(path), "%s/Pictures/Screenshots/", storage.c_str());
	} else {
		strcpy(path, "/tmp/");
	}

	if (!TWFunc::Create_Dir_Recursive(path, 0775, uid, gid))
		return 0;

	tm = time(NULL);
	path_len = strlen(path);

	// Screenshot_2014-01-01-18-21-38.png
	strftime(path+path_len, sizeof(path)-path_len, "Screenshot_%Y-%m-%d-%H-%M-%S.png", localtime(&tm));

	int res = gr_save_screenshot(path);
	if (res == 0) {
		chmod(path, 0666);
		chown(path, uid, gid);

		gui_msg(Msg("screenshot_saved=Screenshot was saved to {1}")(path));

		// blink to notify that the screenshow was taken
		gr_color(255, 255, 255, 255);
		gr_fill(0, 0, gr_fb_width(), gr_fb_height());
		gr_flip();
		gui_forceRender();
	} else {
		gui_err("screenshot_err=Failed to take a screenshot!");
	}
	return 0;
}

int GUIAction::setbrightness(std::string arg)
{
	return TWFunc::Set_Brightness(arg);
}

int GUIAction::fileexists(std::string arg)
{
	struct stat st;
	string newpath = arg + "/.";

	operation_start("FileExists");
	if (stat(arg.c_str(), &st) == 0 || stat(newpath.c_str(), &st) == 0)
		operation_end(0);
	else
		operation_end(1);
	return 0;
}

void GUIAction::reinject_after_flash()
{
	if (DataManager::GetIntValue(TW_HAS_INJECTTWRP) == 1 && DataManager::GetIntValue(TW_INJECT_AFTER_ZIP) == 1) {
		gui_msg("injecttwrp=Injecting TWRP into boot image...");
		if (simulate) {
			simulate_progress_bar();
		} else {
			TWPartition* Boot = PartitionManager.Find_Partition_By_Path("/boot");
			if (Boot == NULL || Boot->Current_File_System != "emmc")
				TWFunc::Exec_Cmd("injecttwrp --dump /tmp/backup_recovery_ramdisk.img /tmp/injected_boot.img --flash");
			else {
				string injectcmd = "injecttwrp --dump /tmp/backup_recovery_ramdisk.img /tmp/injected_boot.img --flash bd=" + Boot->Actual_Block_Device;
				TWFunc::Exec_Cmd(injectcmd);
			}
			gui_msg("done=Done.");
		}
	}
#ifdef TARGET_RECOVERY_IS_MULTIROM
	if(DataManager::GetIntValue(TW_AUTO_INJECT_MROM) == 1 && MultiROM::folderExists())
	{
		gui_print("Injecting boot.img with MultiROM...\n");
		MultiROM::injectBoot(MultiROM::getBootDev(), true);
	}
#endif //TARGET_RECOVERY_IS_MULTIROM
}

int GUIAction::flash(std::string arg)
{
	int i, ret_val = 0, wipe_cache = 0;
	// We're going to jump to this page first, like a loading page
	gui_changePage(arg);
	for (i=0; i<zip_queue_index; i++) {
		string zip_path = zip_queue[i];
		size_t slashpos = zip_path.find_last_of('/');
		string zip_filename = (slashpos == string::npos) ? zip_path : zip_path.substr(slashpos + 1);
		operation_start("Flashing");
		DataManager::SetValue("tw_filename", zip_path);
		DataManager::SetValue("tw_file", zip_filename);
		DataManager::SetValue(TW_ZIP_INDEX, (i + 1));

		TWFunc::SetPerformanceMode(true);
		ret_val = flash_zip(zip_path, &wipe_cache);
		TWFunc::SetPerformanceMode(false);
		if (ret_val != 0) {
			gui_msg(Msg(msg::kError, "zip_err=Error installing zip file '{1}'")(zip_path));
			ret_val = 1;
			break;
		}
	}
	zip_queue_index = 0;

	if (wipe_cache) {
		gui_msg("zip_wipe_cache=One or more zip requested a cache wipe -- Wiping cache now.");
		PartitionManager.Wipe_By_Path("/cache");
	}

	reinject_after_flash();
	PartitionManager.Update_System_Details();
	operation_end(ret_val);
	// This needs to be after the operation_end call so we change pages before we change variables that we display on the screen
	DataManager::SetValue(TW_ZIP_QUEUE_COUNT, zip_queue_index);
	return 0;
}

int GUIAction::wipe(std::string arg)
{
	operation_start("Format");
	DataManager::SetValue("tw_partition", arg);

	int ret_val = false;

	if (simulate) {
		simulate_progress_bar();
	} else {
		if (arg == "data")
			ret_val = PartitionManager.Factory_Reset();
		else if (arg == "battery")
			ret_val = PartitionManager.Wipe_Battery_Stats();
		else if (arg == "rotate")
			ret_val = PartitionManager.Wipe_Rotate_Data();
		else if (arg == "dalvik")
			ret_val = PartitionManager.Wipe_Dalvik_Cache();
		else if (arg == "DATAMEDIA") {
			ret_val = PartitionManager.Format_Data();
		} else if (arg == "INTERNAL") {
			int has_datamedia;

			DataManager::GetValue(TW_HAS_DATA_MEDIA, has_datamedia);
			if (has_datamedia) {
				ret_val = PartitionManager.Wipe_Media_From_Data();
			} else {
				ret_val = PartitionManager.Wipe_By_Path(DataManager::GetSettingsStoragePath());
			}
		} else if (arg == "EXTERNAL") {
			string External_Path;

			DataManager::GetValue(TW_EXTERNAL_PATH, External_Path);
			ret_val = PartitionManager.Wipe_By_Path(External_Path);
		} else if (arg == "ANDROIDSECURE") {
			ret_val = PartitionManager.Wipe_Android_Secure();
		} else if (arg == "LIST") {
			string Wipe_List, wipe_path;
			bool skip = false;
			ret_val = true;

			DataManager::GetValue("tw_wipe_list", Wipe_List);
			LOGINFO("wipe list '%s'\n", Wipe_List.c_str());
			if (!Wipe_List.empty()) {
				size_t start_pos = 0, end_pos = Wipe_List.find(";", start_pos);
				while (end_pos != string::npos && start_pos < Wipe_List.size()) {
					wipe_path = Wipe_List.substr(start_pos, end_pos - start_pos);
					LOGINFO("wipe_path '%s'\n", wipe_path.c_str());
					if (wipe_path == "/and-sec") {
						if (!PartitionManager.Wipe_Android_Secure()) {
							gui_msg("and_sec_wipe_err=Unable to wipe android secure");
							ret_val = false;
							break;
						} else {
							skip = true;
						}
					} else if (wipe_path == "DALVIK") {
						if (!PartitionManager.Wipe_Dalvik_Cache()) {
							gui_err("dalvik_wipe_err=Failed to wipe dalvik");
							ret_val = false;
							break;
						} else {
							skip = true;
						}
					} else if (wipe_path == "INTERNAL") {
						if (!PartitionManager.Wipe_Media_From_Data()) {
							ret_val = false;
							break;
						} else {
							skip = true;
						}
					}
					if (!skip) {
						if (!PartitionManager.Wipe_By_Path(wipe_path)) {
							gui_msg(Msg(msg::kError, "unable_to_wipe=Unable to wipe {1}.")(wipe_path));
							ret_val = false;
							break;
						} else if (wipe_path == DataManager::GetSettingsStoragePath()) {
							arg = wipe_path;
						}
					} else {
						skip = false;
					}
					start_pos = end_pos + 1;
					end_pos = Wipe_List.find(";", start_pos);
				}
			}
		} else
			ret_val = PartitionManager.Wipe_By_Path(arg);
#ifndef TW_OEM_BUILD
		if (arg == DataManager::GetSettingsStoragePath()) {
			// If we wiped the settings storage path, recreate the TWRP folder and dump the settings
			string Storage_Path = DataManager::GetSettingsStoragePath();

			if (PartitionManager.Mount_By_Path(Storage_Path, true)) {
				LOGINFO("Making TWRP folder and saving settings.\n");
				Storage_Path += "/TWRP";
				mkdir(Storage_Path.c_str(), 0777);
				DataManager::Flush();
			} else {
				LOGERR("Unable to recreate TWRP folder and save settings.\n");
			}
		}
#endif
	}
	PartitionManager.Update_System_Details();
	if (ret_val)
		ret_val = 0; // 0 is success
	else
		ret_val = 1; // 1 is failure
	operation_end(ret_val);
	return 0;
}

int GUIAction::refreshsizes(std::string arg __unused)
{
	operation_start("Refreshing Sizes");
	if (simulate) {
		simulate_progress_bar();
	} else
		PartitionManager.Update_System_Details();
	operation_end(0);
	return 0;
}

int GUIAction::nandroid(std::string arg)
{
	if (simulate) {
		PartitionManager.stop_backup.set_value(0);
		DataManager::SetValue("tw_partition", "Simulation");
		simulate_progress_bar();
		operation_end(0);
	} else {
		operation_start("Nandroid");
		int ret = 0;

		if (arg == "backup") {
			string Backup_Name;
			DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
			string auto_gen = gui_lookup("auto_generate", "(Auto Generate)");
			if (Backup_Name == auto_gen || Backup_Name == gui_lookup("curr_date", "(Current Date)") || Backup_Name == "0" || Backup_Name == "(" || PartitionManager.Check_Backup_Name(true) == 0) {
				ret = PartitionManager.Run_Backup(false);
			} else {
				operation_end(1);
				return -1;
			}
			DataManager::SetValue(TW_BACKUP_NAME, auto_gen);
		} else if (arg == "restore") {
			string Restore_Name;
			DataManager::GetValue("tw_restore", Restore_Name);
			ret = PartitionManager.Run_Restore(Restore_Name);
		} else {
			operation_end(1);
			return -1;
		}
		DataManager::SetValue("tw_encrypt_backup", 0);
		if (!PartitionManager.stop_backup.get_value()) {
			if (ret == false)
				ret = 1; // 1 for failure
			else
				ret = 0; // 0 for success
			DataManager::SetValue("tw_cancel_backup", 0);
		}
		else {
			DataManager::SetValue("tw_cancel_backup", 1);
			gui_msg("backup_cancel=Backup Cancelled");
			ret = 0;
		}
		operation_end(ret);
		return ret;
	}
	return 0;
}

int GUIAction::cancelbackup(std::string arg __unused) {
	if (simulate) {
		PartitionManager.stop_backup.set_value(1);
	}
	else {
		int op_status = PartitionManager.Cancel_Backup();
		if (op_status != 0)
			op_status = 1; // failure
	}

	return 0;
}

int GUIAction::fixcontexts(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Fix Contexts");
	LOGINFO("fix contexts started!\n");
	if (simulate) {
		simulate_progress_bar();
	} else {
		op_status = PartitionManager.Fix_Contexts();
		if (op_status != 0)
			op_status = 1; // failure
	}
	operation_end(op_status);
	return 0;
}

int GUIAction::fixpermissions(std::string arg)
{
	return fixcontexts(arg);
}

int GUIAction::dd(std::string arg)
{
	operation_start("imaging");

	if (simulate) {
		simulate_progress_bar();
	} else {
		string cmd = "dd " + arg;
		TWFunc::Exec_Cmd(cmd);
	}
	operation_end(0);
	return 0;
}

int GUIAction::partitionsd(std::string arg __unused)
{
	operation_start("Partition SD Card");
	int ret_val = 0;

	if (simulate) {
		simulate_progress_bar();
	} else {
		int allow_partition;
		DataManager::GetValue(TW_ALLOW_PARTITION_SDCARD, allow_partition);
		if (allow_partition == 0) {
			gui_err("no_real_sdcard=This device does not have a real SD Card! Aborting!");
		} else {
			if (!PartitionManager.Partition_SDCard())
				ret_val = 1; // failed
		}
	}
	operation_end(ret_val);
	return 0;

}

int GUIAction::installhtcdumlock(std::string arg __unused)
{
	operation_start("Install HTC Dumlock");
	if (simulate) {
		simulate_progress_bar();
	} else
		TWFunc::install_htc_dumlock();

	operation_end(0);
	return 0;
}

int GUIAction::htcdumlockrestoreboot(std::string arg __unused)
{
	operation_start("HTC Dumlock Restore Boot");
	if (simulate) {
		simulate_progress_bar();
	} else
		TWFunc::htc_dumlock_restore_original_boot();

	operation_end(0);
	return 0;
}

int GUIAction::htcdumlockreflashrecovery(std::string arg __unused)
{
	operation_start("HTC Dumlock Reflash Recovery");
	if (simulate) {
		simulate_progress_bar();
	} else
		TWFunc::htc_dumlock_reflash_recovery_to_boot();

	operation_end(0);
	return 0;
}

int GUIAction::cmd(std::string arg)
{
	int op_status = 0;

	operation_start("Command");
	LOGINFO("Running command: '%s'\n", arg.c_str());
	if (simulate) {
		simulate_progress_bar();
	} else {
		op_status = TWFunc::Exec_Cmd(arg);
		if (op_status != 0)
			op_status = 1;
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::terminalcommand(std::string arg)
{
	int op_status = 0;
	string cmdpath, command;

	DataManager::GetValue("tw_terminal_location", cmdpath);
	operation_start("CommandOutput");
	gui_print("%s # %s\n", cmdpath.c_str(), arg.c_str());
	if (simulate) {
		simulate_progress_bar();
		operation_end(op_status);
	} else if (arg == "exit") {
		LOGINFO("Exiting terminal\n");
		operation_end(op_status);
		page("main");
	} else {
		command = "cd \"" + cmdpath + "\" && " + arg + " 2>&1";;
		LOGINFO("Actual command is: '%s'\n", command.c_str());
		DataManager::SetValue("tw_terminal_state", 1);
		DataManager::SetValue("tw_background_thread_running", 1);
		FILE* fp;
		char line[512];

		fp = popen(command.c_str(), "r");
		if (fp == NULL) {
			LOGERR("Error opening command to run (%s).\n", strerror(errno));
		} else {
			int fd = fileno(fp), has_data = 0, check = 0, keep_going = -1;
			struct timeval timeout;
			fd_set fdset;

			while (keep_going)
			{
				FD_ZERO(&fdset);
				FD_SET(fd, &fdset);
				timeout.tv_sec = 0;
				timeout.tv_usec = 400000;
				has_data = select(fd+1, &fdset, NULL, NULL, &timeout);
				if (has_data == 0) {
					// Timeout reached
					DataManager::GetValue("tw_terminal_state", check);
					if (check == 0) {
						keep_going = 0;
					}
				} else if (has_data < 0) {
					// End of execution
					keep_going = 0;
				} else {
					// Try to read output
					if (fgets(line, sizeof(line), fp) != NULL)
						gui_print("%s", line); // Display output
					else
						keep_going = 0; // Done executing
				}
			}
			fclose(fp);
		}
		DataManager::SetValue("tw_operation_status", 0);
		DataManager::SetValue("tw_operation_state", 1);
		DataManager::SetValue("tw_terminal_state", 0);
		DataManager::SetValue("tw_background_thread_running", 0);
		DataManager::SetValue(TW_ACTION_BUSY, 0);
	}
	return 0;
}

int GUIAction::killterminal(std::string arg __unused)
{
	LOGINFO("Sending kill command...\n");
	operation_start("KillCommand");
	DataManager::SetValue("tw_operation_status", 0);
	DataManager::SetValue("tw_operation_state", 1);
	DataManager::SetValue("tw_terminal_state", 0);
	DataManager::SetValue("tw_background_thread_running", 0);
	DataManager::SetValue(TW_ACTION_BUSY, 0);
	return 0;
}

int GUIAction::reinjecttwrp(std::string arg __unused)
{
	int op_status = 0;
	operation_start("ReinjectTWRP");
	gui_msg("injecttwrp=Injecting TWRP into boot image...");
	if (simulate) {
		simulate_progress_bar();
	} else {
		TWFunc::Exec_Cmd("injecttwrp --dump /tmp/backup_recovery_ramdisk.img /tmp/injected_boot.img --flash");
		gui_msg("done=Done.");
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::checkbackupname(std::string arg __unused)
{
	int op_status = 0;

	operation_start("CheckBackupName");
	if (simulate) {
		simulate_progress_bar();
	} else {
		op_status = PartitionManager.Check_Backup_Name(true);
		if (op_status != 0)
			op_status = 1;
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::decrypt(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Decrypt");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string Password;
		DataManager::GetValue("tw_crypto_password", Password);
		op_status = PartitionManager.Decrypt_Device(Password);
		if (op_status != 0)
			op_status = 1;
		else {

			DataManager::SetValue(TW_IS_ENCRYPTED, 0);

			int has_datamedia;

			// Check for a custom theme and load it if exists
			DataManager::GetValue(TW_HAS_DATA_MEDIA, has_datamedia);
			if (has_datamedia != 0) {
				if (tw_get_default_metadata(DataManager::GetSettingsStoragePath().c_str()) != 0) {
					LOGINFO("Failed to get default contexts and file mode for storage files.\n");
				} else {
					LOGINFO("Got default contexts and file mode for storage files.\n");
				}
			}
			PartitionManager.Decrypt_Adopted();
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::adbsideload(std::string arg __unused)
{
	operation_start("Sideload");
	if (simulate) {
		simulate_progress_bar();
		operation_end(0);
	} else {
		gui_msg("start_sideload=Starting ADB sideload feature...");
		bool mtp_was_enabled = TWFunc::Toggle_MTP(false);

		// wait for the adb connection
		int ret = apply_from_adb("/", &sideload_child_pid);
		DataManager::SetValue("tw_has_cancel", 0); // Remove cancel button from gui now that the zip install is going to start

		if (ret != 0) {
			if (ret == -2)
				gui_msg("need_new_adb=You need adb 1.0.32 or newer to sideload to this device.");
			ret = 1; // failure
		} else {
			int wipe_cache = 0;
			int wipe_dalvik = 0;
			DataManager::GetValue("tw_wipe_dalvik", wipe_dalvik);

			if (TWinstall_zip(FUSE_SIDELOAD_HOST_PATHNAME, &wipe_cache) == 0) {
				if (wipe_cache || DataManager::GetIntValue("tw_wipe_cache"))
					PartitionManager.Wipe_By_Path("/cache");
				if (wipe_dalvik)
					PartitionManager.Wipe_Dalvik_Cache();
			} else {
				ret = 1; // failure
			}
		}
		if (sideload_child_pid) {
			LOGINFO("Signaling child sideload process to exit.\n");
			struct stat st;
			// Calling stat() on this magic filename signals the minadbd
			// subprocess to shut down.
			stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
			int status;
			LOGINFO("Waiting for child sideload process to exit.\n");
			waitpid(sideload_child_pid, &status, 0);
		}
		property_set("ctl.start", "adbd");
		TWFunc::Toggle_MTP(mtp_was_enabled);
		reinject_after_flash();
		operation_end(ret);
	}
	return 0;
}

int GUIAction::adbsideloadcancel(std::string arg __unused)
{
	struct stat st;
	DataManager::SetValue("tw_has_cancel", 0); // Remove cancel button from gui
	gui_msg("cancel_sideload=Cancelling ADB sideload...");
	LOGINFO("Signaling child sideload process to exit.\n");
	// Calling stat() on this magic filename signals the minadbd
	// subprocess to shut down.
	stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
	if (!sideload_child_pid) {
		LOGERR("Unable to get child ID\n");
		return 0;
	}
	::sleep(1);
	LOGINFO("Killing child sideload process.\n");
	kill(sideload_child_pid, SIGTERM);
	int status;
	LOGINFO("Waiting for child sideload process to exit.\n");
	waitpid(sideload_child_pid, &status, 0);
	sideload_child_pid = 0;
	DataManager::SetValue("tw_page_done", "1"); // For OpenRecoveryScript support
	return 0;
}

int GUIAction::openrecoveryscript(std::string arg __unused)
{
	operation_start("OpenRecoveryScript");
	if (simulate) {
		simulate_progress_bar();
		operation_end(0);
	} else {
		int op_status = OpenRecoveryScript::Run_OpenRecoveryScript_Action();
		operation_end(op_status);
	}
    //MultiROM note: TW_ORS_IS_SECONDARY_ROM moved to openrecoveryscript.cpp
	return 0;
}

int GUIAction::installsu(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Install SuperSU");
	if (simulate) {
		simulate_progress_bar();
	} else {
		if (!TWFunc::Install_SuperSU())
			op_status = 1;
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::fixsu(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Fixing Superuser Permissions");
	if (simulate) {
		simulate_progress_bar();
	} else {
		LOGERR("Fixing su permissions was deprecated from TWRP.\n");
		LOGERR("4.3+ ROMs with SELinux will always lose su perms.\n");
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::decrypt_backup(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Try Restore Decrypt");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string Restore_Path, Filename, Password;
		DataManager::GetValue("tw_restore", Restore_Path);
		Restore_Path += "/";
		DataManager::GetValue("tw_restore_password", Password);
		TWFunc::SetPerformanceMode(true);
		if (TWFunc::Try_Decrypting_Backup(Restore_Path, Password))
			op_status = 0; // success
		else
			op_status = 1; // fail
		TWFunc::SetPerformanceMode(false);
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::repair(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Repair Partition");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string part_path;
		DataManager::GetValue("tw_partition_mount_point", part_path);
		if (PartitionManager.Repair_By_Path(part_path, true)) {
			op_status = 0; // success
		} else {
			op_status = 1; // fail
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::resize(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Resize Partition");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string part_path;
		DataManager::GetValue("tw_partition_mount_point", part_path);
		if (PartitionManager.Resize_By_Path(part_path, true)) {
			op_status = 0; // success
		} else {
			op_status = 1; // fail
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::changefilesystem(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Change File System");
	if (simulate) {
		simulate_progress_bar();
	} else {
		string part_path, file_system;
		DataManager::GetValue("tw_partition_mount_point", part_path);
		DataManager::GetValue("tw_action_new_file_system", file_system);
		if (PartitionManager.Wipe_By_Path(part_path, file_system)) {
			op_status = 0; // success
		} else {
			gui_err("change_fs_err=Error changing file system.");
			op_status = 1; // fail
		}
	}
	PartitionManager.Update_System_Details();
	operation_end(op_status);
	return 0;
}

int GUIAction::startmtp(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Start MTP");
	if (PartitionManager.Enable_MTP())
		op_status = 0; // success
	else
		op_status = 1; // fail

	operation_end(op_status);
	return 0;
}

int GUIAction::stopmtp(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Stop MTP");
	if (PartitionManager.Disable_MTP())
		op_status = 0; // success
	else
		op_status = 1; // fail

	operation_end(op_status);
	return 0;
}

int GUIAction::flashimage(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Flash Image");
	string path, filename;
	DataManager::GetValue("tw_zip_location", path);
	DataManager::GetValue("tw_file", filename);
	if (PartitionManager.Flash_Image(path, filename))
		op_status = 0; // success
	else
		op_status = 1; // fail

	operation_end(op_status);
	return 0;
}

int GUIAction::twcmd(std::string arg)
{
	operation_start("TWRP CLI Command");
	if (simulate)
		simulate_progress_bar();
	else
		OpenRecoveryScript::Run_CLI_Command(arg.c_str());
	operation_end(0);
	return 0;
}

int GUIAction::getKeyByName(std::string key)
{
	if (key == "home")		return KEY_HOMEPAGE;  // note: KEY_HOME is cursor movement (like KEY_END)
	else if (key == "menu")		return KEY_MENU;
	else if (key == "back")	 	return KEY_BACK;
	else if (key == "search")	return KEY_SEARCH;
	else if (key == "voldown")	return KEY_VOLUMEDOWN;
	else if (key == "volup")	return KEY_VOLUMEUP;
	else if (key == "power") {
		int ret_val;
		DataManager::GetValue(TW_POWER_BUTTON, ret_val);
		if (!ret_val)
			return KEY_POWER;
		else
			return ret_val;
	}

	return atol(key.c_str());
}

int GUIAction::checkpartitionlifetimewrites(std::string arg)
{
	int op_status = 0;
	TWPartition* sys = PartitionManager.Find_Partition_By_Path(arg);

	operation_start("Check Partition Lifetime Writes");
	if (sys) {
		if (sys->Check_Lifetime_Writes() != 0)
			DataManager::SetValue("tw_lifetime_writes", 1);
		else
			DataManager::SetValue("tw_lifetime_writes", 0);
		op_status = 0; // success
	} else {
		DataManager::SetValue("tw_lifetime_writes", 1);
		op_status = 1; // fail
	}

	operation_end(op_status);
	return 0;
}

#ifdef TARGET_RECOVERY_IS_MULTIROM
int GUIAction::rotation(std::string arg)
{
	int rot = atoi(arg.c_str());
//TODO
////	if(rot == gr_get_rotation())
		return 0;

////	return !gui_rotate(rot);
}

int GUIAction::timeout(std::string arg __unused)
{
//TODO
#ifndef TW_NO_SCREEN_TIMEOUT
////	blankTimer.blankScreen();
#endif
	return 0;
}

int GUIAction::multirom(std::string arg __unused)
{
	if(MultiROM::folderExists())
		return gui_changePage("multirom_main");
	else
	{
		DataManager::SetValue("tw_mrom_title", "MultiROM is not installed!");
		DataManager::SetValue("tw_mrom_text1", "/data/media/multirom not found.");
		DataManager::SetValue("tw_mrom_text2", "/data/media/0/multirom not found.");
		DataManager::SetValue("tw_mrom_back", "main");
		return gui_changePage("multirom_msg");
	}
}

int GUIAction::multirom_reset_roms_paths(std::string arg __unused)
{
	MultiROM::setRomsPath(INTERNAL_MEM_LOC_TXT);
	DataManager::SetValue("tw_multirom_folder", MultiROM::getRomsPath());
	DataManager::SetValue("tw_multirom_install_loc", INTERNAL_MEM_LOC_TXT);
	return 0;
}

int GUIAction::multirom_rename(std::string arg)
{
	std::string new_name = arg;
	TWFunc::trim(new_name);
	MultiROM::move(DataManager::GetStrValue("tw_multirom_rom_name"), new_name);
	return gui_changePage("multirom_list");
}

int GUIAction::multirom_manage(std::string arg __unused)
{
	std::string name = DataManager::GetStrValue("tw_multirom_rom_name");
	int type = MultiROM::getType(name);
	DataManager::SetValue("tw_multirom_is_android", (M(type) & MASK_ANDROID) != 0);
	DataManager::SetValue("tw_multirom_is_ubuntu", (M(type) & MASK_UBUNTU) != 0);
	DataManager::SetValue("tw_multirom_is_touch", (M(type) & MASK_UTOUCH) != 0);
	DataManager::SetValue("tw_multirom_is_sailfish", (M(type) & MASK_SAILFISH) != 0);
	if((M(type) & MASK_ANDROID) != 0)
	{
		std::string path = MultiROM::getRomsPath() + "/" + name + "/boot.img";
		DataManager::SetValue("tw_multirom_has_bootimg", access(path.c_str(), F_OK) >= 0);
	}
	DataManager::SetValue("tw_multirom_has_fw_partition", MultiROM::hasFirmwareDev());
	if(MultiROM::hasFirmwareDev())
	{
		std::string fw_file = MultiROM::getRomsPath() + DataManager::GetStrValue("tw_multirom_rom_name") + "/firmware.img";
		DataManager::SetValue("tw_multirom_has_fw_image", int(access(fw_file.c_str(), F_OK) >= 0));
	}
	return gui_changePage("multirom_manage");
}

int GUIAction::multirom_settings(std::string arg __unused)
{
	MultiROM::config cfg = MultiROM::loadConfig();

	if(cfg.auto_boot_type & MROM_AUTOBOOT_CHECK_KEYS)
		DataManager::SetValue("tw_multirom_auto_boot_trigger", MROM_AUTOBOOT_TRIGGER_KEYS);
	else if(cfg.auto_boot_seconds > 0)
		DataManager::SetValue("tw_multirom_auto_boot_trigger", MROM_AUTOBOOT_TRIGGER_TIME);
	else
		DataManager::SetValue("tw_multirom_auto_boot_trigger", MROM_AUTOBOOT_TRIGGER_DISABLED);

	if(cfg.auto_boot_seconds <= 0)
		DataManager::SetValue("tw_multirom_delay", 5);
	else
		DataManager::SetValue("tw_multirom_delay", cfg.auto_boot_seconds);
	DataManager::SetValue("tw_multirom_current", cfg.current_rom);
	DataManager::SetValue("tw_multirom_auto_boot_rom", cfg.auto_boot_rom);
	DataManager::SetValue("tw_multirom_auto_boot_type", (cfg.auto_boot_type & MROM_AUTOBOOT_LAST));
#ifdef MR_NO_KEXEC
	DataManager::SetValue("tw_multirom_no_kexec", cfg.no_kexec & 0x3F);
	DataManager::SetValue("tw_multirom_no_kexec_internal", (cfg.no_kexec & 0x40) ? 1 : 0);
	DataManager::SetValue("tw_multirom_no_kexec_restore", (cfg.no_kexec & 0x80) ? 1 : 0);
#else
	DataManager::SetValue("tw_multirom_no_kexec_na", 1);  //no-kexec workaround is disabled in this build
	DataManager::SetValue("tw_multirom_no_kexec", 0);
#endif
	DataManager::SetValue("tw_multirom_colors", cfg.colors);
	DataManager::SetValue("tw_multirom_brightness", cfg.brightness);
	DataManager::SetValue("tw_multirom_enable_adb", cfg.enable_adb);
	DataManager::SetValue("tw_multirom_enable_kmsg_logging", cfg.enable_kmsg_logging);
	DataManager::SetValue("tw_multirom_hide_internal", cfg.hide_internal);
	DataManager::SetValue("tw_multirom_int_display_name", cfg.int_display_name);
	DataManager::SetValue("tw_multirom_rotation", cfg.rotation);
	DataManager::SetValue("tw_multirom_force_generic_fb", cfg.force_generic_fb);
	DataManager::SetValue("tw_anim_duration_coef_pct", cfg.anim_duration_coef_pct);

	DataManager::SetValue("tw_multirom_unrecognized_opts", cfg.unrecognized_opts);

	DataManager::SetValue("tw_multirom_roms", MultiROM::listRoms());
	return gui_changePage("multirom_settings");
}

int GUIAction::multirom_settings_save(std::string arg __unused)
{
	MultiROM::config cfg;
	cfg.current_rom = DataManager::GetStrValue("tw_multirom_current");
	cfg.auto_boot_type = DataManager::GetIntValue("tw_multirom_auto_boot_type");
	switch(DataManager::GetIntValue("tw_multirom_auto_boot_trigger"))
	{
		case MROM_AUTOBOOT_TRIGGER_DISABLED:
			cfg.auto_boot_seconds = 0;
			break;
		case MROM_AUTOBOOT_TRIGGER_TIME:
			cfg.auto_boot_seconds = DataManager::GetIntValue("tw_multirom_delay");
			break;
		case MROM_AUTOBOOT_TRIGGER_KEYS:
			cfg.auto_boot_type |= MROM_AUTOBOOT_CHECK_KEYS;
			break;
	}
	cfg.auto_boot_rom = DataManager::GetStrValue("tw_multirom_auto_boot_rom");
#ifdef MR_NO_KEXEC
	cfg.no_kexec = DataManager::GetIntValue("tw_multirom_no_kexec");
	if (cfg.no_kexec != 0)
	{
		cfg.no_kexec += (DataManager::GetIntValue("tw_multirom_no_kexec_internal") == 1) ? 0x40 : 0;
		cfg.no_kexec += (DataManager::GetIntValue("tw_multirom_no_kexec_restore") == 1) ? 0x80 : 0;
	}
#endif
	cfg.colors = DataManager::GetIntValue("tw_multirom_colors");
	cfg.brightness = DataManager::GetIntValue("tw_multirom_brightness");
	cfg.enable_adb = DataManager::GetIntValue("tw_multirom_enable_adb");
	cfg.enable_kmsg_logging = DataManager::GetIntValue("tw_multirom_enable_kmsg_logging");
	cfg.hide_internal = DataManager::GetIntValue("tw_multirom_hide_internal");
	cfg.int_display_name = DataManager::GetStrValue("tw_multirom_int_display_name");
	cfg.rotation = DataManager::GetIntValue("tw_multirom_rotation");
	cfg.force_generic_fb = DataManager::GetIntValue("tw_multirom_force_generic_fb");
	cfg.anim_duration_coef_pct = DataManager::GetIntValue("tw_anim_duration_coef_pct");

	cfg.unrecognized_opts = DataManager::GetStrValue("tw_multirom_unrecognized_opts");

	MultiROM::saveConfig(cfg);
	return 0;
}

int GUIAction::multirom_add(std::string arg __unused)
{
	DataManager::SetValue("tw_multirom_install_loc_list", MultiROM::listInstallLocations());
	DataManager::SetValue("tw_multirom_install_loc", INTERNAL_MEM_LOC_TXT);
	MultiROM::updateSupportedSystems();
	return gui_changePage("multirom_add");
}

int GUIAction::multirom_add_second(std::string arg __unused)
{
	switch(DataManager::GetIntValue("tw_multirom_type"))
	{
		case 1:
			return gui_changePage("multirom_add_source");
		case 5:
			DataManager::SetValue("tw_sailfish_filename_base", "");
			DataManager::SetValue("tw_sailfish_filename_rootfs", "");
			return gui_changePage("multirom_add_sailfish");
		default:
			return gui_changePage("multirom_add_select");
	}
}

int GUIAction::multirom_add_file_selected(std::string arg __unused)
{
	std::string loc = DataManager::GetStrValue("tw_multirom_install_loc");
	bool images = MultiROM::installLocNeedsImages(loc);
	int type = DataManager::GetIntValue("tw_multirom_type");

	MultiROM::clearBaseFolders();

	if(type == 1 || type == 2 || type == 5)
	{
		switch(type)
		{
			case 1: // Android
				MultiROM::addBaseFolder("data", DATA_IMG_MINSIZE, DATA_IMG_DEFSIZE);
				MultiROM::addBaseFolder("system", SYS_IMG_MINSIZE, SYS_IMG_DEFSIZE);
				MultiROM::addBaseFolder("cache", CACHE_IMG_MINSIZE, CACHE_IMG_DEFSIZE);
				break;
			case 2: // Ubuntu dekstop
				MultiROM::addBaseFolder("root", UB_DATA_IMG_MINSIZE, UB_DATA_IMG_DEFSIZE);
				break;
			case 5: // SailfishOS
				MultiROM::addBaseFolder("data", SAILFISH_DATA_IMG_MINSIZE, SAILFISH_DATA_IMG_DEFSIZE);
				MultiROM::addBaseFolder("system", SYS_IMG_MINSIZE, SYS_IMG_DEFSIZE);
				MultiROM::addBaseFolder("cache", CACHE_IMG_MINSIZE, CACHE_IMG_DEFSIZE);
				break;
		}

		MultiROM::updateImageVariables();

		if(images)
			return gui_changePage("multirom_add_image_size");
		else
			return gui_changePage("multirom_add_start_process");
	}
	else if(type == 3)
	{
		DataManager::SetValue("tw_mrom_back", "multirom_add");
		DataManager::SetValue("tw_mrom_text2", "");

		std:string ex;
		MROMInstaller *i = new MROMInstaller();

		DataManager::SetValue("tw_mrom_title", "Bad installer");
		if(!(ex = i->open(DataManager::GetStrValue("tw_filename"))).empty())
			return i->destroyWithErrorMsg(ex);

		DataManager::SetValue("tw_mrom_title", "Unsupported device");
		if(!(ex = i->checkDevices()).empty())
			return i->destroyWithErrorMsg(ex);

		DataManager::SetValue("tw_mrom_title", "Old MultiROM");
		if(!(ex = i->checkVersion()).empty())
			return i->destroyWithErrorMsg(ex);

		DataManager::SetValue("tw_mrom_title", "Unsupported install location");
		if(!(ex = i->setInstallLoc(loc, images)).empty())
			return i->destroyWithErrorMsg(ex);

		if(!(ex = i->parseBaseFolders(loc.find("ntfs") != std::string::npos)).empty())
			return i->destroyWithErrorMsg(ex);

		MultiROM::updateImageVariables();
		MultiROM::setInstaller(i);

		if(images)
			return gui_changePage("multirom_add_image_size");
		else
			return gui_changePage("multirom_add_start_process");
	}
	return 0;
}

int GUIAction::multirom_change_img_size(std::string arg)
{
	DataManager::SetValue("tw_multirom_image_too_small", 0);
	DataManager::SetValue("tw_multirom_image_too_big", 0);
	DataManager::SetValue("tw_multirom_image_name", arg);

	base_folder *b = MultiROM::getBaseFolder(arg);
	if(b != NULL)
		DataManager::SetValue("tw_multirom_image_size", b->size);

	return gui_changePage("multirom_change_img_size");
}

int GUIAction::multirom_change_img_size_act(std::string arg __unused)
{
	int value = DataManager::GetIntValue("tw_multirom_image_size");

	base_folder *b = MultiROM::getBaseFolder(DataManager::GetStrValue("tw_multirom_image_name"));
	if(!b)
		return gui_changePage("multirom_add_image_size");

	DataManager::SetValue("tw_multirom_image_too_small", 0);
	DataManager::SetValue("tw_multirom_image_too_big", 0);

	if(value < b->min_size)
	{
		DataManager::SetValue("tw_multirom_image_too_small", 1);
		DataManager::SetValue("tw_multirom_min_size", b->min_size);
		return gui_changePage("multirom_change_img_size");
	}

	if(value > 4095 &&
		DataManager::GetStrValue("tw_multirom_install_loc").find("(vfat") != std::string::npos)
	{
		DataManager::SetValue("tw_multirom_image_too_big", 1);
		return gui_changePage("multirom_change_img_size");
	}

	b->size = value;
	MultiROM::updateImageVariables();
	return gui_changePage("multirom_add_image_size");
}

int GUIAction::multirom_set_list_loc(std::string arg __unused)
{
	DataManager::SetValue("tw_multirom_install_loc_list", MultiROM::listInstallLocations());
	return gui_changePage("multirom_set_list_loc");
}

int GUIAction::multirom_list_loc_selected(std::string arg __unused)
{
	std::string loc = DataManager::GetStrValue("tw_multirom_install_loc");
	if(!MultiROM::setRomsPath(loc))
		MultiROM::setRomsPath(INTERNAL_MEM_LOC_TXT);
	DataManager::SetValue("tw_multirom_folder", MultiROM::getRomsPath());
	return gui_changePage("multirom_list");
}

int GUIAction::multirom_exit_backup(std::string arg)
{
	if(DataManager::GetIntValue("multirom_do_backup") == 1)
	{
		operation_start("Restoring default backup settings");
		MultiROM::deinitBackup();
		operation_end(0);
	}
	else if(arg == "multirom_manage")
		arg = "main";

	return gui_changePage(arg);
}

int GUIAction::multirom_create_internal_rom_name(std::string arg)
{
	std::string name = TWFunc::getROMName();
	if(name.size() > MAX_ROM_NAME)
		name.resize(MAX_ROM_NAME);
	else if(name.empty())
		name = "unknown";

	TWFunc::stringReplace(name, ' ', '_');

	std::string roms = MultiROM::getRomsPath();
	for(char i = '1'; TWFunc::Path_Exists(roms + name) && i <= '9'; ++i)
		name.replace(name.size()-1, 1, 1, i);

	DataManager::SetValue(arg, name);
	return 0;
}

int GUIAction::multirom_list_roms_for_swap(std::string arg)
{
	const int mask = MASK_ANDROID & (~M(ROM_INTERNAL_PRIMARY));
	DataManager::SetValue(arg, MultiROM::listRoms(mask, true));
	return 0;
}

int GUIAction::multirom_delete(std::string arg __unused)
{
	int op_status = 0;
	operation_start("Delete ROM");
	if(!MultiROM::erase(DataManager::GetStrValue("tw_multirom_rom_name")))
		op_status = 1;
	PartitionManager.Update_System_Details();
	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_flash_zip(std::string arg __unused)
{
	operation_start("Flashing");
	int op_status = 0;

	std::string name = DataManager::GetStrValue("tw_multirom_rom_name");
	std::string boot = MultiROM::getRomsPath() + name + "/boot.img";
	int had_boot = access(boot.c_str(), F_OK) >= 0;

	DataManager::SetValue("multirom_rom_name_title", 1);

	if (!MultiROM::flashZip(name, DataManager::GetStrValue("tw_filename")))
		op_status = 1;

	if(!had_boot && MultiROM::compareFiles(MultiROM::getBootDev().c_str(), boot.c_str()))
		unlink(boot.c_str());
	else if(op_status == 0)
	{
		DataManager::SetValue("tw_multirom_share_kernel", 0);
		if(!MultiROM::extractBootForROM(MultiROM::getRomsPath() + name))
			op_status = 1;
	}

	DataManager::SetValue("multirom_rom_name_title", 0);

	operation_end(op_status);
	return op_status;
}

int GUIAction::multirom_flash_zip_sailfish(std::string arg __unused)
{
	operation_start("Flashing");
	int op_status = 0;

	std::string name = DataManager::GetStrValue("tw_multirom_rom_name");
	std::string root = MultiROM::getRomsPath() + name;

	if(rename((root + "/data/.stowaways/sailfishos/system").c_str(), (root + "/system").c_str()) < 0)
		gui_print("/system move failed %s", strerror(errno));


	if (!MultiROM::flashZip(name, DataManager::GetStrValue("tw_filename")))
		op_status = 1;

	if(rename((root + "/system").c_str(), (root + "/data/.stowaways/sailfishos/system").c_str()) < 0)
		gui_print("/system move failed %s", strerror(errno));

	if(!MultiROM::sailfishProcessBoot(root))
		op_status = 1;

	if(!MultiROM::sailfishProcess(root, name))
		op_status = 1;

	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_inject(std::string arg __unused)
{
	operation_start("Injecting");
	int op_status = 0;
	std::string path = DataManager::GetStrValue("tw_filename");
	if(DataManager::GetIntValue("tw_multirom_add_bootimg"))
		op_status = MultiROM::copyBoot(path, DataManager::GetStrValue("tw_multirom_rom_name"));

	if(!op_status)
		op_status = !MultiROM::injectBoot(path);
	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_inject_curr_boot(std::string arg __unused)
{
	operation_start("Injecting");
	int op_status = !MultiROM::folderExists();
	if(op_status)
		gui_print("MultiROM is not installed!\n");
	else
		op_status = !MultiROM::injectBoot(MultiROM::getBootDev());
	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_add_rom(std::string arg __unused)
{
	operation_start("Installing");
	int op_status = !MultiROM::addROM(DataManager::GetStrValue("tw_filename"),
									  DataManager::GetIntValue("tw_multirom_type"),
									  DataManager::GetStrValue("tw_multirom_install_loc"));
	operation_end(op_status);
	return op_status;
}

int GUIAction::multirom_ubuntu_patch_init(std::string arg __unused)
{
	operation_start("Patching");
	int op_status = !MultiROM::patchInit(DataManager::GetStrValue("tw_multirom_rom_name"));
	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_touch_patch_init(std::string arg __unused)
{
	operation_start("Patching");
	int op_status = 1;
	std::string root = MultiROM::getRomsPath() + DataManager::GetStrValue("tw_multirom_rom_name") + "/";
	if(access((root + "/boot.img").c_str(), F_OK) >= 0)
	{
		std::string type;
		if(access((root + "/data/system.img").c_str(), F_OK) >= 0)
			type = "ubuntu-touch-sysimage-init";
		else
			type = "ubuntu-touch-init";

		gui_print("Patching ubuntu with %s\n", type.c_str());
		op_status = !MultiROM::ubuntuTouchProcessBoot(root, type.c_str());
		if(op_status == 0)
			op_status = !MultiROM::ubuntuTouchProcess(root, DataManager::GetStrValue("tw_multirom_rom_name"));
	}
	else
		LOGERR("This ubuntu installation does not have boot.img, it can't be patched.\n");
	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_wipe(std::string arg __unused)
{
	operation_start("Wiping");
	int op_status = !MultiROM::wipe(DataManager::GetStrValue("tw_multirom_rom_name"),
									DataManager::GetStrValue("tw_multirom_wipe"));
	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_disable_flash_kernel(std::string arg __unused)
{
	operation_start("working");
	int op_status = !MultiROM::disableFlashKernelAct(DataManager::GetStrValue("tw_multirom_rom_name"),
													 DataManager::GetStrValue("tw_multirom_install_loc"));
	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_rm_bootimg(std::string arg __unused)
{
	operation_start("working");
	std::string cmd = "rm \"" + MultiROM::getRomsPath() + "/" + DataManager::GetStrValue("tw_multirom_rom_name") + "/boot.img\"";
	int op_status = (system(cmd.c_str()) != 0);

	operation_end(op_status);
	return 0;
}

int GUIAction::multirom_backup_rom(std::string arg __unused)
{
	operation_start("Changing mountpoints for backup");
	int op_status = !MultiROM::initBackup(DataManager::GetStrValue("tw_multirom_rom_name"));
	operation_end(op_status);
	if(op_status == 0)
		return gui_changePage("backup");
	else
	{
		DataManager::SetValue("tw_mrom_title", "Failed to prepare ROM for backup!");
		DataManager::SetValue("tw_mrom_text1", "See /tmp/recovery.log for more details.");
		DataManager::SetValue("tw_mrom_text2", "");
		DataManager::SetValue("tw_mrom_back", "multirom_manage");
		return gui_changePage("multirom_msg");
	}
}

int GUIAction::multirom_sideload(std::string arg __unused)
{
	int ret = 0;

	operation_start("Sideload");
	bool mtp_was_enabled = TWFunc::Toggle_MTP(false);

	if(DataManager::GetStrValue("tw_back") == "multirom_manage")
		DataManager::SetValue("multirom_rom_name_title", 1);

	gui_print("Starting ADB sideload feature...\n");
	ret = apply_from_adb("/", &sideload_child_pid);
	DataManager::SetValue("tw_has_cancel", 0); // Remove cancel button from gui now that the zip install is going to start
	if (ret != 0) {
		if (ret == -2)
			gui_print("You need adb 1.0.32 or newer to sideload to this device.\n");
	} else {
		DataManager::SetValue("tw_filename", FUSE_SIDELOAD_HOST_PATHNAME);
		DataManager::SetValue("tw_mrom_sideloaded", 1);

		if(DataManager::GetStrValue("tw_back") == "multirom_add") {
			ret = multirom_add_rom("");
		} else if(DataManager::GetStrValue("tw_back") == "multirom_manage") {
			ret = multirom_flash_zip("");
			DataManager::SetValue("tw_back", "multirom_list");
		}
	}

	if (sideload_child_pid) {
		LOGINFO("Signaling child sideload process to exit.\n");
		struct stat st;
		// Calling stat() on this magic filename signals the minadbd
		// subprocess to shut down.
		stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &st);
		int status;
		LOGINFO("Waiting for child sideload process to exit.\n");
		waitpid(sideload_child_pid, &status, 0);
	}

	TWFunc::Toggle_MTP(mtp_was_enabled);
	reinject_after_flash();
	operation_end(ret);

	DataManager::SetValue("multirom_rom_name_title", 0);
	return 0;
}

int GUIAction::multirom_swap_calc_space(std::string arg __unused)
{
	static const char *parts[] = { "/cache", "/system", "/data" };
	TWPartition *p;
	unsigned long long int_size = 0, int_data_size = 0;
	unsigned long long needed = 0, free = 0;

	std::string swap_rom = DataManager::GetStrValue("tw_multirom_swap_rom");
	int type = DataManager::GetIntValue("tw_multirom_swap_type");

	operation_start("CalcSpace");

	PartitionManager.Update_System_Details();

	p = PartitionManager.Find_Partition_By_Path(MultiROM::getRomsPath());
	if(!p)
	{
		LOGINFO("multirom_swap_calc_space: failed to find partition for ROMs!\n");
		operation_end(1);
		return 0;
	}

	free = p->GetSizeFree();

	for(size_t i = 0; i < sizeof(parts)/sizeof(parts[0]); ++i)
	{
		p = PartitionManager.Find_Partition_By_Path(parts[i]);
		if(!p)
		{
			LOGINFO("multirom_swap_calc_space: failed to get %s!\n", parts[i]);
			operation_end(1);
			return 0;
		}

		int_size += p->GetSizeBackup();
		if(strcmp("/data", parts[i]) == 0)
			int_data_size = p->GetSizeBackup();
	}

	TWExclude twe;
	switch(type)
	{
		case MROM_SWAP_WITH_SECONDARY:
			needed = int_size + twe.Get_Folder_Size(MultiROM::getRomsPath() + swap_rom + "/data");
			break;
		case MROM_SWAP_COPY_SECONDARY:
		{
			uint64_t sec_data_size = twe.Get_Folder_Size(MultiROM::getRomsPath() + swap_rom + "/data");
			if(sec_data_size > int_data_size)
				needed = sec_data_size - int_data_size + 50*1024*1024;
			break;
		}
		case MROM_SWAP_COPY_INTERNAL:
		case MROM_SWAP_MOVE_INTERNAL:
			needed = twe.Get_Folder_Size(MultiROM::getRomsPath() + swap_rom + "/data");
			break;
		case MROM_SWAP_DUPLICATE:
			needed = twe.Get_Folder_Size(MultiROM::getRomsPath() + swap_rom);
			break;
	}

	needed /= 1024*1024;
	free /= 1024*1024;
	DataManager::SetValue("tw_multirom_swap_needed", needed);
	DataManager::SetValue("tw_multirom_swap_free", free);

	LOGINFO("multirom_swap_calc_space: needed: %llu MB, free: %llu MB\n", needed, free);

	operation_end(0);

	if(needed >= free)
	{
		DataManager::SetValue("tw_multirom_swap_calculating", 0);
		gui_changeOverlay("multirom_swap_space_info");
	}
	else
	{
		::sleep(1); // give the user chance to read the overlay
		gui_changeOverlay("");
		gui_changePage("action_page");
	}
	return 0;
}

int GUIAction::multirom_execute_swap(std::string arg __unused)
{
	operation_start("SwapROMs");

	int res = 1;
	int type = DataManager::GetIntValue("tw_multirom_swap_type");
	std::string int_target = DataManager::GetStrValue("tw_multirom_swap_internal_name");

	switch(type)
	{
		case MROM_SWAP_WITH_SECONDARY:
		{
			std::string src_rom = DataManager::GetStrValue("tw_multirom_swap_rom");

			if(!MultiROM::copyInternal(int_target))
				break;

			if(!MultiROM::wipeInternal())
				break;

			if(!MultiROM::copySecondaryToInternal(src_rom))
				break;

			if(!MultiROM::erase(src_rom))
				break;

			res = 0;
			break;
		}
		case MROM_SWAP_COPY_SECONDARY:
		{
			std::string src_rom = DataManager::GetStrValue("tw_multirom_swap_rom");

			if(!MultiROM::wipeInternal())
				break;

			if(!MultiROM::copySecondaryToInternal(src_rom))
				break;

			res = 0;
			break;
		}
		case MROM_SWAP_COPY_INTERNAL:
			if(MultiROM::copyInternal(int_target))
				res = 0;
			break;
		case MROM_SWAP_MOVE_INTERNAL:
		{
			if(!MultiROM::copyInternal(int_target))
				break;

			if(!MultiROM::wipeInternal())
				break;

			res = 0;
			break;
		}
		case MROM_SWAP_DUPLICATE:
		{
			std::string src_rom = DataManager::GetStrValue("tw_multirom_swap_rom");
			if(!MultiROM::duplicateSecondary(src_rom, int_target))
				break;
			res = 0;
			break;
		}
	}

	PartitionManager.Update_System_Details();

	operation_end(res);
	return 0;
}

int GUIAction::multirom_set_fw(std::string arg __unused)
{
	operation_start("CopyFW");

	std::string src = DataManager::GetStrValue("tw_filename");
	std::string dst = MultiROM::getRomsPath() + DataManager::GetStrValue("tw_multirom_rom_name") + "/firmware.img";

	gui_print("Setting ROM's radio.img to %s", src.c_str());
	int res = TWFunc::copy_file(src, dst, 0755) == 0 ? 0 : 1;

	DataManager::SetValue("tw_multirom_has_fw_image", int(access(dst.c_str(), F_OK) >= 0));

	operation_end(res);
	return 0;
}

int GUIAction::multirom_remove_fw(std::string arg __unused)
{
	operation_start("RemoveFW");

	gui_print("Removing ROM's radio.img...");
	std::string dst = MultiROM::getRomsPath() + DataManager::GetStrValue("tw_multirom_rom_name") + "/firmware.img";
	int res = remove(dst.c_str()) >= 0 ? 0 : 1;
	DataManager::SetValue("tw_multirom_has_fw_image", int(access(dst.c_str(), F_OK) >= 0));

	operation_end(res);
	return 0;
}

int GUIAction::multirom_restorecon(std::string arg __unused)
{
	operation_start("restorecon");
	int res = MultiROM::restorecon(DataManager::GetStrValue("tw_multirom_rom_name")) ? 0 : -1;
	operation_end(res);
	return 0;
}

int GUIAction::system_image_upgrader(std::string arg __unused)
{
	operation_start("system-image-upgrader");

	int res = 0;

	if(TWFunc::Path_Exists(UBUNTU_COMMAND_FILE))
	{
		gui_print("\n");
		res = TWFunc::Exec_Cmd_Show_Output("system-image-upgrader " UBUNTU_COMMAND_FILE);
		gui_print("\n");

		if(res != 0)
		{
			gui_print("system-image-upgrader failed\n");
			res = 1;
		}
		DataManager::SetValue("system-image-upgrader-res", res);
	} else
		gui_print("Could not find system-image-upgrader command file: " UBUNTU_COMMAND_FILE"\n");

	DataManager::SetValue("tw_page_done", 1);
	operation_end(res);

	return 0;
}
#endif //TARGET_RECOVERY_IS_MULTIROM

int GUIAction::mountsystemtoggle(std::string arg)
{
	int op_status = 0;
	bool remount_system = PartitionManager.Is_Mounted_By_Path("/system");
	bool remount_vendor = PartitionManager.Is_Mounted_By_Path("/vendor");

	operation_start("Toggle System Mount");
	if (!PartitionManager.UnMount_By_Path("/system", true)) {
		op_status = 1; // fail
	} else {
		TWPartition* Part = PartitionManager.Find_Partition_By_Path("/system");
		if (Part) {
			if (arg == "0") {
				DataManager::SetValue("tw_mount_system_ro", 0);
				Part->Change_Mount_Read_Only(false);
			} else {
				DataManager::SetValue("tw_mount_system_ro", 1);
				Part->Change_Mount_Read_Only(true);
			}
			if (remount_system) {
				Part->Mount(true);
			}
			op_status = 0; // success
		} else {
			op_status = 1; // fail
		}
		Part = PartitionManager.Find_Partition_By_Path("/vendor");
		if (Part) {
			if (arg == "0") {
				Part->Change_Mount_Read_Only(false);
			} else {
				Part->Change_Mount_Read_Only(true);
			}
			if (remount_vendor) {
				Part->Mount(true);
			}
			op_status = 0; // success
		} else {
			op_status = 1; // fail
		}
	}

	operation_end(op_status);
	return 0;
}

int GUIAction::setlanguage(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Set Language");
	PageManager::LoadLanguage(DataManager::GetStrValue("tw_language"));
	PageManager::RequestReload();
	op_status = 0; // success

	operation_end(op_status);
	return 0;
}

int GUIAction::togglebacklight(std::string arg __unused)
{
	blankTimer.toggleBlank();
	return 0;
}

int GUIAction::setbootslot(std::string arg)
{
	operation_start("Set Boot Slot");
	if (!simulate)
		PartitionManager.Set_Active_Slot(arg);
	else
		simulate_progress_bar();
	operation_end(0);
	return 0;
}

int GUIAction::checkforapp(std::string arg __unused)
{
	operation_start("Check for TWRP App");
	if (!simulate)
	{
		string sdkverstr = TWFunc::System_Property_Get("ro.build.version.sdk");
		int sdkver = 0;
		if (!sdkverstr.empty()) {
			sdkver = atoi(sdkverstr.c_str());
		}
		if (sdkver <= 13) {
			if (sdkver == 0)
				LOGINFO("Unable to read sdk version from build prop\n");
			else
				LOGINFO("SDK version too low for TWRP app (%i < 14)\n", sdkver);
			DataManager::SetValue("tw_app_install_status", 1); // 0 = no status, 1 = not installed, 2 = already installed or do not install
			goto exit;
		}
		if (PartitionManager.Mount_By_Path("/system", false)) {
			string base_path = "/system";
			if (TWFunc::Path_Exists("/system/system"))
				base_path += "/system"; // For devices with system as a root file system (e.g. Pixel)
			string install_path = base_path + "/priv-app";
			if (!TWFunc::Path_Exists(install_path))
				install_path = base_path + "/app";
			install_path += "/twrpapp";
			if (TWFunc::Path_Exists(install_path)) {
				LOGINFO("App found at '%s'\n", install_path.c_str());
				DataManager::SetValue("tw_app_install_status", 2); // 0 = no status, 1 = not installed, 2 = already installed or do not install
				goto exit;
			}
		}
		if (PartitionManager.Mount_By_Path("/data", false)) {
			const char parent_path[] = "/data/app";
			const char app_prefix[] = "me.twrp.twrpapp-";
			DIR *d = opendir(parent_path);
			if (d) {
				struct dirent *p;
				while ((p = readdir(d))) {
					if (p->d_type != DT_DIR || strlen(p->d_name) < strlen(app_prefix) || strncmp(p->d_name, app_prefix, strlen(app_prefix)))
						continue;
					closedir(d);
					LOGINFO("App found at '%s/%s'\n", parent_path, p->d_name);
					DataManager::SetValue("tw_app_install_status", 2); // 0 = no status, 1 = not installed, 2 = already installed or do not install
					goto exit;
				}
				closedir(d);
			}
		} else {
			LOGINFO("Data partition cannot be mounted during app check\n");
			DataManager::SetValue("tw_app_install_status", 2); // 0 = no status, 1 = not installed, 2 = already installed or do not install
		}
	} else
		simulate_progress_bar();
	LOGINFO("App not installed\n");
	DataManager::SetValue("tw_app_install_status", 1); // 0 = no status, 1 = not installed, 2 = already installed
exit:
	operation_end(0);
	return 0;
}

int GUIAction::installapp(std::string arg __unused)
{
	int op_status = 1;
	operation_start("Install TWRP App");
	if (!simulate)
	{
		if (DataManager::GetIntValue("tw_mount_system_ro") > 0 || DataManager::GetIntValue("tw_app_install_system") == 0) {
			if (PartitionManager.Mount_By_Path("/data", true)) {
				string install_path = "/data/app";
				string context = "u:object_r:apk_data_file:s0";
				if (!TWFunc::Path_Exists(install_path)) {
					if (mkdir(install_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) {
						LOGERR("Error making %s directory: %s\n", install_path.c_str(), strerror(errno));
						goto exit;
					}
					if (chown(install_path.c_str(), 1000, 1000)) {
						LOGERR("chown %s error: %s\n", install_path.c_str(), strerror(errno));
						goto exit;
					}
					if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
						LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
						goto exit;
					}
				}
				install_path += "/me.twrp.twrpapp-1";
				if (mkdir(install_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) {
					LOGERR("Error making %s directory: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				if (chown(install_path.c_str(), 1000, 1000)) {
					LOGERR("chown %s error: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
					LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				install_path += "/base.apk";
				if (TWFunc::copy_file("/sbin/me.twrp.twrpapp.apk", install_path, 0644)) {
					LOGERR("Error copying apk file\n");
					goto exit;
				}
				if (chown(install_path.c_str(), 1000, 1000)) {
					LOGERR("chown %s error: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
					LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
					goto exit;
				}
				sync();
				sync();
			}
		} else {
			if (PartitionManager.Mount_By_Path("/system", true)) {
				string base_path = "/system";
				if (TWFunc::Path_Exists("/system/system"))
					base_path += "/system"; // For devices with system as a root file system (e.g. Pixel)
				string install_path = base_path + "/priv-app";
				string context = "u:object_r:system_file:s0";
				if (!TWFunc::Path_Exists(install_path))
					install_path = base_path + "/app";
				if (TWFunc::Path_Exists(install_path)) {
					install_path += "/twrpapp";
					LOGINFO("Installing app to '%s'\n", install_path.c_str());
					if (mkdir(install_path.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0) {
						if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
							LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
							goto exit;
						}
						install_path += "/me.twrp.twrpapp.apk";
						if (TWFunc::copy_file("/sbin/me.twrp.twrpapp.apk", install_path, 0644)) {
							LOGERR("Error copying apk file\n");
							goto exit;
						}
						if (setfilecon(install_path.c_str(), (security_context_t)context.c_str()) < 0) {
							LOGERR("setfilecon %s error: %s\n", install_path.c_str(), strerror(errno));
							goto exit;
						}
						sync();
						sync();
						PartitionManager.UnMount_By_Path("/system", true);
						op_status = 0;
					} else {
						LOGERR("Error making app directory '%s': %s\n", strerror(errno));
					}
				}
			}
		}
	} else
		simulate_progress_bar();
exit:
	operation_end(0);
	return 0;
}
