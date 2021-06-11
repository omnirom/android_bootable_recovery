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

#include <algorithm>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <chrono>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <sys/time.h>

#define LOG_TAG "MtpServer"

#include "MtpDebug.h"
#include "mtp_MtpDatabase.hpp"
#include "MtpDescriptors.h"
#include "MtpDevHandle.h"
#include "MtpFfsCompatHandle.h"
#include "MtpFfsHandle.h"
#include "MtpObjectInfo.h"
#include "MtpProperty.h"
#include "MtpServer.h"
#include "MtpStorage.h"
#include "MtpStringBuffer.h"

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
//	  MTP_OPERATION_INITIATE_CAPTURE,
//	  MTP_OPERATION_FORMAT_STORE,
	MTP_OPERATION_RESET_DEVICE,
//	  MTP_OPERATION_SELF_TEST,
//	  MTP_OPERATION_SET_OBJECT_PROTECTION,
//	  MTP_OPERATION_POWER_DOWN,
	MTP_OPERATION_GET_DEVICE_PROP_DESC,
	MTP_OPERATION_GET_DEVICE_PROP_VALUE,
	MTP_OPERATION_SET_DEVICE_PROP_VALUE,
	MTP_OPERATION_RESET_DEVICE_PROP_VALUE,
//	  MTP_OPERATION_TERMINATE_OPEN_CAPTURE,
	MTP_OPERATION_MOVE_OBJECT,
	MTP_OPERATION_COPY_OBJECT,
	MTP_OPERATION_GET_PARTIAL_OBJECT,
//	  MTP_OPERATION_INITIATE_OPEN_CAPTURE,
	MTP_OPERATION_GET_OBJECT_PROPS_SUPPORTED,
	MTP_OPERATION_GET_OBJECT_PROP_DESC,
	MTP_OPERATION_GET_OBJECT_PROP_VALUE,
	MTP_OPERATION_SET_OBJECT_PROP_VALUE,
	MTP_OPERATION_GET_OBJECT_PROP_LIST,
//	  MTP_OPERATION_SET_OBJECT_PROP_LIST,
//	  MTP_OPERATION_GET_INTERDEPENDENT_PROP_DESC,
//	  MTP_OPERATION_SEND_OBJECT_PROP_LIST,
	MTP_OPERATION_GET_OBJECT_REFERENCES,
	MTP_OPERATION_SET_OBJECT_REFERENCES,
//	  MTP_OPERATION_SKIP,
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
	MTP_EVENT_DEVICE_PROP_CHANGED,
};

MtpServer::MtpServer(IMtpDatabase* database, int controlFd, bool ptp,
					const char *deviceInfoManufacturer,
					const char *deviceInfoModel,
					const char *deviceInfoDeviceVersion,
					const char *deviceInfoSerialNumber)
	:	mDatabase(database),
		mPtp(ptp),
		mDeviceInfoManufacturer(deviceInfoManufacturer),
		mDeviceInfoModel(deviceInfoModel),
		mDeviceInfoDeviceVersion(deviceInfoDeviceVersion),
		mDeviceInfoSerialNumber(deviceInfoSerialNumber),
		mSessionID(0),
		mSessionOpen(false),
		mSendObjectHandle(kInvalidObjectHandle),
		mSendObjectFormat(0),
		mSendObjectFileSize(0),
		mSendObjectModifiedTime(0)
{
	bool ffs_ok = access(FFS_MTP_EP0, W_OK) == 0;
	if (ffs_ok) {
		bool aio_compat = android::base::GetBoolProperty("sys.usb.ffs.aio_compat", false);
		mHandle = aio_compat ? new MtpFfsCompatHandle(controlFd) : new MtpFfsHandle(controlFd);
		mHandle->writeDescriptors(mPtp);
	} else {
		mHandle = new MtpDevHandle(controlFd);
	}
}

MtpServer::~MtpServer() {
}

void MtpServer::addStorage(MtpStorage* storage) {
	std::lock_guard<std::mutex> lg(mMutex);
	mDatabase->createDB(storage, storage->getStorageID());
	mStorages.push_back(storage);
	sendStoreAdded(storage->getStorageID());
}

void MtpServer::removeStorage(MtpStorage* storage) {
	std::lock_guard<std::mutex> lg(mMutex);
	auto iter = std::find(mStorages.begin(), mStorages.end(), storage);
	if (iter != mStorages.end()) {
		sendStoreRemoved(storage->getStorageID());
		mStorages.erase(iter);
	}
}

MtpStorage* MtpServer::getStorage(MtpStorageID id) {
	if (id == 0)
		return mStorages[0];
	for (MtpStorage *storage : mStorages) {
		if (storage->getStorageID() == id)
			return storage;
	}
	return nullptr;
}

bool MtpServer::hasStorage(MtpStorageID id) {
	if (id == 0 || id == 0xFFFFFFFF)
		return mStorages.size() > 0;
	return (getStorage(id) != nullptr);
}

