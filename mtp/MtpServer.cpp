/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include "../twcommon.h"
#include "../set_metadata.h"
#include <cutils/properties.h>

#include "MtpTypes.h"
#include "MtpDebug.h"
#include "MtpDatabase.h"
#include "MtpObjectInfo.h"
#include "MtpProperty.h"
#include "MtpServer.h"
#include "MtpStorage.h"
#include "MtpStringBuffer.h"

#include <linux/usb/f_mtp.h>

static const MtpOperationCode kSupportedOperationCodes[] = {
	MTP_OPERATION_GET_DEVICE_INFO,
	MTP_OPERATION_OPEN_SESSION,
	MTP_OPERATION_CLOSE_SESSION,
	MTP_OPERATION_GET_STORAGE_IDS,
	MTP_OPERATION_GET_STORAGE_INFO,
	MTP_OPERATION_GET_NUM_OBJECTS,
	MTP_OPERATION_GET_OBJECT_HANDLES,
	MTP_OPERATION_GET_OBJECT_INFO,
	MTP_OPERATION_GET_OBJECT,
	MTP_OPERATION_GET_THUMB,
	MTP_OPERATION_DELETE_OBJECT,
	MTP_OPERATION_SEND_OBJECT_INFO,
	MTP_OPERATION_SEND_OBJECT,
//	MTP_OPERATION_INITIATE_CAPTURE,
//	MTP_OPERATION_FORMAT_STORE,
//	MTP_OPERATION_RESET_DEVICE,
//	MTP_OPERATION_SELF_TEST,
//	MTP_OPERATION_SET_OBJECT_PROTECTION,
//	MTP_OPERATION_POWER_DOWN,
	MTP_OPERATION_GET_DEVICE_PROP_DESC,
	MTP_OPERATION_GET_DEVICE_PROP_VALUE,
	MTP_OPERATION_SET_DEVICE_PROP_VALUE,
	MTP_OPERATION_RESET_DEVICE_PROP_VALUE,
//	MTP_OPERATION_TERMINATE_OPEN_CAPTURE,
//	MTP_OPERATION_MOVE_OBJECT,
//	MTP_OPERATION_COPY_OBJECT,
	MTP_OPERATION_GET_PARTIAL_OBJECT,
//	MTP_OPERATION_INITIATE_OPEN_CAPTURE,
	MTP_OPERATION_GET_OBJECT_PROPS_SUPPORTED,
	MTP_OPERATION_GET_OBJECT_PROP_DESC,
	MTP_OPERATION_GET_OBJECT_PROP_VALUE,
	MTP_OPERATION_SET_OBJECT_PROP_VALUE,
	MTP_OPERATION_GET_OBJECT_PROP_LIST,
//	MTP_OPERATION_SET_OBJECT_PROP_LIST,
//	MTP_OPERATION_GET_INTERDEPENDENT_PROP_DESC,
//	MTP_OPERATION_SEND_OBJECT_PROP_LIST,
	MTP_OPERATION_GET_OBJECT_REFERENCES,
	MTP_OPERATION_SET_OBJECT_REFERENCES,
//	MTP_OPERATION_SKIP,
	// Android extension for direct file IO
	MTP_OPERATION_GET_PARTIAL_OBJECT_64,
	MTP_OPERATION_SEND_PARTIAL_OBJECT,
	MTP_OPERATION_TRUNCATE_OBJECT,
	MTP_OPERATION_BEGIN_EDIT_OBJECT,
	MTP_OPERATION_END_EDIT_OBJECT,
};

static const MtpEventCode kSupportedEventCodes[] = {
	MTP_EVENT_OBJECT_ADDED,
	MTP_EVENT_OBJECT_REMOVED,
	MTP_EVENT_STORE_ADDED,
	MTP_EVENT_STORE_REMOVED,
	MTP_EVENT_OBJECT_PROP_CHANGED,
};

MtpServer::MtpServer(MtpDatabase* database, bool ptp,
					int fileGroup, int filePerm, int directoryPerm)
	:	mDatabase(database),
		mPtp(ptp),
		mFileGroup(fileGroup),
		mFilePermission(filePerm),
		mDirectoryPermission(directoryPerm),
		mSessionID(0),
		mSessionOpen(false),
		mSendObjectHandle(kInvalidObjectHandle),
		mSendObjectFormat(0),
		mSendObjectFileSize(0)
{
	mFD = -1;
}

MtpServer::~MtpServer() {
}

void MtpServer::addStorage(MtpStorage* storage) {
	android::Mutex::Autolock autoLock(mMutex);
	MTPD("addStorage(): storage: %x\n", storage);
	if (getStorage(storage->getStorageID()) != NULL) {
		MTPE("MtpServer::addStorage Storage for storage ID %i already exists.\n", storage->getStorageID());
		return;
	}
	mDatabase->createDB(storage, storage->getStorageID());
	mStorages.push(storage);
	sendStoreAdded(storage->getStorageID());
}

void MtpServer::removeStorage(MtpStorage* storage) {
	android::Mutex::Autolock autoLock(mMutex);

	for (size_t i = 0; i < mStorages.size(); i++) {
		if (mStorages[i] == storage) {
			MTPD("MtpServer::removeStorage calling sendStoreRemoved\n");
			// First lock the mutex so that the inotify thread and main
			// thread do not do anything while we remove the storage
			// item, and to make sure we don't remove the item while an
			// operation is in progress
			mDatabase->lockMutex();
			// Grab the storage ID before we delete the item from the
			// database
			MtpStorageID storageID = storage->getStorageID();
			// Remove the item from the mStorages from the vector. At
			// this point the main thread will no longer be able to find
			// this storage item anymore.
			mStorages.removeAt(i);
			// Destroy the storage item, free up all the memory, kill
			// the inotify thread.
			mDatabase->destroyDB(storageID);
			// Tell the host OS that the storage item is gone.
			sendStoreRemoved(storageID);
			// Unlock any remaining mutexes on other storage devices.
			// If no storage devices exist anymore this will do nothing.
			mDatabase->unlockMutex();
			break;
		}
	}
	MTPD("MtpServer::removeStorage DONE\n");
}

