/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <android-base/logging.h>
#include <sys/types.h>
#include <cutils/properties.h>

#include "MtpDescriptors.h"
#include "MtpDebug.h"

const struct usb_interface_descriptor mtp_interface_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bNumEndpoints = 3,
	.bInterfaceClass = USB_CLASS_STILL_IMAGE,
	.bInterfaceSubClass = 1,
	.bInterfaceProtocol = 1,
	.iInterface = 1,
};

const struct usb_interface_descriptor ptp_interface_desc = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bNumEndpoints = 3,
	.bInterfaceClass = USB_CLASS_STILL_IMAGE,
	.bInterfaceSubClass = 1,
	.bInterfaceProtocol = 1,
};

const struct usb_endpoint_descriptor_no_audio fs_sink = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 1 | USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = MAX_PACKET_SIZE_FS,
};

const struct usb_endpoint_descriptor_no_audio fs_source = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 2 | USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = MAX_PACKET_SIZE_FS,
};

const struct usb_endpoint_descriptor_no_audio intr = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 3 | USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = MAX_PACKET_SIZE_EV,
	.bInterval = 6,
};

const struct usb_endpoint_descriptor_no_audio hs_sink = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 1 | USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = MAX_PACKET_SIZE_HS,
};

const struct usb_endpoint_descriptor_no_audio hs_source = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 2 | USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = MAX_PACKET_SIZE_HS,
};

const struct usb_endpoint_descriptor_no_audio ss_sink = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 1 | USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = MAX_PACKET_SIZE_SS,
};

const struct usb_endpoint_descriptor_no_audio ss_source = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 2 | USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = MAX_PACKET_SIZE_SS,
};

const struct usb_ss_ep_comp_descriptor ss_sink_comp = {
	.bLength = sizeof(ss_sink_comp),
	.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst = 6,
};

const struct usb_ss_ep_comp_descriptor ss_source_comp = {
	.bLength = sizeof(ss_source_comp),
	.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst = 6,
};

const struct usb_ss_ep_comp_descriptor ss_intr_comp = {
	.bLength = sizeof(ss_intr_comp),
	.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
};

const struct func_desc mtp_fs_descriptors = {
	.intf = mtp_interface_desc,
	.sink = fs_sink,
	.source = fs_source,
	.intr = intr,
};

const struct func_desc mtp_hs_descriptors = {
	.intf = mtp_interface_desc,
	.sink = hs_sink,
	.source = hs_source,
	.intr = intr,
};

const struct ss_func_desc mtp_ss_descriptors = {
	.intf = mtp_interface_desc,
	.sink = ss_sink,
	.sink_comp = ss_sink_comp,
	.source = ss_source,
	.source_comp = ss_source_comp,
	.intr = intr,
	.intr_comp = ss_intr_comp,
};

const struct func_desc ptp_fs_descriptors = {
	.intf = ptp_interface_desc,
	.sink = fs_sink,
	.source = fs_source,
	.intr = intr,
};

const struct func_desc ptp_hs_descriptors = {
	.intf = ptp_interface_desc,
	.sink = hs_sink,
	.source = hs_source,
	.intr = intr,
};

const struct ss_func_desc ptp_ss_descriptors = {
	.intf = ptp_interface_desc,
	.sink = ss_sink,
	.sink_comp = ss_sink_comp,
	.source = ss_source,
	.source_comp = ss_source_comp,
	.intr = intr,
	.intr_comp = ss_intr_comp,
};

const struct functionfs_strings mtp_strings = {
	.header = {
		.magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
		.length = htole32(sizeof(mtp_strings)),
		.str_count = htole32(1),
		.lang_count = htole32(1),
	},
	.lang0 = {
		.code = htole16(0x0409),
		.str1 = STR_INTERFACE,
	},
};

const struct usb_os_desc_header mtp_os_desc_header = {
	.interface = htole32(1),
	.dwLength = htole32(sizeof(usb_os_desc_header) + sizeof(usb_ext_compat_desc)),
	.bcdVersion = htole16(1),
	.wIndex = htole16(4),
	.bCount = htole16(1),
	.Reserved = htole16(0),
};

const struct usb_ext_compat_desc mtp_os_desc_compat = {
	.bFirstInterfaceNumber = 0,
	.Reserved1 = htole32(1),
	.CompatibleID = { 'M', 'T', 'P' },
	.SubCompatibleID = {0},
	.Reserved2 = {0},
};

const struct usb_ext_compat_desc ptp_os_desc_compat = {
	.bFirstInterfaceNumber = 0,
	.Reserved1 = htole32(1),
	.CompatibleID = { 'P', 'T', 'P' },
	.SubCompatibleID = {0},
	.Reserved2 = {0},
};

const struct desc_v2 mtp_desc_v2 = {
	.header = {
		.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.length = htole32(sizeof(struct desc_v2)),
		.flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC |
				 FUNCTIONFS_HAS_SS_DESC | FUNCTIONFS_HAS_MS_OS_DESC,
	},
	.fs_count = 4,
	.hs_count = 4,
	.ss_count = 7,
	.os_count = 1,
	.fs_descs = mtp_fs_descriptors,
	.hs_descs = mtp_hs_descriptors,
	.ss_descs = mtp_ss_descriptors,
	.os_header = mtp_os_desc_header,
	.os_desc = mtp_os_desc_compat,
};

const struct desc_v2 ptp_desc_v2 = {
	.header = {
		.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.length = htole32(sizeof(struct desc_v2)),
		.flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC |
				 FUNCTIONFS_HAS_SS_DESC | FUNCTIONFS_HAS_MS_OS_DESC,
	},
	.fs_count = 4,
	.hs_count = 4,
	.ss_count = 7,
	.os_count = 1,
	.fs_descs = ptp_fs_descriptors,
	.hs_descs = ptp_hs_descriptors,
	.ss_descs = ptp_ss_descriptors,
	.os_header = mtp_os_desc_header,
	.os_desc = ptp_os_desc_compat,
};

const struct desc_v1 mtp_desc_v1 = {
	.header = {
		.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC),
		.length = htole32(sizeof(struct desc_v1)),
		.fs_count = 4,
		.hs_count = 4,
	},
	.fs_descs = mtp_fs_descriptors,
	.hs_descs = mtp_hs_descriptors,
};

const struct desc_v1 ptp_desc_v1 = {
	.header = {
		.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC),
		.length = htole32(sizeof(struct desc_v1)),
		.fs_count = 4,
		.hs_count = 4,
	},
	.fs_descs = ptp_fs_descriptors,
	.hs_descs = ptp_hs_descriptors,
};

bool writeDescriptors(int fd, bool ptp) {
	ssize_t ret = TEMP_FAILURE_RETRY(write(fd,
				&(ptp ? ptp_desc_v2 : mtp_desc_v2), sizeof(desc_v2)));
	if (ret < 0) {
		MTPE("Switching to V1 descriptor format\n");
		ret = TEMP_FAILURE_RETRY(write(fd,
					&(ptp ? ptp_desc_v1 : mtp_desc_v1), sizeof(desc_v1)));
		if (ret < 0) {
			MTPE("Writing descriptors failed\n");
			return false;
		}
	}
	ret = TEMP_FAILURE_RETRY(write(fd, &mtp_strings, sizeof(mtp_strings)));
	if (ret < 0) {
		MTPE("Writing strings failed\n");
		return false;
	}
	property_set("sys.usb.ffs.mtp.ready", "1");
	return true;
}