void MtpServer::run() {
	if (mHandle->start(mPtp)) {
		MTPE("Failed to start usb driver!");
		mHandle->close();
		return;
	}

	while (1) {
		int ret = mRequest.read(mHandle);
		if (ret < 0) {
			MTPE("request read returned %d, errno: %d", ret, errno);
			if (errno == ECANCELED) {
				// return to top of loop and wait for next command
				continue;
			}
			break;
		}
		MtpOperationCode operation = mRequest.getOperationCode();
		MtpTransactionID transaction = mRequest.getTransactionID();

		MTPD("operation: %s\n", MtpDebug::getOperationCodeName(operation));
		// FIXME need to generalize this
		bool dataIn = (operation == MTP_OPERATION_SEND_OBJECT_INFO
					|| operation == MTP_OPERATION_SET_OBJECT_REFERENCES
					|| operation == MTP_OPERATION_SET_OBJECT_PROP_VALUE
					|| operation == MTP_OPERATION_SET_DEVICE_PROP_VALUE);
		if (dataIn) {
			int ret = mData.read(mHandle);
			if (ret < 0) {
				MTPE("data read returned %d, errno: %d", ret, errno);
				if (errno == ECANCELED) {
					// return to top of loop and wait for next command
					continue;
				}
				break;
			}
			MTPD("received data:");
		} else {
			mData.reset();
		}

		if (handleRequest()) {
			if (!dataIn && mData.hasData()) {
				mData.setOperationCode(operation);
				mData.setTransactionID(transaction);
				MTPD("sending data:");
				ret = mData.write(mHandle);
				if (ret < 0) {
					MTPE("request write returned %d, errno: %d", ret, errno);
					if (errno == ECANCELED) {
						// return to top of loop and wait for next command
						continue;
					}
					break;
				}
			}

			mResponse.setTransactionID(transaction);
			MTPD("sending response %04X", mResponse.getResponseCode());
			ret = mResponse.write(mHandle);
			const int savedErrno = errno;
			if (ret < 0) {
				MTPE("request write returned %d, errno: %d", ret, errno);
				if (savedErrno == ECANCELED) {
					// return to top of loop and wait for next command
					continue;
				}
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

	mHandle->close();
}

void MtpServer::sendObjectAdded(MtpObjectHandle handle) {
	MTPD("MtpServer::sendObjectAdded %d\n", handle);
	sendEvent(MTP_EVENT_OBJECT_ADDED, handle);
}

void MtpServer::sendObjectRemoved(MtpObjectHandle handle) {
	MTPD("MtpServer::sendObjectRemoved %d\n", handle);
	sendEvent(MTP_EVENT_OBJECT_REMOVED, handle);
}

void MtpServer::sendStoreRemoved(MtpStorageID id) {
	MTPD("MtpServer::sendStoreRemoved %08X\n", id);
	sendEvent(MTP_EVENT_STORE_REMOVED, id);
}

void MtpServer::sendStoreAdded(MtpStorageID id) {
	MTPD("MtpServer::sendStoreAdded %08X\n", id);
	sendEvent(MTP_EVENT_STORE_ADDED, id);
}

void MtpServer::sendObjectUpdated(MtpObjectHandle handle) {
	MTPD("MtpServer::sendObjectUpdated %d\n", handle);
	sendEvent(MTP_EVENT_OBJECT_PROP_CHANGED, handle);
}

void MtpServer::sendDevicePropertyChanged(MtpDeviceProperty property) {
	MTPD("MtpServer::sendDevicePropertyChanged %d\n", property);
	sendEvent(MTP_EVENT_DEVICE_PROP_CHANGED, property);
}

void MtpServer::sendObjectInfoChanged(MtpObjectHandle handle) {
	MTPD("MtpServer::sendObjectInfoChanged %d\n", handle);
	sendEvent(MTP_EVENT_OBJECT_INFO_CHANGED, handle);
}

void MtpServer::sendEvent(MtpEventCode code, uint32_t param1) {
	if (mSessionOpen) {
		mEvent.setEventCode(code);
		mEvent.setTransactionID(mRequest.getTransactionID());
		mEvent.setParameter(1, param1);
		if (mEvent.write(mHandle))
			MTPE("Mtp send event failed: %s\n", strerror(errno));
	}
}

void MtpServer::addEditObject(MtpObjectHandle handle, MtpStringBuffer& path,
		uint64_t size, MtpObjectFormat format, int fd) {
	ObjectEdit*  edit = new ObjectEdit(handle, path, size, format, fd);
	mObjectEditList.push_back(edit);
}

MtpServer::ObjectEdit* MtpServer::getEditObject(MtpObjectHandle handle) {
	int count = mObjectEditList.size();
	for (int i = 0; i < count; i++) {
		ObjectEdit* edit = mObjectEditList[i];
		if (edit->mHandle == handle) return edit;
	}
	return nullptr;
}

void MtpServer::removeEditObject(MtpObjectHandle handle) {
	int count = mObjectEditList.size();
	for (int i = 0; i < count; i++) {
		ObjectEdit* edit = mObjectEditList[i];
		if (edit->mHandle == handle) {
			delete edit;
			mObjectEditList.erase(mObjectEditList.begin() + i);
			return;
		}
	}
	MTPE("ObjectEdit not found in removeEditObject");
}

void MtpServer::commitEdit(ObjectEdit* edit) {
	mDatabase->rescanFile((const char *)edit->mPath, edit->mHandle, edit->mFormat);
}


bool MtpServer::handleRequest() {
	std::lock_guard<std::mutex> lg(mMutex);

	MtpOperationCode operation = mRequest.getOperationCode();
	MtpResponseCode response;

	mResponse.reset();

	if (mSendObjectHandle != kInvalidObjectHandle && operation != MTP_OPERATION_SEND_OBJECT) {
		mSendObjectHandle = kInvalidObjectHandle;
		mSendObjectFormat = 0;
		mSendObjectModifiedTime = 0;
	}

	int containertype = mRequest.getContainerType();
	if (containertype != MTP_CONTAINER_TYPE_COMMAND) {
		MTPE("wrong container type %d", containertype);
		return false;
	}

	MTPD("got command %s (%x)\n", MtpDebug::getOperationCodeName(operation), operation);

	switch (operation) {
		case MTP_OPERATION_GET_DEVICE_INFO:
			response = doGetDeviceInfo();
			break;
		case MTP_OPERATION_OPEN_SESSION:
			response = doOpenSession();
			break;
		case MTP_OPERATION_RESET_DEVICE:
		case MTP_OPERATION_CLOSE_SESSION:
			response = doCloseSession();
			break;
		case MTP_OPERATION_GET_STORAGE_IDS:
			response = doGetStorageIDs();
			break;
		 case MTP_OPERATION_GET_STORAGE_INFO:
			response = doGetStorageInfo();
			break;
		case MTP_OPERATION_GET_OBJECT_PROPS_SUPPORTED:
			response = doGetObjectPropsSupported();
			break;
		case MTP_OPERATION_GET_OBJECT_HANDLES:
			response = doGetObjectHandles();
			break;
		case MTP_OPERATION_GET_NUM_OBJECTS:
			response = doGetNumObjects();
			break;
		case MTP_OPERATION_GET_OBJECT_REFERENCES:
			response = doGetObjectReferences();
			break;
		case MTP_OPERATION_SET_OBJECT_REFERENCES:
			response = doSetObjectReferences();
			break;
		case MTP_OPERATION_GET_OBJECT_PROP_VALUE:
			response = doGetObjectPropValue();
			break;
		case MTP_OPERATION_SET_OBJECT_PROP_VALUE:
			response = doSetObjectPropValue();
			break;
		case MTP_OPERATION_GET_DEVICE_PROP_VALUE:
			response = doGetDevicePropValue();
			break;
		case MTP_OPERATION_SET_DEVICE_PROP_VALUE:
			response = doSetDevicePropValue();
			break;
		case MTP_OPERATION_RESET_DEVICE_PROP_VALUE:
			response = doResetDevicePropValue();
			break;
		case MTP_OPERATION_GET_OBJECT_PROP_LIST:
			response = doGetObjectPropList();
			break;
		case MTP_OPERATION_GET_OBJECT_INFO:
			response = doGetObjectInfo();
			break;
		case MTP_OPERATION_GET_OBJECT:
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
			response = doSendObjectInfo();
			break;
		case MTP_OPERATION_SEND_OBJECT:
			response = doSendObject();
			break;
		case MTP_OPERATION_DELETE_OBJECT:
			response = doDeleteObject();
			break;
		case MTP_OPERATION_COPY_OBJECT:
			response = doCopyObject();
			break;
		case MTP_OPERATION_MOVE_OBJECT:
			response = doMoveObject();
			break;
		case MTP_OPERATION_GET_OBJECT_PROP_DESC:
			response = doGetObjectPropDesc();
			break;
		case MTP_OPERATION_GET_DEVICE_PROP_DESC:
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
			MTPE("got unsupported command %s (%x)",
					MtpDebug::getOperationCodeName(operation), operation);
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

	MtpObjectFormatList* playbackFormats = mDatabase->getSupportedPlaybackFormats();
	MtpObjectFormatList* captureFormats = mDatabase->getSupportedCaptureFormats();
	MtpDevicePropertyList* deviceProperties = mDatabase->getSupportedDeviceProperties();

	// fill in device info
	mData.putUInt16(MTP_STANDARD_VERSION);
	if (mPtp) {
		mData.putUInt32(0);
	} else {
		// MTP Vendor Extension ID
		mData.putUInt32(6);
	}
	mData.putUInt16(MTP_STANDARD_VERSION);
	if (mPtp) {
		// no extensions
		string.set("");
	} else {
		// MTP extensions
		string.set("microsoft.com: 1.0; android.com: 1.0;");
	}
	mData.putString(string); // MTP Extensions
	mData.putUInt16(0); //Functional Mode
	mData.putAUInt16(kSupportedOperationCodes,
			sizeof(kSupportedOperationCodes) / sizeof(uint16_t)); // Operations Supported
	mData.putAUInt16(kSupportedEventCodes,
			sizeof(kSupportedEventCodes) / sizeof(uint16_t)); // Events Supported
	mData.putAUInt16(deviceProperties); // Device Properties Supported
	mData.putAUInt16(captureFormats); // Capture Formats
	mData.putAUInt16(playbackFormats);	// Playback Formats

	mData.putString(mDeviceInfoManufacturer); // Manufacturer
	mData.putString(mDeviceInfoModel); // Model
	mData.putString(mDeviceInfoDeviceVersion); // Device Version
	mData.putString(mDeviceInfoSerialNumber); // Serial Number

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
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;

	mSessionID = mRequest.getParameter(1);
	mSessionOpen = true;

	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doCloseSession() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	mSessionID = 0;
	mSessionOpen = false;
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetStorageIDs() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;

	int count = mStorages.size();
	mData.putUInt32(count);
	for (int i = 0; i < count; i++)
		mData.putUInt32(mStorages[i]->getStorageID());

	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetStorageInfo() {
	MtpStringBuffer   string;

	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;

	MtpStorageID id = mRequest.getParameter(1);
	MtpStorage* storage = getStorage(id);
	if (!storage)
		return MTP_RESPONSE_INVALID_STORAGE_ID;

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
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectFormat format = mRequest.getParameter(1);
	MtpObjectPropertyList* properties = mDatabase->getSupportedObjectProperties(format);
	mData.putAUInt16(properties);
	delete properties;
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetObjectHandles() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	if (mRequest.getParameterCount() < 3)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpStorageID storageID = mRequest.getParameter(1);		// 0xFFFFFFFF for all storage
	MtpObjectFormat format = mRequest.getParameter(2);		// 0 for all formats
	MtpObjectHandle parent = mRequest.getParameter(3);		// 0xFFFFFFFF for objects with no parent
															// 0x00000000 for all objects

	if (!hasStorage(storageID))
		return MTP_RESPONSE_INVALID_STORAGE_ID;

	MtpObjectHandleList* handles = mDatabase->getObjectList(storageID, format, parent);
	if (handles == NULL)
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	mData.putAUInt32(handles);
	delete handles;
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetNumObjects() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	if (mRequest.getParameterCount() < 3)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpStorageID storageID = mRequest.getParameter(1);		// 0xFFFFFFFF for all storage
	MtpObjectFormat format = mRequest.getParameter(2);		// 0 for all formats
	MtpObjectHandle parent = mRequest.getParameter(3);		// 0xFFFFFFFF for objects with no parent
															// 0x00000000 for all objects
	if (!hasStorage(storageID))
		return MTP_RESPONSE_INVALID_STORAGE_ID;

	int count = mDatabase->getNumObjects(storageID, format, parent);
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
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle handle = mRequest.getParameter(1);

	// FIXME - check for invalid object handle
	MtpObjectHandleList* handles = mDatabase->getObjectReferences(handle);
	if (handles) {
		mData.putAUInt32(handles);
		delete handles;
	} else {
		mData.putEmptyArray();
	}
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doSetObjectReferences() {
	if (!mSessionOpen)
		return MTP_RESPONSE_SESSION_NOT_OPEN;
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpStorageID handle = mRequest.getParameter(1);

	MtpObjectHandleList* references = mData.getAUInt32();
	if (!references)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpResponseCode result = mDatabase->setObjectReferences(handle, references);
	delete references;
	return result;
}

MtpResponseCode MtpServer::doGetObjectPropValue() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	if (mRequest.getParameterCount() < 2)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpObjectProperty property = mRequest.getParameter(2);
	MTPD("GetObjectPropValue %d %s\n", handle,
			MtpDebug::getObjectPropCodeName(property));

	return mDatabase->getObjectPropertyValue(handle, property, mData);
}

MtpResponseCode MtpServer::doSetObjectPropValue() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	if (mRequest.getParameterCount() < 2)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpObjectProperty property = mRequest.getParameter(2);
	MTPD("SetObjectPropValue %d %s\n", handle,
			MtpDebug::getObjectPropCodeName(property));

	return mDatabase->setObjectPropertyValue(handle, property, mData);
}

MtpResponseCode MtpServer::doGetDevicePropValue() {
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpDeviceProperty property = mRequest.getParameter(1);
	MTPD("GetDevicePropValue %s\n",
			MtpDebug::getDevicePropCodeName(property));

	return mDatabase->getDevicePropertyValue(property, mData);
}

MtpResponseCode MtpServer::doSetDevicePropValue() {
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpDeviceProperty property = mRequest.getParameter(1);
	MTPD("SetDevicePropValue %s\n",
			MtpDebug::getDevicePropCodeName(property));

	return mDatabase->setDevicePropertyValue(property, mData);
}

MtpResponseCode MtpServer::doResetDevicePropValue() {
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpDeviceProperty property = mRequest.getParameter(1);
	MTPD("ResetDevicePropValue %s\n",
			MtpDebug::getDevicePropCodeName(property));

	return mDatabase->resetDeviceProperty(property);
}

MtpResponseCode MtpServer::doGetObjectPropList() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	if (mRequest.getParameterCount() < 5)
		return MTP_RESPONSE_INVALID_PARAMETER;

	MtpObjectHandle handle = mRequest.getParameter(1);
	// use uint32_t so we can support 0xFFFFFFFF
	uint32_t format = mRequest.getParameter(2);
	uint32_t property = mRequest.getParameter(3);
	int groupCode = mRequest.getParameter(4);
	int depth = mRequest.getParameter(5);
   MTPD("GetObjectPropList %d format: %s property: %s group: %d depth: %d\n",
			handle, MtpDebug::getFormatCodeName(format),
			MtpDebug::getObjectPropCodeName(property), groupCode, depth);

	return mDatabase->getObjectPropertyList(handle, format, property, groupCode, depth, mData);
}

MtpResponseCode MtpServer::doGetObjectInfo() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpObjectInfo info(handle);
	MtpResponseCode result = mDatabase->getObjectInfo(handle, info);
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
		mData.putString(info.mName);
		formatDateTime(info.mDateCreated, date, sizeof(date));
		mData.putString(date);	 // date created
		formatDateTime(info.mDateModified, date, sizeof(date));
		mData.putString(date);	 // date modified
		mData.putEmptyString();   // keywords
	}
	return result;
}

MtpResponseCode MtpServer::doGetObject() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpStringBuffer pathBuf;
	int64_t fileLength;
	MtpObjectFormat format;
	int result = mDatabase->getObjectFilePath(handle, pathBuf, fileLength, format);
	if (result != MTP_RESPONSE_OK)
		return result;

	auto start = std::chrono::steady_clock::now();

	const char* filePath = (const char *)pathBuf;
	mtp_file_range	mfr;
	mfr.fd = open(filePath, O_RDONLY);
	if (mfr.fd < 0) {
		return MTP_RESPONSE_GENERAL_ERROR;
	}
	mfr.offset = 0;
	mfr.length = fileLength;
	mfr.command = mRequest.getOperationCode();
	mfr.transaction_id = mRequest.getTransactionID();

	// then transfer the file
	int ret = mHandle->sendFile(mfr);
	if (ret < 0) {
		MTPE("Mtp send file got error %s", strerror(errno));
		if (errno == ECANCELED) {
			result = MTP_RESPONSE_TRANSACTION_CANCELLED;
		} else {
			result = MTP_RESPONSE_GENERAL_ERROR;
		}
	} else {
		result = MTP_RESPONSE_OK;
	}

	auto end = std::chrono::steady_clock::now();
	std::chrono::duration<double> diff = end - start;
	struct stat sstat;
	fstat(mfr.fd, &sstat);
	uint64_t finalsize = sstat.st_size;
	MTPD("Sent a file over MTP. Time: %f s, Size: %" PRIu64 ", Rate: %f bytes/s",
			diff.count(), finalsize, ((double) finalsize) / diff.count());
	closeObjFd(mfr.fd, filePath);
	return result;
}

