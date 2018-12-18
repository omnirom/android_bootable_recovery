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

#ifndef MTP_DESCRIPTORS_H
#define MTP_DESCRIPTORS_H

#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <sys/endian.h>

constexpr char FFS_MTP_EP0[] = "/dev/usb-ffs/mtp/ep0";
constexpr char FFS_MTP_EP_IN[] = "/dev/usb-ffs/mtp/ep1";
constexpr char FFS_MTP_EP_OUT[] = "/dev/usb-ffs/mtp/ep2";
constexpr char FFS_MTP_EP_INTR[] = "/dev/usb-ffs/mtp/ep3";

constexpr char FFS_PTP_EP0[] = "/dev/usb-ffs/ptp/ep0";
constexpr char FFS_PTP_EP_IN[] = "/dev/usb-ffs/ptp/ep1";
constexpr char FFS_PTP_EP_OUT[] = "/dev/usb-ffs/ptp/ep2";
constexpr char FFS_PTP_EP_INTR[] = "/dev/usb-ffs/ptp/ep3";

constexpr int MAX_PACKET_SIZE_FS = 64;
constexpr int MAX_PACKET_SIZE_HS = 512;
constexpr int MAX_PACKET_SIZE_SS = 1024;
constexpr int MAX_PACKET_SIZE_EV = 28;

struct func_desc {
	struct usb_interface_descriptor intf;
	struct usb_endpoint_descriptor_no_audio sink;
	struct usb_endpoint_descriptor_no_audio source;
	struct usb_endpoint_descriptor_no_audio intr;
} __attribute__((packed));

struct ss_func_desc {
	struct usb_interface_descriptor intf;
	struct usb_endpoint_descriptor_no_audio sink;
	struct usb_ss_ep_comp_descriptor sink_comp;
	struct usb_endpoint_descriptor_no_audio source;
	struct usb_ss_ep_comp_descriptor source_comp;
	struct usb_endpoint_descriptor_no_audio intr;
	struct usb_ss_ep_comp_descriptor intr_comp;
} __attribute__((packed));

struct desc_v1 {
	struct usb_functionfs_descs_head_v1 {
		__le32 magic;
		__le32 length;
		__le32 fs_count;
		__le32 hs_count;
	} __attribute__((packed)) header;
	struct func_desc fs_descs, hs_descs;
} __attribute__((packed));

struct desc_v2 {
	struct usb_functionfs_descs_head_v2 header;
	// The rest of the structure depends on the flags in the header.
	__le32 fs_count;
	__le32 hs_count;
	__le32 ss_count;
	__le32 os_count;
	struct func_desc fs_descs, hs_descs;
	struct ss_func_desc ss_descs;
	struct usb_os_desc_header os_header;
	struct usb_ext_compat_desc os_desc;
} __attribute__((packed));

// OS descriptor contents should not be changed. See b/64790536.
static_assert(sizeof(struct desc_v2) == sizeof(usb_functionfs_descs_head_v2) +
		16 + 2 * sizeof(struct func_desc) + sizeof(struct ss_func_desc) +
		sizeof(usb_os_desc_header) + sizeof(usb_ext_compat_desc),
		"Size of mtp descriptor is incorrect!");

#define STR_INTERFACE "MTP"
struct functionfs_lang {
	__le16 code;
	char str1[sizeof(STR_INTERFACE)];
} __attribute__((packed));

struct functionfs_strings {
	struct usb_functionfs_strings_head header;
	struct functionfs_lang lang0;
} __attribute__((packed));

extern const struct desc_v2 mtp_desc_v2;
extern const struct desc_v2 ptp_desc_v2;
extern const struct desc_v1 mtp_desc_v1;
extern const struct desc_v1 ptp_desc_v1;
extern const struct functionfs_strings mtp_strings;

bool writeDescriptors(int fd, bool ptp);

#endif // MTP_DESCRIPTORS_H