MtpStorage* MtpServer::getStorage(MtpStorageID id) {
	MTPD("getStorage\n");
	if (id == 0) {
		MTPD("mStorages\n");
		return mStorages[0];
	}
	for (size_t i = 0; i < mStorages.size(); i++) {
		MtpStorage* storage = mStorages[i];
		MTPD("id: %d\n", id);
		MTPD("storage: %d\n", storage->getStorageID());
		if (storage->getStorageID() == id) {
			return storage;
		}
	}
	return NULL;
}

bool MtpServer::hasStorage(MtpStorageID id) {
	MTPD("in hasStorage\n");
	if (id == 0 || id == 0xFFFFFFFF)
		return mStorages.size() > 0;
	return (getStorage(id) != NULL);
}

void MtpServer::run(int fd) {
	if (fd < 0)
		return;

	mFD = fd;
	MTPI("MtpServer::run fd: %d\n", fd);

	while (1) {
		MTPD("About to read device...\n");
		int ret = mRequest.read(fd);
		if (ret < 0) {
			if (errno == ECANCELED) {
				// return to top of loop and wait for next command
				MTPD("request read returned %d ECANCELED, starting over\n", ret);
				continue;
			}
			MTPE("request read returned %d, errno: %d, exiting MtpServer::run loop\n", ret, errno);
			break;
		}
		MtpOperationCode operation = mRequest.getOperationCode();
		MtpTransactionID transaction = mRequest.getTransactionID();

		MTPD("operation: %s", MtpDebug::getOperationCodeName(operation));
		mRequest.dump();

		// FIXME need to generalize this
		bool dataIn = (operation == MTP_OPERATION_SEND_OBJECT_INFO
					|| operation == MTP_OPERATION_SET_OBJECT_REFERENCES
					|| operation == MTP_OPERATION_SET_OBJECT_PROP_VALUE
					|| operation == MTP_OPERATION_SET_DEVICE_PROP_VALUE);
		if (dataIn) {
			int ret = mData.read(fd);
			if (ret < 0) {
				if (errno == ECANCELED) {
					// return to top of loop and wait for next command
					MTPD("data read returned %d ECANCELED, starting over\n", ret);
					continue;
				}
				MTPD("data read returned %d, errno: %d, exiting MtpServer::run loop\n", ret, errno);
				break;
			}
			MTPD("received data:");
			mData.dump();
		} else {
			mData.reset();
		}

		if (handleRequest()) {
			if (!dataIn && mData.hasData()) {
				mData.setOperationCode(operation);
				mData.setTransactionID(transaction);
				MTPD("sending data:");
				mData.dump();
				ret = mData.write(fd);
				if (ret < 0) {
					if (errno == ECANCELED) {
						// return to top of loop and wait for next command
						MTPD("data write returned %d ECANCELED, starting over\n", ret);
						continue;
					}
					MTPE("data write returned %d, errno: %d, exiting MtpServer::run loop\n", ret, errno);
					break;
				}
			}

			mResponse.setTransactionID(transaction);
			MTPD("sending response %04X\n", mResponse.getResponseCode());
			ret = mResponse.write(fd);
			MTPD("ret: %d\n", ret);
			mResponse.dump();
			if (ret < 0) {
				if (errno == ECANCELED) {
					// return to top of loop and wait for next command
					MTPD("response write returned %d ECANCELED, starting over\n", ret);
					continue;
				}
				MTPE("response write returned %d, errno: %d, exiting MtpServer::run loop\n", ret, errno);
				break;
			}
		} else {
			MTPD("skipping response\n");
		}
	}

	// commit any open edits
	int count = mObjectEditList.size();
	for (int i = 0; i < count; i++) {
		ObjectEdit* edit = mObjectEditList[i];
		commitEdit(edit);
		delete edit;
	}
	mObjectEditList.clear();

	if (mSessionOpen)
		mDatabase->sessionEnded(); // This doesn't actually do anything but was carry over from AOSP
	close(fd);
	mFD = -1;
}

void MtpServer::sendObjectAdded(MtpObjectHandle handle) {
	MTPD("sendObjectAdded %d\n", handle);
	sendEvent(MTP_EVENT_OBJECT_ADDED, handle);
}

void MtpServer::sendObjectRemoved(MtpObjectHandle handle) {
	MTPD("sendObjectRemoved %d\n", handle);
	sendEvent(MTP_EVENT_OBJECT_REMOVED, handle);
}

void MtpServer::sendObjectUpdated(MtpObjectHandle handle) {
	MTPD("sendObjectUpdated %d\n", handle);
	sendEvent(MTP_EVENT_OBJECT_PROP_CHANGED, handle);
}

void MtpServer::sendStoreAdded(MtpStorageID id) {
	MTPD("sendStoreAdded %08X\n", id);
	sendEvent(MTP_EVENT_STORE_ADDED, id);
}

void MtpServer::sendStoreRemoved(MtpStorageID id) {
	MTPD("sendStoreRemoved %08X\n", id);
	sendEvent(MTP_EVENT_STORE_REMOVED, id);
	MTPD("MtpServer::sendStoreRemoved done\n");
}

void MtpServer::sendEvent(MtpEventCode code, uint32_t param1) {
	MTPD("MtpServer::sendEvent sending event code: %x\n", code);
	if (mSessionOpen) {
		mEvent.setEventCode(code);
		mEvent.setTransactionID(mRequest.getTransactionID());
		mEvent.setParameter(1, param1);
		int ret = mEvent.write(mFD);
		MTPD("mEvent.write returned %d\n", ret);
	}
}

void MtpServer::addEditObject(MtpObjectHandle handle, MtpString& path,
		uint64_t size, MtpObjectFormat format, int fd) {
	ObjectEdit*  edit = new ObjectEdit(handle, path, size, format, fd);
	mObjectEditList.add(edit);
}

MtpServer::ObjectEdit* MtpServer::getEditObject(MtpObjectHandle handle) {
	int count = mObjectEditList.size();
	for (int i = 0; i < count; i++) {
		ObjectEdit* edit = mObjectEditList[i];
		if (edit->mHandle == handle) return edit;
	}
	return NULL;
}