MtpResponseCode MtpServer::doGetThumb() {
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle handle = mRequest.getParameter(1);
	size_t thumbSize;
	void* thumb = mDatabase->getThumbnail(handle, thumbSize);
	if (thumb) {
		// send data
		mData.setOperationCode(mRequest.getOperationCode());
		mData.setTransactionID(mRequest.getTransactionID());
		mData.writeData(mHandle, thumb, thumbSize);
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
		// MTP_OPERATION_GET_PARTIAL_OBJECT_64 takes 4 arguments
		if (mRequest.getParameterCount() < 4)
			return MTP_RESPONSE_INVALID_PARAMETER;

		// android extension with 64 bit offset
		uint64_t offset2 = mRequest.getParameter(3);
		offset = offset | (offset2 << 32);
		length = mRequest.getParameter(4);
	} else {
		// MTP_OPERATION_GET_PARTIAL_OBJECT takes 3 arguments
		if (mRequest.getParameterCount() < 3)
			return MTP_RESPONSE_INVALID_PARAMETER;

		// standard GetPartialObject
		length = mRequest.getParameter(3);
	}
	MtpStringBuffer pathBuf;
	int64_t fileLength;
	MtpObjectFormat format;
	int result = mDatabase->getObjectFilePath(handle, pathBuf, fileLength, format);
	if (result != MTP_RESPONSE_OK)
		return result;
	if (offset + length > (uint64_t)fileLength)
		length = fileLength - offset;

	const char* filePath = (const char *)pathBuf;
	MTPD("sending partial %s\n %" PRIu64 " %" PRIu32, filePath, offset, length);
	mtp_file_range	mfr;
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
	int ret = mHandle->sendFile(mfr);
	MTPD("MTP_SEND_FILE_WITH_HEADER returned %d\n", ret);
	result = MTP_RESPONSE_OK;
	if (ret < 0) {
		if (errno == ECANCELED)
			result = MTP_RESPONSE_TRANSACTION_CANCELLED;
		else
			result = MTP_RESPONSE_GENERAL_ERROR;
	}
	closeObjFd(mfr.fd, filePath);
	return result;
}

