/*
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright (C) 2018-2020 THL A29 Limited, a Tencent company. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "utils_param_check.h"

/* CMIoT ML302 added by YangTao@20200910 */
#include "vfs.h"

/* Enable this macro (also control by cmake) to use static string buffer to store device info */
/* To use specific storing methods like files/flash, disable this macro and implement dedicated methods */
//#define DEBUG_DEV_INFO_USED    /* CMIoT ML302 annotated by YangTao@20200910 */

#ifdef DEBUG_DEV_INFO_USED
/* product Id  */
static char sg_product_id[MAX_SIZE_OF_PRODUCT_ID + 1] = "PRODUCT_ID";

/* device name */
static char sg_device_name[MAX_SIZE_OF_DEVICE_NAME + 1] = "YOUR_DEV_NAME";

#ifdef DEV_DYN_REG_ENABLED
/* product secret for device dynamic Registration  */
static char sg_product_secret[MAX_SIZE_OF_PRODUCT_SECRET + 1] = "YOUR_PRODUCT_SECRET";
#endif

//#ifdef AUTH_MODE_CERT    /* CMIoT ML302 annotated by YangTao@20200910 */
/* public cert file name of certificate device */
static char sg_device_cert_file_name[MAX_SIZE_OF_DEVICE_CERT_FILE_NAME + 1] = "YOUR_DEVICE_NAME_cert.crt";
/* private key file name of certificate device */
static char sg_device_privatekey_file_name[MAX_SIZE_OF_DEVICE_SECRET_FILE_NAME + 1] = "YOUR_DEVICE_NAME_private.key";
//#else
/* device secret of PSK device */
static char sg_device_secret[MAX_SIZE_OF_DEVICE_SECRET + 1] = "YOUR_IOT_PSK";
//#endif

#ifdef GATEWAY_ENABLED
/* sub-device product id  */
static char sg_sub_device_product_id[MAX_SIZE_OF_PRODUCT_ID + 1] = "PRODUCT_ID";
/* sub-device device name */
static char sg_sub_device_name[MAX_SIZE_OF_DEVICE_NAME + 1] = "YOUR_SUB_DEV_NAME";
#endif

/* CMIoT ML302 added by YangTao@20200910 */
/* 鉴权模式 */
static DeviceAuthMode sg_auth_mode = 0;

#else

/* CMIoT ML302 added by YangTao@20200910 */
/* 结构体形式存放设备基本信息文件 */
#define QCLOUD_IOT_DEVICE_INFO_FILE			"/TencentDevInfo.txt"
/* 结构体形式存放子设备基本信息文件 */
#define QCLOUD_IOT_SUB_DEVICE_INFO_FILE		"/TencentSubDevInfo.txt"
/* 存放鉴权模式文件 */
#define QCLOUD_IOT_AUTH_MODE_FILE			"/TencentAuthMode.txt"
/* 防止程序运行时反复读取鉴权模式的文件 */
static DeviceAuthMode sg_auth_mode_soft = AUTH_MODE_MAX;

#endif

/* CMIoT ML302 modified by YangTao@20200910 */
static int device_info_copy(void *pdst, void *psrc, uint8_t max_len)
{
    if (strlen(psrc) > max_len) {
        return QCLOUD_ERR_FAILURE;
    }
    memset(pdst, '\0', max_len);
    strncpy(pdst, psrc, max_len);
    return QCLOUD_RET_SUCCESS;
}

/* CMIoT ML302 added by YangTao@20200910 */
long HAL_FlashReadRaw(const char *fileName, uint8_t **dataBuff) 
{
	int fd = -1;
	int read_size = 0;
	long file_size = 0;

	POINTER_SANITY_CHECK(fileName, QCLOUD_ERR_INVAL);
	POINTER_SANITY_CHECK(dataBuff, QCLOUD_ERR_INVAL);
	
	if (0 > (fd = vfs_open(fileName, O_RDONLY, 0))) {
		Log_e("Open File: %s Failed!", fileName);
		return QCLOUD_ERR_FAILURE;
	}

	struct stat st;
	vfs_fstat(fd, &st);
	if (0 > (file_size = st.st_size)) {
		Log_e("File: %s Get Status Failed!", fileName);
		return QCLOUD_ERR_FAILURE;
	}

	if (NULL != *dataBuff) {
    	Log_i("Free dataBuff!");
        HAL_Free(*dataBuff);
    }
	
	*dataBuff = (uint8_t *)HAL_Malloc(file_size + 1);
	if (NULL == *dataBuff) {
    	Log_e("Malloc Failed!");
        vfs_close(fd);
        return QCLOUD_ERR_FAILURE;
    }
    memset(*dataBuff, 0, file_size + 1);

	read_size = vfs_read(fd, *dataBuff, file_size);
    if (read_size != file_size) {
    	Log_e("Read Failed! read_size:%d, file_size:%d", read_size, file_size);
        HAL_Free(*dataBuff);
        vfs_close(fd);
        return QCLOUD_ERR_FAILURE;
    }
	
	vfs_close(fd);
	
	return file_size;
}