void MtpServer::removeEditObject(MtpObjectHandle handle) {
	int count = mObjectEditList.size();
	for (int i = 0; i < count; i++) {
		ObjectEdit* edit = mObjectEditList[i];
		if (edit->mHandle == handle) {
			delete edit;
			mObjectEditList.removeAt(i);
			return;
		}
	}
	MTPE("ObjectEdit not found in removeEditObject");
}

void MtpServer::commitEdit(ObjectEdit* edit) {
	mDatabase->endSendObject((const char *)edit->mPath, edit->mHandle, edit->mFormat, true);
}


bool MtpServer::handleRequest() {
	android::Mutex::Autolock autoLock(mMutex);

	MtpOperationCode operation = mRequest.getOperationCode();
	MtpResponseCode response;

	mResponse.reset();

	if (mSendObjectHandle != kInvalidObjectHandle && operation != MTP_OPERATION_SEND_OBJECT) {
		// FIXME - need to delete mSendObjectHandle from the database
		MTPE("expected SendObject after SendObjectInfo");
		mSendObjectHandle = kInvalidObjectHandle;
	}

	switch (operation) {
		case MTP_OPERATION_GET_DEVICE_INFO:
				MTPD("doGetDeviceInfo()\n");
				response = doGetDeviceInfo();
				break;
			case MTP_OPERATION_OPEN_SESSION:
				MTPD("doOpenSesion()\n");
				response = doOpenSession();
				break;
			case MTP_OPERATION_CLOSE_SESSION:
				MTPD("doCloseSession()\n");
				response = doCloseSession();
				break;
			case MTP_OPERATION_GET_STORAGE_IDS:
				MTPD("doGetStorageIDs()\n");
				response = doGetStorageIDs();
				break;
		 	 case MTP_OPERATION_GET_STORAGE_INFO:
				MTPD("about to call doGetStorageInfo()\n");
				response = doGetStorageInfo();
				break;
			case MTP_OPERATION_GET_OBJECT_PROPS_SUPPORTED:
				MTPD("about to call doGetObjectPropsSupported()\n");
				response = doGetObjectPropsSupported();
				break;
			case MTP_OPERATION_GET_OBJECT_HANDLES:
				MTPD("about to call doGetObjectHandles()\n");
				response = doGetObjectHandles();
				break;
			case MTP_OPERATION_GET_NUM_OBJECTS:
				MTPD("about to call doGetNumbObjects()\n");
				response = doGetNumObjects();
				break;
			case MTP_OPERATION_GET_OBJECT_REFERENCES:
				MTPD("about to call doGetObjectReferences()\n");
				response = doGetObjectReferences();
				break;
			case MTP_OPERATION_SET_OBJECT_REFERENCES:
				MTPD("about to call doSetObjectReferences()\n");
				response = doSetObjectReferences();
				break;
			case MTP_OPERATION_GET_OBJECT_PROP_VALUE:
				MTPD("about to call doGetObjectPropValue()\n");
				response = doGetObjectPropValue();
				break;
			case MTP_OPERATION_SET_OBJECT_PROP_VALUE:
				MTPD("about to call doSetObjectPropValue()\n");
				response = doSetObjectPropValue();
				break;
			case MTP_OPERATION_GET_DEVICE_PROP_VALUE:
				MTPD("about to call doGetDevicPropValue()\n");
				response = doGetDevicePropValue();
				break;
			case MTP_OPERATION_SET_DEVICE_PROP_VALUE:
				MTPD("about to call doSetDevicePropVaue()\n");
				response = doSetDevicePropValue();
				break;
			case MTP_OPERATION_RESET_DEVICE_PROP_VALUE:
				MTPD("about to call doResetDevicePropValue()\n");
				response = doResetDevicePropValue();
				break;
			case MTP_OPERATION_GET_OBJECT_PROP_LIST:
				MTPD("calling doGetObjectPropList()\n");
				response = doGetObjectPropList();
				break;
			case MTP_OPERATION_GET_OBJECT_INFO:
				MTPD("calling doGetObjectInfo()\n");
				response = doGetObjectInfo();
				break;
			case MTP_OPERATION_GET_OBJECT:
				MTPD("about to call doGetObject()\n");
				response = doGetObject();
				break;
			case MTP_OPERATION_GET_THUMB:
				response = doGetThumb();
				break;
			case MTP_OPERATION_GET_PARTIAL_OBJECT:
			case MTP_OPERATION_GET_PARTIAL_OBJECT_64:
				response = doGetPartialObject(operation);
				break;
			case MTP_OPERATION_SEND_OBJECT_INFO:
				MTPD("about to call doSendObjectInfo()\n");
				response = doSendObjectInfo();
				break;
			case MTP_OPERATION_SEND_OBJECT:
				MTPD("about to call doSendObject()\n");
				response = doSendObject();
				break;
			case MTP_OPERATION_DELETE_OBJECT:
				response = doDeleteObject();
				break;
			case MTP_OPERATION_GET_OBJECT_PROP_DESC:
				MTPD("about to call doGetObjectPropDesc()\n");
				response = doGetObjectPropDesc();
				break;
			case MTP_OPERATION_GET_DEVICE_PROP_DESC:
				MTPD("about to call doGetDevicePropDesc()\n");
				response = doGetDevicePropDesc();
				break;
			case MTP_OPERATION_SEND_PARTIAL_OBJECT:
				response = doSendPartialObject();
				break;
			case MTP_OPERATION_TRUNCATE_OBJECT:
				response = doTruncateObject();
				break;
			case MTP_OPERATION_BEGIN_EDIT_OBJECT:
				response = doBeginEditObject();
				break;
			case MTP_OPERATION_END_EDIT_OBJECT:
				response = doEndEditObject();
				break;
			default:
				MTPE("got unsupported command %s", MtpDebug::getOperationCodeName(operation));
				response = MTP_RESPONSE_OPERATION_NOT_SUPPORTED;
				break;
		}

		if (response == MTP_RESPONSE_TRANSACTION_CANCELLED)
			return false;
		mResponse.setResponseCode(response);
		return true;
}