MtpResponseCode MtpServer::doSendObjectInfo() {
	MtpStringBuffer path;
	uint16_t temp16;
	uint32_t temp32;

	if (mRequest.getParameterCount() < 2)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpStorageID storageID = mRequest.getParameter(1);
	MtpStorage* storage = getStorage(storageID);
	MtpObjectHandle parent = mRequest.getParameter(2);
	if (!storage)
		return MTP_RESPONSE_INVALID_STORAGE_ID;

	// special case the root
	if (parent == MTP_PARENT_ROOT) {
		path.set(storage->getPath());
		parent = 0;
	} else {
		int64_t length;
		MtpObjectFormat format;
		int result = mDatabase->getObjectFilePath(parent, path, length, format);
		if (result != MTP_RESPONSE_OK)
			return result;
		if (format != MTP_FORMAT_ASSOCIATION)
			return MTP_RESPONSE_INVALID_PARENT_OBJECT;
	}

	// read only the fields we need
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // storage ID
	if (!mData.getUInt16(temp16)) return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectFormat format = temp16;
	if (!mData.getUInt16(temp16)) return MTP_RESPONSE_INVALID_PARAMETER;  // protection status
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;
	mSendObjectFileSize = temp32;
	if (!mData.getUInt16(temp16)) return MTP_RESPONSE_INVALID_PARAMETER;  // thumb format
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // thumb compressed size
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // thumb pix width
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // thumb pix height
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // image pix width
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // image pix height
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // image bit depth
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // parent
	if (!mData.getUInt16(temp16)) return MTP_RESPONSE_INVALID_PARAMETER;
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;
	if (!mData.getUInt32(temp32)) return MTP_RESPONSE_INVALID_PARAMETER;  // sequence number
	MtpStringBuffer name, created, modified;
	if (!mData.getString(name)) return MTP_RESPONSE_INVALID_PARAMETER;	  // file name
	if (name.isEmpty()) {
		MTPE("empty name");
		return MTP_RESPONSE_INVALID_PARAMETER;
	}
	if (!mData.getString(created)) return MTP_RESPONSE_INVALID_PARAMETER;	   // date created
	if (!mData.getString(modified)) return MTP_RESPONSE_INVALID_PARAMETER;	   // date modified
	// keywords follow

	MTPD("name: %s format: %04X\n", (const char *)name, format);
	time_t modifiedTime;
	if (!parseDateTime(modified, modifiedTime))
		modifiedTime = 0;

	if (path[path.size() - 1] != '/')
		path.append("/");
	path.append(name);

	// check space first
	if (mSendObjectFileSize > storage->getFreeSpace())
		return MTP_RESPONSE_STORAGE_FULL;
	uint64_t maxFileSize = storage->getMaxFileSize();
	// check storage max file size
	if (maxFileSize != 0) {
		// if mSendObjectFileSize is 0xFFFFFFFF, then all we know is the file size
		// is >= 0xFFFFFFFF
		if (mSendObjectFileSize > maxFileSize || mSendObjectFileSize == 0xFFFFFFFF)
			return MTP_RESPONSE_OBJECT_TOO_LARGE;
	}

	MTPD("path: %s parent: %d storageID: %08X", (const char*)path, parent, storageID);
	uint64_t size = 0; // TODO: this needs to be implemented
	time_t modified_time = 0; // TODO: this needs to be implemented
	MtpObjectHandle handle = mDatabase->beginSendObject((const char*)path, format,
			parent, storageID, size, modified_time);
	if (handle == kInvalidObjectHandle) {
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	if (format == MTP_FORMAT_ASSOCIATION) {
		int ret = makeFolder((const char *)path);
		if (ret)
			return MTP_RESPONSE_GENERAL_ERROR;

		// SendObject does not get sent for directories, so call endSendObject here instead
		mDatabase->endSendObject((const char*)path, handle, format, MTP_RESPONSE_OK);
	}
	mSendObjectFilePath = path;
	// save the handle for the SendObject call, which should follow
	mSendObjectHandle = handle;
	mSendObjectFormat = format;
	mSendObjectModifiedTime = modifiedTime;

	mResponse.setParameter(1, storageID);
	mResponse.setParameter(2, parent);
	mResponse.setParameter(3, handle);

	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doMoveObject() {
	if (!hasStorage())
		return MTP_RESPONSE_GENERAL_ERROR;
	if (mRequest.getParameterCount() < 3)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle objectHandle = mRequest.getParameter(1);
	MtpStorageID storageID = mRequest.getParameter(2);
	MtpStorage* storage = getStorage(storageID);
	MtpObjectHandle parent = mRequest.getParameter(3);
	if (!storage)
		return MTP_RESPONSE_INVALID_STORAGE_ID;
	MtpStringBuffer path;
	MtpResponseCode result;

	MtpStringBuffer fromPath;
	int64_t fileLength;
	MtpObjectFormat format;
	MtpObjectInfo info(objectHandle);
	result = mDatabase->getObjectInfo(objectHandle, info);
	if (result != MTP_RESPONSE_OK)
		return result;
	result = mDatabase->getObjectFilePath(objectHandle, fromPath, fileLength, format);
	if (result != MTP_RESPONSE_OK)
		return result;

	// special case the root
	if (parent == 0) {
		path.set(storage->getPath());
	} else {
		int64_t parentLength;
		MtpObjectFormat parentFormat;
		result = mDatabase->getObjectFilePath(parent, path, parentLength, parentFormat);
		if (result != MTP_RESPONSE_OK)
			return result;
		if (parentFormat != MTP_FORMAT_ASSOCIATION)
			return MTP_RESPONSE_INVALID_PARENT_OBJECT;
	}

	if (path[path.size() - 1] != '/')
		path.append("/");
	path.append(info.mName);

	result = mDatabase->beginMoveObject(objectHandle, parent, storageID);
	if (result != MTP_RESPONSE_OK)
		return result;

	if (info.mStorageID == storageID) {
		MTPD("Moving file from %s to %s", (const char*)fromPath, (const char*)path);
		if (renameTo(fromPath, path)) {
			PLOG(ERROR) << "rename() failed from " << fromPath << " to " << path;
			result = MTP_RESPONSE_GENERAL_ERROR;
		}
	} else {
		MTPD("Moving across storages from %s to %s", (const char*)fromPath, (const char*)path);
		if (format == MTP_FORMAT_ASSOCIATION) {
			int ret = makeFolder((const char *)path);
			ret += copyRecursive(fromPath, path);
			if (ret) {
				result = MTP_RESPONSE_GENERAL_ERROR;
			} else {
				deletePath(fromPath);
			}
		} else {
			if (copyFile(fromPath, path)) {
				result = MTP_RESPONSE_GENERAL_ERROR;
			} else {
				deletePath(fromPath);
			}
		}
	}

	// If the move failed, undo the database change
	mDatabase->endMoveObject(info.mParent, parent, info.mStorageID, storageID, objectHandle,
			result == MTP_RESPONSE_OK);

	return result;
}

MtpResponseCode MtpServer::doCopyObject() {
	if (!hasStorage())
		return MTP_RESPONSE_GENERAL_ERROR;
	MtpResponseCode result = MTP_RESPONSE_OK;
	if (mRequest.getParameterCount() < 3)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle objectHandle = mRequest.getParameter(1);
	MtpStorageID storageID = mRequest.getParameter(2);
	MtpStorage* storage = getStorage(storageID);
	MtpObjectHandle parent = mRequest.getParameter(3);
	if (!storage)
		return MTP_RESPONSE_INVALID_STORAGE_ID;
	MtpStringBuffer path;

	MtpStringBuffer fromPath;
	int64_t fileLength;
	MtpObjectFormat format;
	MtpObjectInfo info(objectHandle);
	result = mDatabase->getObjectInfo(objectHandle, info);
	if (result != MTP_RESPONSE_OK)
		return result;
	result = mDatabase->getObjectFilePath(objectHandle, fromPath, fileLength, format);
	if (result != MTP_RESPONSE_OK)
		return result;

	// special case the root
	if (parent == 0) {
		path.set(storage->getPath());
	} else {
		int64_t parentLength;
		MtpObjectFormat parentFormat;
		result = mDatabase->getObjectFilePath(parent, path, parentLength, parentFormat);
		if (result != MTP_RESPONSE_OK)
			return result;
		if (parentFormat != MTP_FORMAT_ASSOCIATION)
			return MTP_RESPONSE_INVALID_PARENT_OBJECT;
	}

	// check space first
	if ((uint64_t) fileLength > storage->getFreeSpace())
		return MTP_RESPONSE_STORAGE_FULL;

	if (path[path.size() - 1] != '/')
		path.append("/");
	path.append(info.mName);

	MtpObjectHandle handle = mDatabase->beginCopyObject(objectHandle, parent, storageID);
	if (handle == kInvalidObjectHandle) {
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	MTPD("Copying file from %s to %s", (const char*)fromPath, (const char*)path);
	if (format == MTP_FORMAT_ASSOCIATION) {
		int ret = makeFolder((const char *)path);
		ret += copyRecursive(fromPath, path);
		if (ret) {
			result = MTP_RESPONSE_GENERAL_ERROR;
		}
	} else {
		if (copyFile(fromPath, path)) {
			result = MTP_RESPONSE_GENERAL_ERROR;
		}
	}

	mDatabase->endCopyObject(handle, result);
	mResponse.setParameter(1, handle);
	return result;
}

MtpResponseCode MtpServer::doSendObject() {
	if (!hasStorage())
		return MTP_RESPONSE_GENERAL_ERROR;
	MtpResponseCode result = MTP_RESPONSE_OK;
	mode_t mask;
	int ret, initialData;
	bool isCanceled = false;
	struct stat sstat = {};

	auto start = std::chrono::steady_clock::now();

	if (mSendObjectHandle == kInvalidObjectHandle) {
		MTPE("Expected SendObjectInfo before SendObject");
		result = MTP_RESPONSE_NO_VALID_OBJECT_INFO;
		goto done;
	}

	// read the header, and possibly some data
	ret = mData.read(mHandle);
	if (ret < MTP_CONTAINER_HEADER_SIZE) {
		result = MTP_RESPONSE_GENERAL_ERROR;
		goto done;
	}
	initialData = ret - MTP_CONTAINER_HEADER_SIZE;

	if (mSendObjectFormat == MTP_FORMAT_ASSOCIATION) {
		if (initialData != 0)
			MTPE("Expected folder size to be 0!");
		mSendObjectHandle = kInvalidObjectHandle;
		mSendObjectFormat = 0;
		mSendObjectModifiedTime = 0;
		return result;
	}

	mtp_file_range	mfr;
	mfr.fd = open(mSendObjectFilePath, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (mfr.fd < 0) {
		result = MTP_RESPONSE_GENERAL_ERROR;
		goto done;
	}
	fchown(mfr.fd, getuid(), FILE_GROUP);
	// set permissions
	mask = umask(0);
	fchmod(mfr.fd, FILE_PERM);
	umask(mask);

	if (initialData > 0) {
		ret = write(mfr.fd, mData.getData(), initialData);
	}

	if (ret < 0) {
		MTPE("failed to write initial data");
		result = MTP_RESPONSE_GENERAL_ERROR;
	} else {
		mfr.offset = initialData;
		if (mSendObjectFileSize == 0xFFFFFFFF) {
			// tell driver to read until it receives a short packet
			mfr.length = 0xFFFFFFFF;
		} else {
			mfr.length = mSendObjectFileSize - initialData;
		}

		mfr.command = 0;
		mfr.transaction_id = 0;

		// transfer the file
		ret = mHandle->receiveFile(mfr, mfr.length == 0 &&
				initialData == MTP_BUFFER_SIZE - MTP_CONTAINER_HEADER_SIZE);
		if ((ret < 0) && (errno == ECANCELED)) {
			isCanceled = true;
		}
	}

	if (mSendObjectModifiedTime) {
		struct timespec newTime[2];
		newTime[0].tv_nsec = UTIME_NOW;
		newTime[1].tv_sec = mSendObjectModifiedTime;
		newTime[1].tv_nsec = 0;
		if (futimens(mfr.fd, newTime) < 0) {
			MTPE("changing modified time failed, %s", strerror(errno));
		}
	}

	fstat(mfr.fd, &sstat);
	closeObjFd(mfr.fd, mSendObjectFilePath);

	if (ret < 0) {
		MTPE("Mtp receive file got error %s", strerror(errno));
		unlink(mSendObjectFilePath);
		if (isCanceled)
			result = MTP_RESPONSE_TRANSACTION_CANCELLED;
		else
			result = MTP_RESPONSE_GENERAL_ERROR;
	}

done:
	// reset so we don't attempt to send the data back
	mData.reset();

	mDatabase->endSendObject(mSendObjectFilePath, mSendObjectHandle, mSendObjectFormat, result == MTP_RESPONSE_OK);
	mSendObjectHandle = kInvalidObjectHandle;
	mSendObjectFormat = 0;
	mSendObjectModifiedTime = 0;

	auto end = std::chrono::steady_clock::now();
	std::chrono::duration<double> diff = end - start;
	uint64_t finalsize = sstat.st_size;
	MTPD("Got a file over MTP. Time: %fs, Size: %" PRIu64 ", Rate: %f bytes/s",
			diff.count(), finalsize, ((double) finalsize) / diff.count());
	return result;
}

MtpResponseCode MtpServer::doDeleteObject() {
	MTPD("In MtpServer::doDeleteObject\n");
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle handle = mRequest.getParameter(1);
	MtpObjectFormat format;
	// FIXME - support deleting all objects if handle is 0xFFFFFFFF
	// FIXME - implement deleting objects by format

	MtpStringBuffer filePath;
	int64_t fileLength;
	int result = mDatabase->getObjectFilePath(handle, filePath, fileLength, format);
	if (result != MTP_RESPONSE_OK)
		return result;

	// Don't delete the actual files unless the database deletion is allowed
	result = mDatabase->beginDeleteObject(handle);
	if (result != MTP_RESPONSE_OK)
		return result;

	bool success = deletePath((const char *)filePath);

	mDatabase->endDeleteObject(handle, success);
	return success ? result : MTP_RESPONSE_PARTIAL_DELETION;
}

MtpResponseCode MtpServer::doGetObjectPropDesc() {
	if (mRequest.getParameterCount() < 2)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectProperty propCode = mRequest.getParameter(1);
	MtpObjectFormat format = mRequest.getParameter(2);
	MTPD("GetObjectPropDesc %s %s\n", MtpDebug::getObjectPropCodeName(propCode),
										MtpDebug::getFormatCodeName(format));
	MtpProperty* property = mDatabase->getObjectPropertyDesc(propCode, format);
	if (!property)
		return MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
	property->write(mData);
	delete property;
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doGetDevicePropDesc() {
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpDeviceProperty propCode = mRequest.getParameter(1);
	MTPD("GetDevicePropDesc %s\n", MtpDebug::getDevicePropCodeName(propCode));
	MtpProperty* property = mDatabase->getDevicePropertyDesc(propCode);
	if (!property)
		return MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
	property->write(mData);
	delete property;
	return MTP_RESPONSE_OK;
}

MtpResponseCode MtpServer::doSendPartialObject() {
	if (!hasStorage())
		return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
	if (mRequest.getParameterCount() < 4)
		return MTP_RESPONSE_INVALID_PARAMETER;
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
		MTPD("writing past end of object, offset: %" PRIu64 ", edit->mSize: %" PRIu64,
			offset, edit->mSize);
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	const char* filePath = (const char *)edit->mPath;
	MTPD("receiving partial %s %" PRIu64 " %" PRIu32, filePath, offset, length);

	// read the header, and possibly some data
	int ret = mData.read(mHandle);
	if (ret < MTP_CONTAINER_HEADER_SIZE)
		return MTP_RESPONSE_GENERAL_ERROR;
	int initialData = ret - MTP_CONTAINER_HEADER_SIZE;

	if (initialData > 0) {
		ret = pwrite(edit->mFD, mData.getData(), initialData, offset);
		offset += initialData;
		length -= initialData;
	}

	bool isCanceled = false;
	if (ret < 0) {
		MTPE("failed to write initial data");
	} else {
		mtp_file_range	mfr;
		mfr.fd = edit->mFD;
		mfr.offset = offset;
		mfr.length = length;
		mfr.command = 0;
		mfr.transaction_id = 0;

		// transfer the file
		ret = mHandle->receiveFile(mfr, mfr.length == 0 &&
				initialData == MTP_BUFFER_SIZE - MTP_CONTAINER_HEADER_SIZE);
		if ((ret < 0) && (errno == ECANCELED)) {
			isCanceled = true;
		}
	}
	if (ret < 0) {
		mResponse.setParameter(1, 0);
		if (isCanceled)
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
	if (mRequest.getParameterCount() < 3)
		return MTP_RESPONSE_INVALID_PARAMETER;
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
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
	MtpObjectHandle handle = mRequest.getParameter(1);
	if (getEditObject(handle)) {
		MTPE("object already open for edit in doBeginEditObject");
		return MTP_RESPONSE_GENERAL_ERROR;
	}

	MtpStringBuffer path;
	int64_t fileLength;
	MtpObjectFormat format;
	int result = mDatabase->getObjectFilePath(handle, path, fileLength, format);
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
	if (mRequest.getParameterCount() < 1)
		return MTP_RESPONSE_INVALID_PARAMETER;
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
