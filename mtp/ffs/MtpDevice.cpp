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

#define LOG_TAG "MtpDevice"

#include "MtpDebug.h"
#include "MtpDevice.h"
#include "MtpDeviceInfo.h"
#include "MtpEventPacket.h"
#include "MtpObjectInfo.h"
#include "MtpProperty.h"
#include "MtpStorageInfo.h"
#include "MtpStringBuffer.h"
#include "MtpUtils.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <endian.h>

#include <usbhost/usbhost.h>

namespace {

static constexpr int USB_CONTROL_TRANSFER_TIMEOUT_MS = 200;

}  // namespace

#if 0
static bool isMtpDevice(uint16_t vendor, uint16_t product) {
	// Sandisk Sansa Fuze
	if (vendor == 0x0781 && product == 0x74c2)
		return true;
	// Samsung YP-Z5
	if (vendor == 0x04e8 && product == 0x503c)
		return true;
	return false;
}
#endif

namespace {

bool writeToFd(void* data, uint32_t /* unused_offset */, uint32_t length, void* clientData) {
	const int fd = *static_cast<int*>(clientData);
	const ssize_t result = write(fd, data, length);
	if (result < 0) {
		return false;
	}
	return static_cast<uint32_t>(result) == length;
}

}  // namespace

MtpDevice* MtpDevice::open(const char* deviceName, int fd) {
	struct usb_device *device = usb_device_new(deviceName, fd);
	if (!device) {
		MTPE("usb_device_new failed for %s", deviceName);
		return NULL;
	}

	struct usb_descriptor_header* desc;
	struct usb_descriptor_iter iter;

	usb_descriptor_iter_init(device, &iter);

	while ((desc = usb_descriptor_iter_next(&iter)) != NULL) {
		if (desc->bDescriptorType == USB_DT_INTERFACE) {
			struct usb_interface_descriptor *interface = (struct usb_interface_descriptor *)desc;

			if (interface->bInterfaceClass == USB_CLASS_STILL_IMAGE &&
				interface->bInterfaceSubClass == 1 && // Still Image Capture
				interface->bInterfaceProtocol == 1)		// Picture Transfer Protocol (PIMA 15470)
			{
				char* manufacturerName = usb_device_get_manufacturer_name(device,
						USB_CONTROL_TRANSFER_TIMEOUT_MS);
				char* productName = usb_device_get_product_name(device,
						USB_CONTROL_TRANSFER_TIMEOUT_MS);
				MTPD("Found camera: \"%s\" \"%s\"\n", manufacturerName, productName);
				free(manufacturerName);
				free(productName);
			} else if (interface->bInterfaceClass == 0xFF &&
					interface->bInterfaceSubClass == 0xFF &&
					interface->bInterfaceProtocol == 0) {
				char* interfaceName = usb_device_get_string(device, interface->iInterface,
						USB_CONTROL_TRANSFER_TIMEOUT_MS);
				if (!interfaceName) {
					continue;
				} else if (strcmp(interfaceName, "MTP")) {
					free(interfaceName);
					continue;
				}
				free(interfaceName);

				// Looks like an android style MTP device
				char* manufacturerName = usb_device_get_manufacturer_name(device,
						USB_CONTROL_TRANSFER_TIMEOUT_MS);
				char* productName = usb_device_get_product_name(device,
						USB_CONTROL_TRANSFER_TIMEOUT_MS);
				MTPD("Found MTP device: \"%s\" \"%s\"\n", manufacturerName, productName);
				free(manufacturerName);
				free(productName);
			}
#if 0
			 else {
				// look for special cased devices based on vendor/product ID
				// we are doing this mainly for testing purposes
				uint16_t vendor = usb_device_get_vendor_id(device);
				uint16_t product = usb_device_get_product_id(device);
				if (!isMtpDevice(vendor, product)) {
					// not an MTP or PTP device
					continue;
				}
				// request MTP OS string and descriptor
				// some music players need to see this before entering MTP mode.
				char buffer[256];
				memset(buffer, 0, sizeof(buffer));
				int ret = usb_device_control_transfer(device,
						USB_DIR_IN|USB_RECIP_DEVICE|USB_TYPE_STANDARD,
						USB_REQ_GET_DESCRIPTOR, (USB_DT_STRING << 8) | 0xEE,
						0, buffer, sizeof(buffer), 0);
				printf("usb_device_control_transfer returned %d errno: %d\n", ret, errno);
				if (ret > 0) {
					printf("got MTP string %s\n", buffer);
					ret = usb_device_control_transfer(device,
							USB_DIR_IN|USB_RECIP_DEVICE|USB_TYPE_VENDOR, 1,
							0, 4, buffer, sizeof(buffer), 0);
					printf("OS descriptor got %d\n", ret);
				} else {
					printf("no MTP string\n");
				}
			}
#else
			else {
				continue;
			}
#endif
			// if we got here, then we have a likely MTP or PTP device

			// interface should be followed by three endpoints
			struct usb_endpoint_descriptor *ep;
			struct usb_endpoint_descriptor *ep_in_desc = NULL;
			struct usb_endpoint_descriptor *ep_out_desc = NULL;
			struct usb_endpoint_descriptor *ep_intr_desc = NULL;
			//USB3 add USB_DT_SS_ENDPOINT_COMP as companion descriptor;
			struct usb_ss_ep_comp_descriptor *ep_ss_ep_comp_desc = NULL;
			for (int i = 0; i < 3; i++) {
				ep = (struct usb_endpoint_descriptor *)usb_descriptor_iter_next(&iter);
				if (ep && ep->bDescriptorType == USB_DT_SS_ENDPOINT_COMP) {
					MTPD("Descriptor type is USB_DT_SS_ENDPOINT_COMP for USB3 \n");
					ep_ss_ep_comp_desc = (usb_ss_ep_comp_descriptor*)ep;
					ep = (struct usb_endpoint_descriptor *)usb_descriptor_iter_next(&iter);
				 }

				if (!ep || ep->bDescriptorType != USB_DT_ENDPOINT) {
					MTPE("endpoints not found\n");
					usb_device_close(device);
					return NULL;
				}

				if (ep->bmAttributes == USB_ENDPOINT_XFER_BULK) {
					if (ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
						ep_in_desc = ep;
					else
						ep_out_desc = ep;
				} else if (ep->bmAttributes == USB_ENDPOINT_XFER_INT &&
					ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) {
					ep_intr_desc = ep;
				}
			}
			if (!ep_in_desc || !ep_out_desc || !ep_intr_desc) {
				MTPE("endpoints not found\n");
				usb_device_close(device);
				return NULL;
			}

			int ret = usb_device_claim_interface(device, interface->bInterfaceNumber);
			if (ret && errno == EBUSY) {
				// disconnect kernel driver and try again
				usb_device_connect_kernel_driver(device, interface->bInterfaceNumber, false);
				ret = usb_device_claim_interface(device, interface->bInterfaceNumber);
			}
			if (ret) {
				MTPE("usb_device_claim_interface failed errno: %d\n", errno);
				usb_device_close(device);
				return NULL;
			}

			MtpDevice* mtpDevice = new MtpDevice(device, interface->bInterfaceNumber,
						ep_in_desc, ep_out_desc, ep_intr_desc);
			mtpDevice->initialize();
			return mtpDevice;
		}
	}

	usb_device_close(device);
	MTPE("device not found");
	return NULL;
}

MtpDevice::MtpDevice(struct usb_device* device, int interface,
			const struct usb_endpoint_descriptor *ep_in,
			const struct usb_endpoint_descriptor *ep_out,
			const struct usb_endpoint_descriptor *ep_intr)
	:	mDevice(device),
		mInterface(interface),
		mRequestIn1(NULL),
		mRequestIn2(NULL),
		mRequestOut(NULL),
		mRequestIntr(NULL),
		mDeviceInfo(NULL),
		mSessionID(0),
		mTransactionID(0),
		mReceivedResponse(false),
		mProcessingEvent(false),
		mCurrentEventHandle(0),
		mLastSendObjectInfoTransactionID(0),
		mLastSendObjectInfoObjectHandle(0),
		mPacketDivisionMode(FIRST_PACKET_HAS_PAYLOAD)
{
	mRequestIn1 = usb_request_new(device, ep_in);
	mRequestIn2 = usb_request_new(device, ep_in);
	mRequestOut = usb_request_new(device, ep_out);
	mRequestIntr = usb_request_new(device, ep_intr);
}

MtpDevice::~MtpDevice() {
	close();
	for (size_t i = 0; i < mDeviceProperties.size(); i++)
		delete mDeviceProperties[i];
	usb_request_free(mRequestIn1);
	usb_request_free(mRequestIn2);
	usb_request_free(mRequestOut);
	usb_request_free(mRequestIntr);
}

void MtpDevice::initialize() {
	openSession();
	mDeviceInfo = getDeviceInfo();
	if (mDeviceInfo) {
		if (mDeviceInfo->mDeviceProperties) {
			int count = mDeviceInfo->mDeviceProperties->size();
			for (int i = 0; i < count; i++) {
				MtpDeviceProperty propCode = (*mDeviceInfo->mDeviceProperties)[i];
				MtpProperty* property = getDevicePropDesc(propCode);
				if (property)
					mDeviceProperties.push_back(property);
			}
		}
	}
}

void MtpDevice::close() {
	if (mDevice) {
		usb_device_release_interface(mDevice, mInterface);
		usb_device_close(mDevice);
		mDevice = NULL;
	}
}

void MtpDevice::print() {
	if (!mDeviceInfo)
		return;

	mDeviceInfo->print();

	if (mDeviceInfo->mDeviceProperties) {
		MTPD("***** DEVICE PROPERTIES *****\n");
		int count = mDeviceInfo->mDeviceProperties->size();
		for (int i = 0; i < count; i++) {
			MtpDeviceProperty propCode = (*mDeviceInfo->mDeviceProperties)[i];
			MtpProperty* property = getDevicePropDesc(propCode);
			if (property) {
				property->print();
				delete property;
			}
		}
	}

	if (mDeviceInfo->mPlaybackFormats) {
			MTPD("***** OBJECT PROPERTIES *****\n");
		int count = mDeviceInfo->mPlaybackFormats->size();
		for (int i = 0; i < count; i++) {
			MtpObjectFormat format = (*mDeviceInfo->mPlaybackFormats)[i];
			MTPD("*** FORMAT: %s\n", MtpDebug::getFormatCodeName(format));
			MtpObjectPropertyList* props = getObjectPropsSupported(format);
			if (props) {
				for (size_t j = 0; j < props->size(); j++) {
					MtpObjectProperty prop = (*props)[j];
					MtpProperty* property = getObjectPropDesc(prop, format);
					if (property) {
						property->print();
						delete property;
					} else {
						MTPE("could not fetch property: %s",
								MtpDebug::getObjectPropCodeName(prop));
					}
				}
			}
		}
	}
}

const char* MtpDevice::getDeviceName() {
	if (mDevice)
		return usb_device_get_name(mDevice);
	else
		return "???";
}

bool MtpDevice::openSession() {
	std::lock_guard<std::mutex> lg(mMutex);

	mSessionID = 0;
	mTransactionID = 0;
	MtpSessionID newSession = 1;
	mRequest.reset();
	mRequest.setParameter(1, newSession);
	if (!sendRequest(MTP_OPERATION_OPEN_SESSION))
		return false;
	MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_SESSION_ALREADY_OPEN)
		newSession = mResponse.getParameter(1);
	else if (ret != MTP_RESPONSE_OK)
		return false;

	mSessionID = newSession;
	mTransactionID = 1;
	return true;
}