MtpResponseCode MtpServer::doGetDeviceInfo() {
	MtpStringBuffer   string;
	char prop_value[PROPERTY_VALUE_MAX];

	MtpObjectFormatList* playbackFormats = mDatabase->getSupportedPlaybackFormats();
	MtpObjectFormatList* captureFormats = mDatabase->getSupportedCaptureFormats();
	MtpDevicePropertyList* deviceProperties = mDatabase->getSupportedDeviceProperties();

	// fill in device info
	mData.putUInt16(MTP_STANDARD_VERSION);
	if (mPtp) {
		MTPD("doGetDeviceInfo putting 0\n");
		mData.putUInt32(0);
	} else {
		// MTP Vendor Extension ID
		MTPD("doGetDeviceInfo putting 6\n");
		mData.putUInt32(6);
	}
	mData.putUInt16(MTP_STANDARD_VERSION);
	if (mPtp) {
		// no extensions
		MTPD("doGetDeviceInfo no extensions\n");
		string.set("");
	} else {
		// MTP extensions
		MTPD("doGetDeviceInfo microsoft.com: 1.0; android.com: 1.0;\n");
		string.set("microsoft.com: 1.0; android.com: 1.0;");
	}
	mData.putString(string); // MTP Extensions
	mData.putUInt16(0); //Functional Mode
	MTPD("doGetDeviceInfo opcodes, %i\n", sizeof(kSupportedOperationCodes) / sizeof(uint16_t));
	MTPD("doGetDeviceInfo eventcodes, %i\n", sizeof(kSupportedEventCodes) / sizeof(uint16_t));
	mData.putAUInt16(kSupportedOperationCodes,
			sizeof(kSupportedOperationCodes) / sizeof(uint16_t)); // Operations Supported
	mData.putAUInt16(kSupportedEventCodes,
			sizeof(kSupportedEventCodes) / sizeof(uint16_t)); // Events Supported
	mData.putAUInt16(deviceProperties); // Device Properties Supported
	mData.putAUInt16(captureFormats); // Capture Formats
	mData.putAUInt16(playbackFormats);  // Playback Formats

	property_get("ro.product.manufacturer", prop_value, "unknown manufacturer");
	MTPD("prop: %s\n", prop_value);
	string.set(prop_value);
	mData.putString(string);   // Manufacturer

	property_get("ro.product.model", prop_value, "MTP Device");
	string.set(prop_value);
	mData.putString(string);   // Model
	string.set("1.0");
	mData.putString(string);   // Device Version

	property_get("ro.serialno", prop_value, "????????");
	MTPD("sn: %s\n", prop_value);
	string.set(prop_value);
	mData.putString(string);   // Serial Number

	delete playbackFormats;
	delete captureFormats;
	delete deviceProperties;

	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doOpenSession() {
	if (mSessionOpen) {
		mResponse.setParameter(1, mSessionID);
		return MTP_RESPONSE_SESSION_ALREADY_OPEN;
	}
	mSessionID = mRequest.getParameter(1);
	mSessionOpen = true;

	mDatabase->sessionStarted();

	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doCloseSession() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	mSessionID = 0;
	mSessionOpen = false;
	mDatabase->sessionEnded();
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetStorageIDs() {
	MTPD("doGetStorageIDs()\n");
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	int count = mStorages.size();
	mData.putUInt32(count);
	for (int i = 0; i < count; i++) {
		MTPD("getting storageid %d\n", mStorages[i]->getStorageID());
		mData.putUInt32(mStorages[i]->getStorageID());
	}

	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetStorageInfo() {
	MtpStringBuffer   string;
	MTPD("doGetStorageInfo()\n");
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	MtpStorageID id = mRequest.getParameter(1);
	MtpStorage* storage = getStorage(id);
	if (!storage) {
		MTPE("invalid storage id\n");
		return MTP_RESPONSE_INVALID_STORAGE_ID;
	}

	mData.putUInt16(storage->getType());
	mData.putUInt16(storage->getFileSystemType());
	mData.putUInt16(storage->getAccessCapability());
	mData.putUInt64(storage->getMaxCapacity());
	mData.putUInt64(storage->getFreeSpace());
	mData.putUInt32(1024*1024*1024); // Free Space in Objects
	string.set(storage->getDescription());
	mData.putString(string);
	mData.putEmptyString();   // Volume Identifier

	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetObjectPropsSupported() {
	MTPD("doGetObjectPropsSupported()\n");
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	MtpObjectFormat format = mRequest.getParameter(1);
	mDatabase->lockMutex();
	MtpObjectPropertyList* properties = mDatabase->getSupportedObjectProperties(format);
	mData.putAUInt16(properties);
	delete properties;
	mDatabase->unlockMutex();
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetObjectHandles() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	MtpStorageID storageID = mRequest.getParameter(1);	  // 0xFFFFFFFF for all storage
	MtpObjectFormat format = mRequest.getParameter(2);	  // 0 for all formats
	MtpObjectHandle parent = mRequest.getParameter(3);	  // 0xFFFFFFFF for objects with no parent
															// 0x00000000 for all objects

	if (!hasStorage(storageID))
		return MTP_RESPONSE_INVALID_STORAGE_ID;

	MTPD("calling MtpDatabase->getObjectList()\n");
	mDatabase->lockMutex();
	MtpObjectHandleList* handles = mDatabase->getObjectList(storageID, format, parent);
	mData.putAUInt32(handles);
	delete handles;
	mDatabase->unlockMutex();
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetNumObjects() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	MtpStorageID storageID = mRequest.getParameter(1);	  // 0xFFFFFFFF for all storage
	MtpObjectFormat format = mRequest.getParameter(2);	  // 0 for all formats
	MtpObjectHandle parent = mRequest.getParameter(3);	  // 0xFFFFFFFF for objects with no parent
															// 0x00000000 for all objects
	if (!hasStorage(storageID))
		return MTP_RESPONSE_INVALID_STORAGE_ID;

	mDatabase->lockMutex();
	int count = mDatabase->getNumObjects(storageID, format, parent);
	mDatabase->unlockMutex();
	if (count >= 0) {
		mResponse.setParameter(1, count);
		return MTP_RESPONSE_OK;
	} else {
		mResponse.setParameter(1, 0);
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	}
}

MtpResponseCode MtpServer::doGetObjectReferences() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpObjectHandle handle = mRequest.getParameter(1);

	// FIXME - check for invalid object handle
	mDatabase->lockMutex();
	MtpObjectHandleList* handles = mDatabase->getObjectReferences(handle);
	if (handles) {
		mData.putAUInt32(handles);
		delete handles;
	} else {
		MTPD("MtpServer::doGetObjectReferences putEmptyArray\n");
		mData.putEmptyArray();
	}
	mDatabase->unlockMutex();
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doSetObjectReferences() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpStorageID handle = mRequest.getParameter(1);

	MtpObjectHandleList* references = mData.getAUInt32();
	mDatabase->lockMutex();
	MtpResponseCode result = mDatabase->setObjectReferences(handle, references);
	mDatabase->unlockMutex();
	delete references;
	return result;
}

MtpResponseCode MtpServer::doGetObjectPropValue() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpObjectProperty property = mRequest.getParameter(2);
	MTPD("GetObjectPropValue %d %s\n", handle,
			MtpDebug::getObjectPropCodeName(property));

	mDatabase->lockMutex();
	MtpResponseCode res = mDatabase->getObjectPropertyValue(handle, property, mData);
	mDatabase->unlockMutex();
	return res;
}

MtpResponseCode MtpServer::doSetObjectPropValue() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpObjectProperty property = mRequest.getParameter(2);
	MTPD("SetObjectPropValue %d %s\n", handle,
			MtpDebug::getObjectPropCodeName(property));

	mDatabase->lockMutex();
	MtpResponseCode res = mDatabase->setObjectPropertyValue(handle, property, mData);
	mDatabase->unlockMutex();
	return res;
}

MtpResponseCode MtpServer::doGetDevicePropValue() {
	MtpDeviceProperty property = mRequest.getParameter(1);
	MTPD("GetDevicePropValue %s\n",
			MtpDebug::getDevicePropCodeName(property));

	mDatabase->lockMutex();
	MtpResponseCode res = mDatabase->getDevicePropertyValue(property, mData);
	mDatabase->unlockMutex();
	return res;
}

MtpResponseCode MtpServer::doSetDevicePropValue() {
	MtpDeviceProperty property = mRequest.getParameter(1);
	MTPD("SetDevicePropValue %s\n",
			MtpDebug::getDevicePropCodeName(property));

	mDatabase->lockMutex();
	MtpResponseCode res = mDatabase->setDevicePropertyValue(property, mData);
	mDatabase->unlockMutex();
	return res;
}

MtpResponseCode MtpServer::doResetDevicePropValue() {
	MtpDeviceProperty property = mRequest.getParameter(1);
	MTPD("ResetDevicePropValue %s\n",
			MtpDebug::getDevicePropCodeName(property));

	mDatabase->lockMutex();
	MtpResponseCode res = mDatabase->resetDeviceProperty(property);
	mDatabase->unlockMutex();
	return res;
}

MtpResponseCode MtpServer::doGetObjectPropList() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;

	MtpObjectHandle handle = mRequest.getParameter(1);
	// use uint32_t so we can support 0xFFFFFFFF
	uint32_t format = mRequest.getParameter(2);
	uint32_t property = mRequest.getParameter(3);
	int groupCode = mRequest.getParameter(4);
	int depth = mRequest.getParameter(5);
	MTPD("GetObjectPropList %d format: %s property: %x group: %d depth: %d\n",
			handle, MtpDebug::getFormatCodeName(format),
			property, groupCode, depth);

	mDatabase->lockMutex();
	MtpResponseCode res = mDatabase->getObjectPropertyList(handle, format, property, groupCode, depth, mData);
	mDatabase->unlockMutex();
	return res;
}