/* CMIoT ML302 added by YangTao@20200910 */
int HAL_FlashRead(const char *fileName, void *data, size_t dataLen)
{
	int fd = -1;
	int read_size = 0;

	POINTER_SANITY_CHECK(fileName, QCLOUD_ERR_INVAL);
	POINTER_SANITY_CHECK(data, QCLOUD_ERR_INVAL);	
	memset((char *)data, 0, dataLen);

	/* 文件不存在时, 则首次进行创建, 并写入初始数值 */
	if (0 > (fd = vfs_open(fileName, O_RDONLY, 0))) {
		if (0 < (fd = vfs_creat(fileName, 0))) {
			vfs_write(fd, data, dataLen);
			vfs_lseek(fd, 0, SEEK_SET);
			Log_i("Creat File: %s!", fileName);
		} else {
			Log_e("Creat File: %s Failed!", fileName);
			return QCLOUD_ERR_FAILURE;
		}
	}

	read_size = vfs_read(fd, data, dataLen);
	Log_i("File: %s, read_size = %d, dataLen = %d, fd = %d", fileName, read_size, dataLen, fd);
	
	vfs_close(fd);
	
	return (dataLen == read_size)? QCLOUD_RET_SUCCESS : QCLOUD_ERR_FAILURE;
}

/* CMIoT ML302 added by YangTao@20200910 */
int HAL_FlashWrite(const char *fileName, void *data, size_t dataLen)
{
	int fd = -1;
	int write_size = 0;

	POINTER_SANITY_CHECK(fileName, QCLOUD_ERR_INVAL);
	POINTER_SANITY_CHECK(data, QCLOUD_ERR_INVAL);
	
	if (0 > (fd = vfs_creat(fileName, 0))) {
		Log_e("Open Device Info File Failed");
		return QCLOUD_ERR_FAILURE;
	}

	write_size = vfs_write(fd, data, dataLen);
	Log_i("File: %s, write_size = %d, dataLen = %d, fd = %d", fileName, write_size, dataLen, fd);
	
	vfs_close(fd);

	return (dataLen == write_size)? QCLOUD_RET_SUCCESS : QCLOUD_ERR_FAILURE;
}

