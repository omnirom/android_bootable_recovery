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
#include <pwd.h>

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
#include "../minuitwrp/minui.h"
#include "../variables.h"
#include "../twinstall.h"
#include "cutils/properties.h"
#include "../adb_install.h"
#include "../set_metadata.h"
};

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../tw_atomic.hpp"

void curtainClose(void);

GUIAction::mapFunc GUIAction::mf;
std::set<string> GUIAction::setActionsRunningInCallerThread;
static string zip_queue[10];
static int zip_queue_index;
static pthread_t terminal_command;
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
	if(m_thread_running) {
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
		ADD_ACTION(setlanguage);

		// remember actions that run in the caller thread
		for (mapFunc::const_iterator it = mf.begin(); it != mf.end(); ++it)
			setActionsRunningInCallerThread.insert(it->first);

		// These actions will run in a separate thread
		ADD_ACTION(flash);
		ADD_ACTION(wipe);
		ADD_ACTION(refreshsizes);
		ADD_ACTION(nandroid);
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
		ADD_ACTION(twcmd);
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
			for(size_t i = 0; i < keys.size(); ++i)
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

int GUIAction::NotifyTouch(TOUCH_STATE state __unused, int x __unused, int y __unused)
{
	if (state == TOUCH_RELEASE)
		doActions();

	return 0;
}

int GUIAction::NotifyKey(int key, bool down)
{
	if (mKeys.empty())
		return 0;

	std::map<int, bool>::iterator itr = mKeys.find(key);
	if(itr == mKeys.end())
		return 0;

	bool prevState = itr->second;
	itr->second = down;

	// If there is only one key for this action, wait for key up so it
	// doesn't trigger with multi-key actions.
	// Else, check if all buttons are pressed, then consume their release events
	// so they don't trigger one-button actions and reset mKeys pressed status
	if(mKeys.size() == 1) {
		if(!down && prevState)
			doActions();
	} else if(down) {
		for(itr = mKeys.begin(); itr != mKeys.end(); ++itr) {
			if(!itr->second)
				return 0;
		}

		// Passed, all req buttons are pressed, reset them and consume release events
		HardwareKeyboard *kb = PageManager::GetHardwareKeyboard();
		for(itr = mKeys.begin(); itr != mKeys.end(); ++itr) {
			kb->ConsumeKeyRelease(itr->first);
			itr->second = false;
		}

		doActions();
	}

	return 0;
}

int GUIAction::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIObject::NotifyVarChange(varName, value);

	if (varName.empty() && !isConditionValid() && mKeys.empty() && !mActionW)
		doActions();
	else if((varName.empty() || IsConditionVariable(varName)) && isConditionValid() && isConditionTrue())
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
			gui_msg("backup_cancel=Backup Canceled.");
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

	if (!PartitionManager.Mount_By_Path(filename, true))
		return -1;

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
	//curtainClose(); this sometimes causes a crash

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
		string dst;
		PartitionManager.Mount_Current_Storage(true);
		dst = DataManager::GetCurrentStoragePath() + "/recovery.log";
		TWFunc::copy_file("/tmp/recovery.log", dst.c_str(), 0755);
		tw_set_default_metadata(dst.c_str());
		sync();
		gui_msg(Msg("copy_log=Copied recovery log to {1}.")(DataManager::GetCurrentStoragePath()));
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

		if(divide_by != 0)
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

int GUIAction::appenddatetobackupname(std::string arg __unused)
{
	operation_start("AppendDateToBackupName");
	string Backup_Name;
	DataManager::GetValue(TW_BACKUP_NAME, Backup_Name);
	Backup_Name += TWFunc::Get_Current_Date();
	if (Backup_Name.size() > MAX_BACKUP_NAME_LEN)
		Backup_Name.resize(MAX_BACKUP_NAME_LEN);
	DataManager::SetValue(TW_BACKUP_NAME, Backup_Name);
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

int GUIAction::checkpartitionlist(std::string arg __unused)
{
	string Wipe_List, wipe_path;
	int count = 0;

	DataManager::GetValue("tw_wipe_list", Wipe_List);
	LOGINFO("checkpartitionlist list '%s'\n", Wipe_List.c_str());
	if (!Wipe_List.empty()) {
		size_t start_pos = 0, end_pos = Wipe_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Wipe_List.size()) {
			wipe_path = Wipe_List.substr(start_pos, end_pos - start_pos);
			LOGINFO("checkpartitionlist wipe_path '%s'\n", wipe_path.c_str());
			if (wipe_path == "/and-sec" || wipe_path == "DALVIK" || wipe_path == "INTERNAL") {
				// Do nothing
			} else {
				count++;
			}
			start_pos = end_pos + 1;
			end_pos = Wipe_List.find(";", start_pos);
		}
		DataManager::SetValue("tw_check_partition_list", count);
	} else {
		DataManager::SetValue("tw_check_partition_list", 0);
	}
		return 0;
}

int GUIAction::getpartitiondetails(std::string arg __unused)
{
	string Wipe_List, wipe_path;
	int count = 0;

	DataManager::GetValue("tw_wipe_list", Wipe_List);
	LOGINFO("getpartitiondetails list '%s'\n", Wipe_List.c_str());
	if (!Wipe_List.empty()) {
		size_t start_pos = 0, end_pos = Wipe_List.find(";", start_pos);
		while (end_pos != string::npos && start_pos < Wipe_List.size()) {
			wipe_path = Wipe_List.substr(start_pos, end_pos - start_pos);
			LOGINFO("getpartitiondetails wipe_path '%s'\n", wipe_path.c_str());
			if (wipe_path == "/and-sec" || wipe_path == "DALVIK" || wipe_path == "INTERNAL") {
				// Do nothing
			} else {
				DataManager::SetValue("tw_partition_path", wipe_path);
				break;
			}
			start_pos = end_pos + 1;
			end_pos = Wipe_List.find(";", start_pos);
		}
		if (!wipe_path.empty()) {
			TWPartition* Part = PartitionManager.Find_Partition_By_Path(wipe_path);
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
				LOGERR("Unable to locate partition: '%s'\n", wipe_path.c_str());
			}
		}
	}
	DataManager::SetValue("tw_partition_name", "");
	DataManager::SetValue("tw_partition_file_system", "");
	return 0;
}