MtpResponseCode MtpServer::doGetObjectInfo() {
	MTPD("inside doGetObjectInfo()\n");
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpObjectInfo info(handle);
	MTPD("calling mtpdatabase getObjectInfo()\n");
	mDatabase->lockMutex();
	MtpResponseCode result = mDatabase->getObjectInfo(handle, info);
	mDatabase->unlockMutex();
	if (result == MTP_RESPONSE_OK) {
		char	date[20];

		mData.putUInt32(info.mStorageID);
		mData.putUInt16(info.mFormat);
		mData.putUInt16(info.mProtectionStatus);

		// if object is being edited the database size may be out of date
		uint32_t size = info.mCompressedSize;
		ObjectEdit* edit = getEditObject(handle);
		if (edit)
			size = (edit->mSize > 0xFFFFFFFFLL ? 0xFFFFFFFF : (uint32_t)edit->mSize);
		mData.putUInt32(size);

		mData.putUInt16(info.mThumbFormat);
		mData.putUInt32(info.mThumbCompressedSize);
		mData.putUInt32(info.mThumbPixWidth);
		mData.putUInt32(info.mThumbPixHeight);
		mData.putUInt32(info.mImagePixWidth);
		mData.putUInt32(info.mImagePixHeight);
		mData.putUInt32(info.mImagePixDepth);
		mData.putUInt32(info.mParent);
		mData.putUInt16(info.mAssociationType);
		mData.putUInt32(info.mAssociationDesc);
		mData.putUInt32(info.mSequenceNumber);
		MTPD("info.mName: %s\n", info.mName);
		mData.putString(info.mName);
		mData.putEmptyString();	// date created
		formatDateTime(info.mDateModified, date, sizeof(date));
		mData.putString(date);   // date modified
		mData.putEmptyString();   // keywords
	}
	return result;
}