bool MtpDevice::closeSession() {
	// FIXME
	return true;
}

MtpDeviceInfo* MtpDevice::getDeviceInfo() {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	if (!sendRequest(MTP_OPERATION_GET_DEVICE_INFO))
		return NULL;
	if (!readData())
		return NULL;
	MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_OK) {
		MtpDeviceInfo* info = new MtpDeviceInfo;
		if (info->read(mData))
			return info;
		else
			delete info;
	}
	return NULL;
}

MtpStorageIDList* MtpDevice::getStorageIDs() {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	if (!sendRequest(MTP_OPERATION_GET_STORAGE_IDS))
		return NULL;
	if (!readData())
		return NULL;
	MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_OK) {
		return mData.getAUInt32();
	}
	return NULL;
}

MtpStorageInfo* MtpDevice::getStorageInfo(MtpStorageID storageID) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, storageID);
	if (!sendRequest(MTP_OPERATION_GET_STORAGE_INFO))
		return NULL;
	if (!readData())
		return NULL;
	MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_OK) {
		MtpStorageInfo* info = new MtpStorageInfo(storageID);
		if (info->read(mData))
			return info;
		else
			delete info;
	}
	return NULL;
}

MtpObjectHandleList* MtpDevice::getObjectHandles(MtpStorageID storageID,
			MtpObjectFormat format, MtpObjectHandle parent) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, storageID);
	mRequest.setParameter(2, format);
	mRequest.setParameter(3, parent);
	if (!sendRequest(MTP_OPERATION_GET_OBJECT_HANDLES))
		return NULL;
	if (!readData())
		return NULL;
	MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_OK) {
		return mData.getAUInt32();
	}
	return NULL;
}