int HAL_SetDevInfo(void *pdevInfo)
{
    POINTER_SANITY_CHECK(pdevInfo, QCLOUD_ERR_DEV_INFO);
    int         ret;
    DeviceInfo *devInfo = (DeviceInfo *)pdevInfo;

#ifdef DEBUG_DEV_INFO_USED
    ret = device_info_copy(sg_product_id, devInfo->product_id, MAX_SIZE_OF_PRODUCT_ID);      // set product ID
    ret |= device_info_copy(sg_device_name, devInfo->device_name, MAX_SIZE_OF_DEVICE_NAME);  // set dev name

//#ifdef AUTH_MODE_CERT    /* CMIoT ML302 annotated by YangTao@20200910 */
	if (AUTH_MODE_CERT_TLS == HAL_GetAuthMode()) {
	    ret |= device_info_copy(sg_device_cert_file_name, devInfo->dev_cert_file_name,
	                            MAX_SIZE_OF_DEVICE_CERT_FILE_NAME);  // set dev cert file name
	    ret |= device_info_copy(sg_device_privatekey_file_name, devInfo->dev_key_file_name,
	                            MAX_SIZE_OF_DEVICE_SECRET_FILE_NAME);  // set dev key file name
//#else
	} else {
    	ret |= device_info_copy(sg_device_secret, devInfo->device_secret, MAX_SIZE_OF_DEVICE_SECRET);  // set dev secret
//#endif
	}

#else
	/* CMIoT ML302 modified by YangTao@20200910 */
	DeviceInfo device_info;
	
	memset(&device_info, '\0', sizeof(DeviceInfo));
	ret = HAL_FlashRead(QCLOUD_IOT_DEVICE_INFO_FILE, &device_info, sizeof(device_info));

	ret |= device_info_copy(device_info.product_id, devInfo->product_id, MAX_SIZE_OF_PRODUCT_ID);      // set product ID
    ret |= device_info_copy(device_info.device_name, devInfo->device_name, MAX_SIZE_OF_DEVICE_NAME);  // set dev name
    
#ifdef DEV_DYN_REG_ENABLED
	ret |= device_info_copy(device_info.product_secret, devInfo->product_secret, MAX_SIZE_OF_PRODUCT_SECRET);  // get product ID
#endif

	if (AUTH_MODE_CERT_TLS == HAL_GetAuthMode()) {
	    ret |= device_info_copy(device_info.dev_cert_file_name, devInfo->dev_cert_file_name,
	                            MAX_SIZE_OF_DEVICE_CERT_FILE_NAME);  // set dev cert file name
	    ret |= device_info_copy(device_info.dev_key_file_name, devInfo->dev_key_file_name,
	                            MAX_SIZE_OF_DEVICE_SECRET_FILE_NAME);  // set dev key file name
	} else {
    	ret |= device_info_copy(device_info.device_secret, devInfo->device_secret, MAX_SIZE_OF_DEVICE_SECRET);  // set dev secret
	}
	ret |= HAL_FlashWrite(QCLOUD_IOT_DEVICE_INFO_FILE, &device_info, sizeof(device_info));
#endif

    if (QCLOUD_RET_SUCCESS != ret) {
        Log_e("Set device info err");
        ret = QCLOUD_ERR_DEV_INFO;
    }
    return ret;
}

int HAL_GetDevInfo(void *pdevInfo)
{
    POINTER_SANITY_CHECK(pdevInfo, QCLOUD_ERR_DEV_INFO);
    int         ret;
    DeviceInfo *devInfo = (DeviceInfo *)pdevInfo;
    memset((char *)devInfo, '\0', sizeof(DeviceInfo));

#ifdef DEBUG_DEV_INFO_USED
    ret = device_info_copy(devInfo->product_id, sg_product_id, MAX_SIZE_OF_PRODUCT_ID);      // get product ID
    ret |= device_info_copy(devInfo->device_name, sg_device_name, MAX_SIZE_OF_DEVICE_NAME);  // get dev name

#ifdef DEV_DYN_REG_ENABLED
    ret |= device_info_copy(devInfo->product_secret, sg_product_secret, MAX_SIZE_OF_PRODUCT_SECRET);  // get product ID
#endif

//#ifdef AUTH_MODE_CERT    /* CMIoT ML302 annotated by YangTao@20200910 */
	if (AUTH_MODE_CERT_TLS == HAL_GetAuthMode()) {
	    ret |= device_info_copy(devInfo->dev_cert_file_name, sg_device_cert_file_name,
	                            MAX_SIZE_OF_DEVICE_CERT_FILE_NAME);  // get dev cert file name
	    ret |= device_info_copy(devInfo->dev_key_file_name, sg_device_privatekey_file_name,
	                            MAX_SIZE_OF_DEVICE_SECRET_FILE_NAME);  // get dev key file name
//#else
	} else {
    	ret |= device_info_copy(devInfo->device_secret, sg_device_secret, MAX_SIZE_OF_DEVICE_SECRET);  // get dev secret
//#endif
	}

#else
	/* CMIoT ML302 modified by YangTao@20200910 */
	ret = HAL_FlashRead(QCLOUD_IOT_DEVICE_INFO_FILE, devInfo, sizeof(DeviceInfo));
#endif

    if (QCLOUD_RET_SUCCESS != ret) {
        Log_e("Get device info err");
        ret = QCLOUD_ERR_DEV_INFO;
    }
    return ret;
}