MtpResponseCode MtpServer::doGetObject() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpString pathBuf;
	int64_t fileLength;
	MtpObjectFormat format;
	MTPD("MtpServer::doGetObject calling getObjectFilePath\n");
	mDatabase->lockMutex();
	int result = mDatabase->getObjectFilePath(handle, pathBuf, fileLength, format);
	mDatabase->unlockMutex();
	if (result != MTP_RESPONSE_OK)
		return result;

	const char* filePath = (const char *)pathBuf;
	MTPD("filePath: %s\n", filePath);
	mtp_file_range  mfr;
	mfr.fd = open(filePath, O_RDONLY);
	if (mfr.fd < 0) {
		return MTP_RESPONSE_GENERAL_ERROR;
	}
	mfr.offset = 0;
	mfr.length = fileLength;
	MTPD("mfr.length: %lld\n", mfr.length);
	mfr.command = mRequest.getOperationCode();
	mfr.transaction_id = mRequest.getTransactionID();

	// then transfer the file
	int ret = ioctl(mFD, MTP_SEND_FILE_WITH_HEADER, (unsigned long)&mfr);
	MTPD("MTP_SEND_FILE_WITH_HEADER returned %d\n", ret);
	close(mfr.fd);
	if (ret < 0) {
		if (errno == ECANCELED)
			return MTP_RESPONSE_TRANSACTION_CANCELLED;
		else
			return MTP_RESPONSE_GENERAL_ERROR;
	}
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetThumb() {
	MtpObjectHandle handle = mRequest.getParameter(1);
	size_t thumbSize;
	mDatabase->lockMutex();
	void* thumb = mDatabase->getThumbnail(handle, thumbSize);
	mDatabase->unlockMutex();
	if (thumb) {
		// send data
		mData.setOperationCode(mRequest.getOperationCode());
		mData.setTransactionID(mRequest.getTransactionID());
		mData.writeData(mFD, thumb, thumbSize);
		free(thumb);
		return MTP_RESPONSE_OK;
	} else {
		return MTP_RESPONSE_GENERAL_ERROR;
	}
}