MtpObjectInfo* MtpDevice::getObjectInfo(MtpObjectHandle handle) {
	std::lock_guard<std::mutex> lg(mMutex);

	// FIXME - we might want to add some caching here

	mRequest.reset();
	mRequest.setParameter(1, handle);
	if (!sendRequest(MTP_OPERATION_GET_OBJECT_INFO))
		return NULL;
	if (!readData())
		return NULL;
	MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_OK) {
		MtpObjectInfo* info = new MtpObjectInfo(handle);
		if (info->read(mData))
			return info;
		else
			delete info;
	}
	return NULL;
}

void* MtpDevice::getThumbnail(MtpObjectHandle handle, int& outLength) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, handle);
	if (sendRequest(MTP_OPERATION_GET_THUMB) && readData()) {
		MtpResponseCode ret = readResponse();
		if (ret == MTP_RESPONSE_OK) {
			return mData.getData(&outLength);
		}
	}
	outLength = 0;
	return NULL;
}

MtpObjectHandle MtpDevice::sendObjectInfo(MtpObjectInfo* info) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	MtpObjectHandle parent = info->mParent;
	if (parent == 0)
		parent = MTP_PARENT_ROOT;

	mRequest.setParameter(1, info->mStorageID);
	mRequest.setParameter(2, parent);

	mData.reset();
	mData.putUInt32(info->mStorageID);
	mData.putUInt16(info->mFormat);
	mData.putUInt16(info->mProtectionStatus);
	mData.putUInt32(info->mCompressedSize);
	mData.putUInt16(info->mThumbFormat);
	mData.putUInt32(info->mThumbCompressedSize);
	mData.putUInt32(info->mThumbPixWidth);
	mData.putUInt32(info->mThumbPixHeight);
	mData.putUInt32(info->mImagePixWidth);
	mData.putUInt32(info->mImagePixHeight);
	mData.putUInt32(info->mImagePixDepth);
	mData.putUInt32(info->mParent);
	mData.putUInt16(info->mAssociationType);
	mData.putUInt32(info->mAssociationDesc);
	mData.putUInt32(info->mSequenceNumber);
	mData.putString(info->mName);

	char created[100], modified[100];
	formatDateTime(info->mDateCreated, created, sizeof(created));
	formatDateTime(info->mDateModified, modified, sizeof(modified));

	mData.putString(created);
	mData.putString(modified);
	if (info->mKeywords)
		mData.putString(info->mKeywords);
	else
		mData.putEmptyString();

   if (sendRequest(MTP_OPERATION_SEND_OBJECT_INFO) && sendData()) {
		MtpResponseCode ret = readResponse();
		if (ret == MTP_RESPONSE_OK) {
			mLastSendObjectInfoTransactionID = mRequest.getTransactionID();
			mLastSendObjectInfoObjectHandle = mResponse.getParameter(3);
			info->mStorageID = mResponse.getParameter(1);
			info->mParent = mResponse.getParameter(2);
			info->mHandle = mResponse.getParameter(3);
			return info->mHandle;
		}
	}
	return (MtpObjectHandle)-1;
}

