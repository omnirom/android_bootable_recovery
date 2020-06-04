/*
** =========================================================================
** File:
**     tspdrv.h
**
** Description:
**     Constants and type definitions for the TouchSense Kernel Module.
**
** Portions Copyright (c) 2008-2017 Immersion Corporation. All Rights Reserved.
**
** This file contains Original Code and/or Modifications of Original Code
** as defined in and that are subject to the GNU Public License v2 -
** (the 'License'). You may not use this file except in compliance with the
** License. You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or contact
** TouchSenseSales@immersion.com.
**
** The Original Code and all software distributed under the License are
** distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
** EXPRESS OR IMPLIED, AND IMMERSION HEREBY DISCLAIMS ALL SUCH WARRANTIES,
** INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS
** FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see
** the License for the specific language governing rights and limitations
** under the License.
** =========================================================================
*/

#ifndef _TSPDRV_H
#define _TSPDRV_H

/* Constants */
#define MODULE_NAME                         "tspdrv"
#define TSPDRV                              "/dev/tspdrv"
#define TSPDRV_MAGIC_NUMBER                 0x494D4D52
#define TSPDRV_IOCTL_GROUP                  0x52
#define TSPDRV_STOP_KERNEL_TIMER            _IO(TSPDRV_IOCTL_GROUP, 1) /* obsolete, may be removed in future */
#define TSPDRV_SET_MAGIC_NUMBER             _IO(TSPDRV_IOCTL_GROUP, 2)
#define TSPDRV_ENABLE_AMP                   _IO(TSPDRV_IOCTL_GROUP, 3)
#define TSPDRV_DISABLE_AMP                  _IO(TSPDRV_IOCTL_GROUP, 4)
#define TSPDRV_GET_NUM_ACTUATORS            _IO(TSPDRV_IOCTL_GROUP, 5)
#define TSPDRV_SET_DEVICE_PARAMETER         _IO(TSPDRV_IOCTL_GROUP, 6)
#define TSPDRV_SET_DBG_LEVEL                _IO(TSPDRV_IOCTL_GROUP, 7)
#define TSPDRV_GET_DBG_LEVEL                _IO(TSPDRV_IOCTL_GROUP, 8)
#define TSPDRV_SET_RUNTIME_RECORD_FLAG      _IO(TSPDRV_IOCTL_GROUP, 9)
#define TSPDRV_GET_RUNTIME_RECORD_FLAG      _IO(TSPDRV_IOCTL_GROUP, 10)
#define TSPDRV_SET_RUNTIME_RECORD_BUF_SIZE  _IO(TSPDRV_IOCTL_GROUP, 11)
#define TSPDRV_GET_RUNTIME_RECORD_BUF_SIZE  _IO(TSPDRV_IOCTL_GROUP, 12)
#define TSPDRV_GET_PARAM_FILE_ID            _IO(TSPDRV_IOCTL_GROUP, 13)
#define TSPDRV_GET_DEVICE_STATUS            _IO(TSPDRV_IOCTL_GROUP, 14)
/*
** Frequency constant parameters to control force output values and signals.
*/
#define VIBE_KP_CFG_FREQUENCY_PARAM1        85
#define VIBE_KP_CFG_FREQUENCY_PARAM2        86
#define VIBE_KP_CFG_FREQUENCY_PARAM3        87
#define VIBE_KP_CFG_FREQUENCY_PARAM4        88
#define VIBE_KP_CFG_FREQUENCY_PARAM5        89
#define VIBE_KP_CFG_FREQUENCY_PARAM6        90

/*
** Force update rate in milliseconds.
*/
#define VIBE_KP_CFG_UPDATE_RATE_MS          95

#define VIBE_MAX_DEVICE_NAME_LENGTH         64
#define SPI_HEADER_SIZE                     3   /* DO NOT CHANGE - SPI buffer header size */
#define VIBE_OUTPUT_SAMPLE_SIZE             50  /* DO NOT CHANGE - maximum number of samples */
#define MAX_DEBUG_BUFFER_LENGTH             1024

typedef int8_t		VibeInt8;
typedef u_int8_t	VibeUInt8;
typedef int16_t		VibeInt16;
typedef u_int16_t	VibeUInt16;
typedef int32_t		VibeInt32;
typedef u_int32_t	VibeUInt32;
typedef u_int8_t	VibeBool;
typedef VibeInt32	VibeStatus;

/* Device parameters sent to the kernel module, tspdrv.ko */
typedef struct
{
    VibeInt32 nDeviceIndex;
    VibeInt32 nDeviceParamID;
    VibeInt32 nDeviceParamValue;
} device_parameter;

typedef struct
{
    VibeUInt8 nActuatorIndex;  /* 1st byte is actuator index */
    VibeUInt8 nBitDepth;       /* 2nd byte is bit depth */
    VibeUInt8 nBufferSize;     /* 3rd byte is data size */
    VibeUInt8 dataBuffer[40];
} actuator_samples_buffer;

/* Error and Return value codes */
#define VIBE_S_SUCCESS                      0	/* Success */
#define VIBE_E_FAIL			   -4	/* Generic error */

#endif  /* _TSPDRV_H */