int GUIAction::screenshot(std::string arg __unused)
{
	time_t tm;
	char path[256];
	int path_len;
	uid_t uid = -1;
	gid_t gid = -1;

	struct passwd *pwd = getpwnam("media_rw");
	if(pwd) {
		uid = pwd->pw_uid;
		gid = pwd->pw_gid;
	}

	const std::string storage = DataManager::GetCurrentStoragePath();
	if(PartitionManager.Is_Mounted_By_Path(storage)) {
		snprintf(path, sizeof(path), "%s/Pictures/Screenshots/", storage.c_str());
	} else {
		strcpy(path, "/tmp/");
	}

	if(!TWFunc::Create_Dir_Recursive(path, 0666, uid, gid))
		return 0;

	tm = time(NULL);
	path_len = strlen(path);

	// Screenshot_2014-01-01-18-21-38.png
	strftime(path+path_len, sizeof(path)-path_len, "Screenshot_%Y-%m-%d-%H-%M-%S.png", localtime(&tm));

	int res = gr_save_screenshot(path);
	if(res == 0) {
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
			int has_datamedia, dual_storage;

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
			TWPartition* wipe_part = NULL;

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
			string auto_gen = gui_lookup("auto_gen", "(Auto Generate)");
			string curr_date = gui_lookup("curr_date", "(Current Date)");
			if (Backup_Name == "(Auto Generate)" || Backup_Name == "(Current Date)" || Backup_Name == "0" || Backup_Name == "(" || PartitionManager.Check_Backup_Name(true) == 0) {
				ret = PartitionManager.Run_Backup();
			}
			else {
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
			gui_msg("backup_cancel=Backup Canceled.");
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

int GUIAction::fixpermissions(std::string arg __unused)
{
	int op_status = 0;

	operation_start("Fix Permissions");
	LOGINFO("fix permissions started!\n");
	if (simulate) {
		simulate_progress_bar();
	} else {
		op_status = PartitionManager.Fix_Permissions();
		if (op_status != 0)
			op_status = 1; // failure
	}
	operation_end(op_status);
	return 0;
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
			int fd = fileno(fp), has_data = 0, check = 0, keep_going = -1, bytes_read = 0;
			struct timeval timeout;
			fd_set fdset;

			while(keep_going)
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
					if(fgets(line, sizeof(line), fp) != NULL)
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
	int op_status = 0;

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
	string path, filename, full_filename;
	DataManager::GetValue("tw_zip_location", path);
	DataManager::GetValue("tw_file", filename);
	full_filename = path + "/" + filename;
	if (PartitionManager.Flash_Image(full_filename))
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
	if (key == "home")			return KEY_HOME;
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