bool MtpDevice::sendObject(MtpObjectHandle handle, int size, int srcFD) {
	std::lock_guard<std::mutex> lg(mMutex);

	if (mLastSendObjectInfoTransactionID + 1 != mTransactionID ||
			mLastSendObjectInfoObjectHandle != handle) {
		MTPE("A sendObject request must follow the sendObjectInfo request.");
		return false;
	}

	mRequest.reset();
	if (sendRequest(MTP_OPERATION_SEND_OBJECT)) {
		mData.setOperationCode(mRequest.getOperationCode());
		mData.setTransactionID(mRequest.getTransactionID());
		const int writeResult = mData.write(mRequestOut, mPacketDivisionMode, srcFD, size);
		const MtpResponseCode ret = readResponse();
		return ret == MTP_RESPONSE_OK && writeResult > 0;
	}
	return false;
}

bool MtpDevice::deleteObject(MtpObjectHandle handle) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, handle);
	if (sendRequest(MTP_OPERATION_DELETE_OBJECT)) {
		MtpResponseCode ret = readResponse();
		if (ret == MTP_RESPONSE_OK)
			return true;
	}
	return false;
}

MtpObjectHandle MtpDevice::getParent(MtpObjectHandle handle) {
	MtpObjectInfo* info = getObjectInfo(handle);
	if (info) {
		MtpObjectHandle parent = info->mParent;
		delete info;
		return parent;
	} else {
		return -1;
	}
}