MtpResponseCode MtpServer::doGetPartialObject(MtpOperationCode operation) {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpObjectHandle handle = mRequest.getParameter(1);
	uint64_t offset;
	uint32_t length;
	offset = mRequest.getParameter(2);
	if (operation == MTP_OPERATION_GET_PARTIAL_OBJECT_64) {
		// android extension with 64 bit offset
		uint64_t offset2 = mRequest.getParameter(3);
		offset = offset | (offset2 << 32);
		length = mRequest.getParameter(4);
	} else {
		// standard GetPartialObject
		length = mRequest.getParameter(3);
	}
	MtpString pathBuf;
	int64_t fileLength;
	MtpObjectFormat format;
	MTPD("MtpServer::doGetPartialObject calling getObjectFilePath\n");
	mDatabase->lockMutex();
	int result = mDatabase->getObjectFilePath(handle, pathBuf, fileLength, format);
	mDatabase->unlockMutex();
	if (result != MTP_RESPONSE_OK) {
		return result;
	}
	if (offset + length > (uint64_t)fileLength)
		length = fileLength - offset;

	const char* filePath = (const char *)pathBuf;
	mtp_file_range  mfr;
	mfr.fd = open(filePath, O_RDONLY);
	if (mfr.fd < 0) {
		return MTP_RESPONSE_GENERAL_ERROR;
	}
	mfr.offset = offset;
	mfr.length = length;
	mfr.command = mRequest.getOperationCode();
	mfr.transaction_id = mRequest.getTransactionID();
	mResponse.setParameter(1, length);

	// transfer the file
	int ret = ioctl(mFD, MTP_SEND_FILE_WITH_HEADER, (unsigned long)&mfr);
	MTPD("MTP_SEND_FILE_WITH_HEADER returned %d\n", ret);
	close(mfr.fd);
	if (ret < 0) {
		if (errno == ECANCELED)
			return MTP_RESPONSE_TRANSACTION_CANCELLED;
		else
			return MTP_RESPONSE_GENERAL_ERROR;
	}
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doSendObjectInfo() {
	MTPD("MtpServer::doSendObjectInfo starting\n");
	MtpString path;
	MtpStorageID storageID = mRequest.getParameter(1);
	MtpStorage* storage = getStorage(storageID);
	MtpObjectHandle parent = mRequest.getParameter(2);
	if (!storage)
		return MTP_RESPONSE_INVALID_STORAGE_ID;

	// special case the root
	if (parent == MTP_PARENT_ROOT) {
		MTPD("MtpServer::doSendObjectInfo special case root\n");
		path = storage->getPath();
		parent = 0;
	} else {
		int64_t length;
		MtpObjectFormat format;
		MTPD("MtpServer::doSendObjectInfo calling getObjectFilePath\n");
		mDatabase->lockMutex();
		int result = mDatabase->getObjectFilePath(parent, path, length, format);
		mDatabase->unlockMutex();
		if (result != MTP_RESPONSE_OK) {
			return result;
		}
		if (format != MTP_FORMAT_ASSOCIATION)
			return MTP_RESPONSE_INVALID_PARENT_OBJECT;
	}

	// read only the fields we need
	mData.getUInt32();  // storage ID
	MtpObjectFormat format = mData.getUInt16();
	mData.getUInt16();  // protection status
	mSendObjectFileSize = mData.getUInt32();
	mData.getUInt16();  // thumb format
	mData.getUInt32();  // thumb compressed size
	mData.getUInt32();  // thumb pix width
	mData.getUInt32();  // thumb pix height
	mData.getUInt32();  // image pix width
	mData.getUInt32();  // image pix height
	mData.getUInt32();  // image bit depth
	mData.getUInt32();  // parent
	uint16_t associationType = mData.getUInt16();
	uint32_t associationDesc = mData.getUInt32();   // association desc
	mData.getUInt32();  // sequence number
	MtpStringBuffer name, created, modified;
	mData.getString(name);	// file name
	mData.getString(created);	  // date created
	mData.getString(modified);	 // date modified
	// keywords follow

	MTPD("name: %s format: %04X\n", (const char *)name, format);
	time_t modifiedTime;
	if (!parseDateTime(modified, modifiedTime)) {
		modifiedTime = 0;
	}
	if (path[path.size() - 1] != '/') {
		path += "/";
	}
	path += (const char *)name;

	// check space first
	if (mSendObjectFileSize > storage->getFreeSpace())
		return MTP_RESPONSE_STORAGE_FULL;
	uint64_t maxFileSize = storage->getMaxFileSize();
	// check storage max file size
	MTPD("maxFileSize: %ld\n", maxFileSize); 
	if (maxFileSize != 0) {
		// if mSendObjectFileSize is 0xFFFFFFFF, then all we know is the file size
		// is >= 0xFFFFFFFF
		if (mSendObjectFileSize > maxFileSize || mSendObjectFileSize == 0xFFFFFFFF)
			return MTP_RESPONSE_OBJECT_TOO_LARGE;
	}

	MTPD("MtpServer::doSendObjectInfo path: %s parent: %d storageID: %08X\n", (const char*)path, parent, storageID);
	mDatabase->lockMutex();
	MtpObjectHandle handle = mDatabase->beginSendObject((const char*)path,
			format, parent, storageID, mSendObjectFileSize, modifiedTime);
	mDatabase->unlockMutex();
	if (handle == kInvalidObjectHandle) {
		MTPE("MtpServer::doSendObjectInfo returning MTP_RESPONSE_GENERAL_ERROR, handle == kInvalidObjectHandle\n");
		return MTP_RESPONSE_GENERAL_ERROR;
	}

  if (format == MTP_FORMAT_ASSOCIATION) {
		mode_t mask = umask(0);
		MTPD("MtpServer::doSendObjectInfo mkdir '%s'\n", (const char *)path);
		int ret = mkdir((const char *)path, mDirectoryPermission);
		umask(mask);
		if (ret && ret != -EEXIST) {
			MTPE("MtpServer::doSendObjectInfo returning MTP_RESPONSE_GENERAL_ERROR, ret && ret != -EEXIST\n");
			return MTP_RESPONSE_GENERAL_ERROR;
		}
		chown((const char *)path, getuid(), mFileGroup);
		tw_set_default_metadata((const char *)path);

		// SendObject does not get sent for directories, so call endSendObject here instead
		mDatabase->lockMutex();
		mDatabase->endSendObject(path, handle, MTP_FORMAT_ASSOCIATION, MTP_RESPONSE_OK);
		mDatabase->unlockMutex();
	} else {
		mSendObjectFilePath = path;
		// save the handle for the SendObject call, which should follow
		mSendObjectHandle = handle;
		mSendObjectFormat = format;
	}

	mResponse.setParameter(1, storageID);
	mResponse.setParameter(2, parent);
	mResponse.setParameter(3, handle);
	MTPD("MtpServer::doSendObjectInfo returning MTP_RESPONSE_OK\n");
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doSendObject() {
	if (!hasStorage())
		return MTP_RESPONSE_GENERAL_ERROR;
	MtpResponseCode result = MTP_RESPONSE_OK;
	mode_t mask;
	int ret = 0, initialData;

	if (mSendObjectHandle == kInvalidObjectHandle) {
		MTPE("Expected SendObjectInfo before SendObject");
		result = MTP_RESPONSE_NO_VALID_OBJECT_INFO;
		goto done;
	}

	// read the header, and possibly some data
	ret = mData.read(mFD);
	if (ret < MTP_CONTAINER_HEADER_SIZE) {
		MTPE("MTP_RESPONSE_GENERAL_ERROR\n");
		result = MTP_RESPONSE_GENERAL_ERROR;
		goto done;
	}
	initialData = ret - MTP_CONTAINER_HEADER_SIZE;

	mtp_file_range  mfr;
	mfr.fd = open(mSendObjectFilePath, O_RDWR | O_CREAT | O_TRUNC, 0640);
	if (mfr.fd < 0) {
		result = MTP_RESPONSE_GENERAL_ERROR;
		MTPE("fd error\n");
		goto done;
	}
	fchown(mfr.fd, getuid(), mFileGroup);
	// set permissions
	mask = umask(0);
	fchmod(mfr.fd, mFilePermission);
	umask(mask);

	if (initialData > 0)
		ret = write(mfr.fd, mData.getData(), initialData);

	if (mSendObjectFileSize - initialData > 0) {
		mfr.offset = initialData;
		if (mSendObjectFileSize == 0xFFFFFFFF) {
			// tell driver to read until it receives a short packet
			mfr.length = 0xFFFFFFFF;
		} else {
			mfr.length = mSendObjectFileSize - initialData;
		}

		MTPD("receiving %s\n", (const char *)mSendObjectFilePath);
		// transfer the file
		ret = ioctl(mFD, MTP_RECEIVE_FILE, (unsigned long)&mfr);
	}
	close(mfr.fd);
	tw_set_default_metadata((const char *)mSendObjectFilePath);

	if (ret < 0) {
		unlink(mSendObjectFilePath);
		if (errno == ECANCELED)
			result = MTP_RESPONSE_TRANSACTION_CANCELLED;
		else {
		    	MTPD("errno: %d\n", errno);
			result = MTP_RESPONSE_GENERAL_ERROR;
		}
	}

done:
	// reset so we don't attempt to send the data back
	MTPD("MTP_RECEIVE_FILE returned %d\n", ret);
	mData.reset();
	mDatabase->lockMutex();
	mDatabase->endSendObject(mSendObjectFilePath, mSendObjectHandle, mSendObjectFormat,
			result == MTP_RESPONSE_OK);
	mDatabase->unlockMutex();
	mSendObjectHandle = kInvalidObjectHandle;
	MTPD("result: %d\n", result);
	mSendObjectFormat = 0;
	return result;
}

static void deleteRecursive(const char* path) {
	char pathbuf[PATH_MAX];
	size_t pathLength = strlen(path);
	if (pathLength >= sizeof(pathbuf) - 1) {
		MTPE("path too long: %s\n", path);
	}
	strcpy(pathbuf, path);
	if (pathbuf[pathLength - 1] != '/') {
		pathbuf[pathLength++] = '/';
	}
	char* fileSpot = pathbuf + pathLength;
	int pathRemaining = sizeof(pathbuf) - pathLength - 1;

	DIR* dir = opendir(path);
	if (!dir) {
		MTPE("opendir %s failed: %s", path, strerror(errno));
		return;
	}

	struct dirent* entry;
	while ((entry = readdir(dir))) {
		const char* name = entry->d_name;

		// ignore "." and ".."
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
			continue;
		}

		int nameLength = strlen(name);
		if (nameLength > pathRemaining) {
			MTPE("path %s/%s too long\n", path, name);
			continue;
		}
		strcpy(fileSpot, name);

		int type = entry->d_type;
		struct stat st;
		if (lstat(pathbuf, &st)) {
			MTPE("Failed to lstat '%s'\n", pathbuf);
			continue;
		}
		if (st.st_mode & S_IFDIR) {
			deleteRecursive(pathbuf);
			rmdir(pathbuf);
		} else {
			unlink(pathbuf);
		}
	}
	closedir(dir);
}

static void deletePath(const char* path) {
	struct stat statbuf;
	if (stat(path, &statbuf) == 0) {
		if (S_ISDIR(statbuf.st_mode)) {
			deleteRecursive(path);
			rmdir(path);
		} else {
			unlink(path);
		}
	} else {
		MTPE("deletePath stat failed for %s: %s", path, strerror(errno));
	}
}

MtpResponseCode MtpServer::doDeleteObject() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpObjectFormat format = mRequest.getParameter(2);
	// FIXME - support deleting all objects if handle is 0xFFFFFFFF
	// FIXME - implement deleting objects by format

	MtpString filePath;
	int64_t fileLength;
	MTPD("MtpServer::doDeleteObject calling getObjectFilePath\n");
	mDatabase->lockMutex();
	int result = mDatabase->getObjectFilePath(handle, filePath, fileLength, format);
	if (result == MTP_RESPONSE_OK) {
		MTPD("deleting %s", (const char *)filePath);
		result = mDatabase->deleteFile(handle);
		// Don't delete the actual files unless the database deletion is allowed
		if (result == MTP_RESPONSE_OK) {
			deletePath((const char *)filePath);
		}
	}
	mDatabase->unlockMutex();
	return result;
}