/* CMIoT ML302 added by YangTao@20200910 */
int HAL_SetAuthMode(DeviceAuthMode mode)
{
	if (AUTH_MODE_MAX <= mode) {
		Log_e("Exceeds the max auth mode!");
		return QCLOUD_ERR_FAILURE;
	}
	
#ifdef DEBUG_DEV_INFO_USED
	sg_auth_mode = mode;

	return QCLOUD_RET_SUCCESS;
#else
	int ret = HAL_FlashWrite(QCLOUD_IOT_AUTH_MODE_FILE, &mode, sizeof(DeviceAuthMode));

	if (QCLOUD_RET_SUCCESS == ret) {
		Log_i("Save Auth Mode in Soft, Auth Mode = %d", mode);
		sg_auth_mode_soft = mode;		/* 每一次设置成功刷新软件中数值 */
		return QCLOUD_RET_SUCCESS;
	}
	
	return QCLOUD_ERR_FAILURE;
#endif
}

/* CMIoT ML302 added by YangTao@20200910 */
DeviceAuthMode HAL_GetAuthMode(void)
{
#ifdef DEBUG_DEV_INFO_USED
	return sg_auth_mode;
#else
	DeviceAuthMode mode = AUTH_MODE_MAX;

	/* 防止程序运行时反复读取鉴权模式的文件 */
	if (AUTH_MODE_MAX != sg_auth_mode_soft) {
		return sg_auth_mode_soft;
	}

	int ret = HAL_FlashRead(QCLOUD_IOT_AUTH_MODE_FILE, &mode, sizeof(DeviceAuthMode));

	if (QCLOUD_RET_SUCCESS == ret) {
		Log_i("Save Auth Mode in Soft, Auth Mode = %d", mode);
		sg_auth_mode_soft = mode;		/* 防止反复读取, 第一次读之后保存到软件中 */
		return sg_auth_mode_soft;
	}
	
	return AUTH_MODE_MAX;
#endif
}


#ifdef GATEWAY_ENABLED
/* CMIoT ML302 added by YangTao@20200910 */
int HAL_SetGwDevInfo(void *pgwDeviceInfo)
{
    POINTER_SANITY_CHECK(pgwDeviceInfo, QCLOUD_ERR_DEV_INFO);
    int                ret;
    GatewayDeviceInfo *gwDevInfo = (GatewayDeviceInfo *)pgwDeviceInfo;
    memset((char *)gwDevInfo, 0, sizeof(GatewayDeviceInfo));

#ifdef DEBUG_DEV_INFO_USED
    Log_e("HAL_SetGwDevInfo not implement yet");
    ret = QCLOUD_ERR_DEV_INFO;

#else
    Log_e("HAL_SetGwDevInfo not implement yet");
    ret = QCLOUD_ERR_DEV_INFO;
#endif

    if (QCLOUD_RET_SUCCESS != ret) {
        Log_e("Get gateway device info err");
        ret = QCLOUD_ERR_DEV_INFO;
    }
    return ret;
}

int HAL_GetGwDevInfo(void *pgwDeviceInfo)
{
    POINTER_SANITY_CHECK(pgwDeviceInfo, QCLOUD_ERR_DEV_INFO);
    int                ret;
    GatewayDeviceInfo *gwDevInfo = (GatewayDeviceInfo *)pgwDeviceInfo;
    memset((char *)gwDevInfo, 0, sizeof(GatewayDeviceInfo));

#ifdef DEBUG_DEV_INFO_USED
    ret = HAL_GetDevInfo(&(gwDevInfo->gw_info));  // get gw dev info
    // only one sub-device is supported now
    gwDevInfo->sub_dev_num  = 1;
    gwDevInfo->sub_dev_info = (DeviceInfo *)HAL_Malloc(sizeof(DeviceInfo) * (gwDevInfo->sub_dev_num));
    memset((char *)gwDevInfo->sub_dev_info, '\0', sizeof(DeviceInfo));
    // copy sub dev info
    ret = device_info_copy(gwDevInfo->sub_dev_info->product_id, sg_sub_device_product_id, MAX_SIZE_OF_PRODUCT_ID);
    ret |= device_info_copy(gwDevInfo->sub_dev_info->device_name, sg_sub_device_name, MAX_SIZE_OF_DEVICE_NAME);

#else
    Log_e("HAL_GetDevInfo not implement yet");
    ret = QCLOUD_ERR_DEV_INFO;
#endif

    if (QCLOUD_RET_SUCCESS != ret) {
        Log_e("Get gateway device info err");
        ret = QCLOUD_ERR_DEV_INFO;
    }
    return ret;
}
#endif