MtpObjectHandle MtpDevice::getStorageID(MtpObjectHandle handle) {
	MtpObjectInfo* info = getObjectInfo(handle);
	if (info) {
		MtpObjectHandle storageId = info->mStorageID;
		delete info;
		return storageId;
	} else {
		return -1;
	}
}

MtpObjectPropertyList* MtpDevice::getObjectPropsSupported(MtpObjectFormat format) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, format);
	if (!sendRequest(MTP_OPERATION_GET_OBJECT_PROPS_SUPPORTED))
		return NULL;
	if (!readData())
		return NULL;
	MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_OK) {
		return mData.getAUInt16();
	}
	return NULL;

}

MtpProperty* MtpDevice::getDevicePropDesc(MtpDeviceProperty code) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, code);
	if (!sendRequest(MTP_OPERATION_GET_DEVICE_PROP_DESC))
		return NULL;
	if (!readData())
		return NULL;
	MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_OK) {
		MtpProperty* property = new MtpProperty;
		if (property->read(mData))
			return property;
		else
			delete property;
	}
	return NULL;
}

MtpProperty* MtpDevice::getObjectPropDesc(MtpObjectProperty code, MtpObjectFormat format) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, code);
	mRequest.setParameter(2, format);
	if (!sendRequest(MTP_OPERATION_GET_OBJECT_PROP_DESC))
		return NULL;
	if (!readData())
		return NULL;
	const MtpResponseCode ret = readResponse();
	if (ret == MTP_RESPONSE_OK) {
		MtpProperty* property = new MtpProperty;
		if (property->read(mData))
			return property;
		else
			delete property;
	}
	return NULL;
}

bool MtpDevice::getObjectPropValue(MtpObjectHandle handle, MtpProperty* property) {
	if (property == nullptr)
		return false;

	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, handle);
	mRequest.setParameter(2, property->getPropertyCode());
	if (!sendRequest(MTP_OPERATION_GET_OBJECT_PROP_VALUE))
		return false;
	if (!readData())
		return false;
	if (readResponse() != MTP_RESPONSE_OK)
		return false;
	property->setCurrentValue(mData);
	return true;
}

bool MtpDevice::readObject(MtpObjectHandle handle,
						   ReadObjectCallback callback,
						   uint32_t expectedLength,
						   void* clientData) {
	return readObjectInternal(handle, callback, &expectedLength, clientData);
}

// reads the object's data and writes it to the specified file path
bool MtpDevice::readObject(MtpObjectHandle handle, const char* destPath, int group, int perm) {
	MTPD("readObject: %s", destPath);
	int fd = ::open(destPath, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		MTPE("open failed for %s", destPath);
		return false;
	}

	fchown(fd, getuid(), group);
	// set permissions
	int mask = umask(0);
	fchmod(fd, perm);
	umask(mask);

	bool result = readObject(handle, fd);
	::close(fd);
	return result;
}

bool MtpDevice::readObject(MtpObjectHandle handle, int fd) {
	MTPD("readObject: %d", fd);
	return readObjectInternal(handle, writeToFd, NULL /* expected size */, &fd);
}

bool MtpDevice::readObjectInternal(MtpObjectHandle handle,
								   ReadObjectCallback callback,
								   const uint32_t* expectedLength,
								   void* clientData) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, handle);
	if (!sendRequest(MTP_OPERATION_GET_OBJECT)) {
		MTPE("Failed to send a read request.");
		return false;
	}

	return readData(callback, expectedLength, nullptr, clientData);
}