MtpResponseCode MtpServer::doGetObjectPropDesc() {
	MtpObjectProperty propCode = mRequest.getParameter(1);
	MtpObjectFormat format = mRequest.getParameter(2);
	MTPD("MtpServer::doGetObjectPropDesc %s %s\n", MtpDebug::getObjectPropCodeName(propCode),
										MtpDebug::getFormatCodeName(format));
	mDatabase->lockMutex();
	MtpProperty* property = mDatabase->getObjectPropertyDesc(propCode, format);
	mDatabase->unlockMutex();
	if (!property) {
		MTPE("MtpServer::doGetObjectPropDesc propery not supported\n");
		return MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
	}
	property->write(mData);
	delete property;
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetDevicePropDesc() {
	MtpDeviceProperty propCode = mRequest.getParameter(1);
	MTPD("GetDevicePropDesc %s\n", MtpDebug::getDevicePropCodeName(propCode));
	mDatabase->lockMutex();
	MtpProperty* property = mDatabase->getDevicePropertyDesc(propCode);
	mDatabase->unlockMutex();
	if (!property) {
		MTPE("MtpServer::doGetDevicePropDesc property not supported\n");
		return MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
	}
	property->write(mData);
	delete property;
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doSendPartialObject() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	MtpObjectHandle handle = mRequest.getParameter(1);
	uint64_t offset = mRequest.getParameter(2);
	uint64_t offset2 = mRequest.getParameter(3);
	offset = offset | (offset2 << 32);
	uint32_t length = mRequest.getParameter(4);

	ObjectEdit* edit = getEditObject(handle);
	if (!edit) {
		MTPE("object not open for edit in doSendPartialObject");
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	// can't start writing past the end of the file
	if (offset > edit->mSize) {
		MTPE("writing past end of object, offset: %lld, edit->mSize: %lld", offset, edit->mSize);
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	const char* filePath = (const char *)edit->mPath;
	MTPD("receiving partial %s %lld %lld\n", filePath, offset, length);

	// read the header, and possibly some data
	int ret = mData.read(mFD);
	if (ret < MTP_CONTAINER_HEADER_SIZE)
		return MTP_RESPONSE_GENERAL_ERROR;
	int initialData = ret - MTP_CONTAINER_HEADER_SIZE;

	if (initialData > 0) {
		ret = write(edit->mFD, mData.getData(), initialData);
		offset += initialData;
		length -= initialData;
	}

	if (length > 0) {
		mtp_file_range  mfr;
		mfr.fd = edit->mFD;
		mfr.offset = offset;
		mfr.length = length;

		// transfer the file
		ret = ioctl(mFD, MTP_RECEIVE_FILE, (unsigned long)&mfr);
		MTPD("MTP_RECEIVE_FILE returned %d", ret);
	}
	if (ret < 0) {
		mResponse.setParameter(1, 0);
		if (errno == ECANCELED)
			return MTP_RESPONSE_TRANSACTION_CANCELLED;
		else
			return MTP_RESPONSE_GENERAL_ERROR;
	}

	// reset so we don't attempt to send this back
	mData.reset();
	mResponse.setParameter(1, length);
	uint64_t end = offset + length;
	if (end > edit->mSize) {
		edit->mSize = end;
	}
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doTruncateObject() {
	MtpObjectHandle handle = mRequest.getParameter(1);
	ObjectEdit* edit = getEditObject(handle);
	if (!edit) {
		MTPE("object not open for edit in doTruncateObject");
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	uint64_t offset = mRequest.getParameter(2);
	uint64_t offset2 = mRequest.getParameter(3);
	offset |= (offset2 << 32);
	if (ftruncate(edit->mFD, offset) != 0) {
		return MTP_RESPONSE_GENERAL_ERROR;
	} else {
		edit->mSize = offset;
		return MTP_RESPONSE_OK;
	}
}

MtpResponseCode MtpServer::doBeginEditObject() {
	MtpObjectHandle handle = mRequest.getParameter(1);
	if (getEditObject(handle)) {
		MTPE("object already open for edit in doBeginEditObject");
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	MtpString path;
	int64_t fileLength;
	MtpObjectFormat format;
	MTPD("MtpServer::doBeginEditObject calling getObjectFilePath\n");
	mDatabase->lockMutex();
	int result = mDatabase->getObjectFilePath(handle, path, fileLength, format);
	mDatabase->unlockMutex();
	if (result != MTP_RESPONSE_OK)
		return result;

	int fd = open((const char *)path, O_RDWR | O_EXCL);
	if (fd < 0) {
		MTPE("open failed for %s in doBeginEditObject (%d)", (const char *)path, errno);
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	addEditObject(handle, path, fileLength, format, fd);
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doEndEditObject() {
	MtpObjectHandle handle = mRequest.getParameter(1);
	ObjectEdit* edit = getEditObject(handle);
	if (!edit) {
		MTPE("object not open for edit in doEndEditObject");
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	commitEdit(edit);
	removeEditObject(handle);
	return MTP_RESPONSE_OK;
}