bool MtpDevice::readData(ReadObjectCallback callback,
							const uint32_t* expectedLength,
							uint32_t* writtenSize,
							void* clientData) {
	if (!mData.readDataHeader(mRequestIn1)) {
		MTPE("Failed to read header.");
		return false;
	}

	// If object size 0 byte, the remote device may reply a response packet without sending any data
	// packets.
	if (mData.getContainerType() == MTP_CONTAINER_TYPE_RESPONSE) {
		mResponse.copyFrom(mData);
		return mResponse.getResponseCode() == MTP_RESPONSE_OK;
	}

	const uint32_t fullLength = mData.getContainerLength();
	if (fullLength < MTP_CONTAINER_HEADER_SIZE) {
		MTPE("fullLength is too short: %d", fullLength);
		return false;
	}
	const uint32_t length = fullLength - MTP_CONTAINER_HEADER_SIZE;
	if (expectedLength && length != *expectedLength) {
		MTPE("readObject error length: %d", fullLength);
		return false;
	}

	uint32_t offset = 0;
	bool writingError = false;

	{
		int initialDataLength = 0;
		void* const initialData = mData.getData(&initialDataLength);
		if (fullLength > MTP_CONTAINER_HEADER_SIZE && initialDataLength == 0) {
			// According to the MTP spec, the responder (MTP device) can choose two ways of sending
			// data. a) The first packet contains the head and as much of the payload as possible
			// b) The first packet contains only the header. The initiator (MTP host) needs
			// to remember which way the responder used, and send upcoming data in the same way.
			MTPD("Found short packet that contains only a header.");
			mPacketDivisionMode = FIRST_PACKET_ONLY_HEADER;
		}
		if (initialData) {
			if (initialDataLength > 0) {
				if (!callback(initialData, offset, initialDataLength, clientData)) {
					MTPE("Failed to write initial data.");
					writingError = true;
				}
				offset += initialDataLength;
			}
			free(initialData);
		}
	}

	// USB reads greater than 16K don't work.
	char buffer1[MTP_BUFFER_SIZE], buffer2[MTP_BUFFER_SIZE];
	mRequestIn1->buffer = buffer1;
	mRequestIn2->buffer = buffer2;
	struct usb_request* req = NULL;

	while (offset < length) {
		// Wait for previous read to complete.
		void* writeBuffer = NULL;
		int writeLength = 0;
		if (req) {
			const int read = mData.readDataWait(mDevice);
			if (read < 0) {
				MTPE("readDataWait failed.");
				return false;
			}
			writeBuffer = req->buffer;
			writeLength = read;
		}

		// Request to read next chunk.
		const uint32_t nextOffset = offset + writeLength;
		if (nextOffset < length) {
			// Queue up a read request.
			const size_t remaining = length - nextOffset;
			req = (req == mRequestIn1 ? mRequestIn2 : mRequestIn1);
			req->buffer_length = remaining > MTP_BUFFER_SIZE ?
					static_cast<size_t>(MTP_BUFFER_SIZE) : remaining;
			if (mData.readDataAsync(req) != 0) {
				MTPE("readDataAsync failed");
				return false;
			}
		}

		// Write previous buffer.
		if (writeBuffer && !writingError) {
			if (!callback(writeBuffer, offset, writeLength, clientData)) {
				MTPE("write failed");
				writingError = true;
			}
		}
		offset = nextOffset;
	}

	if (writtenSize) {
		*writtenSize = length;
	}

	return readResponse() == MTP_RESPONSE_OK;
}

bool MtpDevice::readPartialObject(MtpObjectHandle handle,
								  uint32_t offset,
								  uint32_t size,
								  uint32_t *writtenSize,
								  ReadObjectCallback callback,
								  void* clientData) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, handle);
	mRequest.setParameter(2, offset);
	mRequest.setParameter(3, size);
	if (!sendRequest(MTP_OPERATION_GET_PARTIAL_OBJECT)) {
		MTPE("Failed to send a read request.");
		return false;
	}
	// The expected size is null because it requires the exact number of bytes to read though
	// MTP_OPERATION_GET_PARTIAL_OBJECT allows devices to return shorter length of bytes than
	// requested. Destination's buffer length should be checked in |callback|.
	return readData(callback, nullptr /* expected size */, writtenSize, clientData);
}

bool MtpDevice::readPartialObject64(MtpObjectHandle handle,
									uint64_t offset,
									uint32_t size,
									uint32_t *writtenSize,
									ReadObjectCallback callback,
									void* clientData) {
	std::lock_guard<std::mutex> lg(mMutex);

	mRequest.reset();
	mRequest.setParameter(1, handle);
	mRequest.setParameter(2, 0xffffffff & offset);
	mRequest.setParameter(3, 0xffffffff & (offset >> 32));
	mRequest.setParameter(4, size);
	if (!sendRequest(MTP_OPERATION_GET_PARTIAL_OBJECT_64)) {
		MTPE("Failed to send a read request.");
		return false;
	}
	// The expected size is null because it requires the exact number of bytes to read though
	// MTP_OPERATION_GET_PARTIAL_OBJECT_64 allows devices to return shorter length of bytes than
	// requested. Destination's buffer length should be checked in |callback|.
	return readData(callback, nullptr /* expected size */, writtenSize, clientData);
}

bool MtpDevice::sendRequest(MtpOperationCode operation) {
	MTPD("sendRequest: %s\n", MtpDebug::getOperationCodeName(operation));
	mReceivedResponse = false;
	mRequest.setOperationCode(operation);
	if (mTransactionID > 0)
		mRequest.setTransactionID(mTransactionID++);
	int ret = mRequest.write(mRequestOut);
	mRequest.dump();
	return (ret > 0);
}

bool MtpDevice::sendData() {
	MTPD("sendData\n");
	mData.setOperationCode(mRequest.getOperationCode());
	mData.setTransactionID(mRequest.getTransactionID());
	int ret = mData.write(mRequestOut, mPacketDivisionMode);
	mData.dump();
	return (ret >= 0);
}

bool MtpDevice::readData() {
	mData.reset();
	int ret = mData.read(mRequestIn1);
	MTPD("readData returned %d\n", ret);
	if (ret >= MTP_CONTAINER_HEADER_SIZE) {
		if (mData.getContainerType() == MTP_CONTAINER_TYPE_RESPONSE) {
			MTPD("got response packet instead of data packet");
			// we got a response packet rather than data
			// copy it to mResponse
			mResponse.copyFrom(mData);
			mReceivedResponse = true;
			return false;
		}
		mData.dump();
		return true;
	}
	else {
		MTPD("readResponse failed\n");
		return false;
	}
}

MtpResponseCode MtpDevice::readResponse() {
	MTPD("readResponse\n");
	if (mReceivedResponse) {
		mReceivedResponse = false;
		return mResponse.getResponseCode();
	}
	int ret = mResponse.read(mRequestIn1);
	// handle zero length packets, which might occur if the data transfer
	// ends on a packet boundary
	if (ret == 0)
		ret = mResponse.read(mRequestIn1);
	if (ret >= MTP_CONTAINER_HEADER_SIZE) {
		mResponse.dump();
		return mResponse.getResponseCode();
	} else {
		MTPD("readResponse failed\n");
		return -1;
	}
}

int MtpDevice::submitEventRequest() {
	if (!mEventMutex.try_lock()) {
		// An event is being reaped on another thread.
		return -1;
	}
	if (mProcessingEvent) {
		// An event request was submitted, but no reapEventRequest called so far.
		return -1;
	}
	std::lock_guard<std::mutex> lg(mEventMutexForInterrupt);
	mEventPacket.sendRequest(mRequestIntr);
	const int currentHandle = ++mCurrentEventHandle;
	mProcessingEvent = true;
	mEventMutex.unlock();
	return currentHandle;
}

int MtpDevice::reapEventRequest(int handle, uint32_t (*parameters)[3]) {
	std::lock_guard<std::mutex> lg(mEventMutex);
	if (!mProcessingEvent || mCurrentEventHandle != handle || !parameters) {
		return -1;
	}
	mProcessingEvent = false;
	const int readSize = mEventPacket.readResponse(mRequestIntr->dev);
	const int result = mEventPacket.getEventCode();
	// MTP event has three parameters.
	(*parameters)[0] = mEventPacket.getParameter(1);
	(*parameters)[1] = mEventPacket.getParameter(2);
	(*parameters)[2] = mEventPacket.getParameter(3);
	return readSize != 0 ? result : 0;
}

void MtpDevice::discardEventRequest(int handle) {
	std::lock_guard<std::mutex> lg(mEventMutexForInterrupt);
	if (mCurrentEventHandle != handle) {
		return;
	}
	usb_request_cancel(mRequestIntr);
}
