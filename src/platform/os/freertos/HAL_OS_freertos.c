/*
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.

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
#include <stdarg.h>

#include <unistd.h>
#include <lwip/sys.h>

#include "FreeRTOS.h"
#include "task.h"
#include "osi_api.h"
#include "osi_internal.h"
#include "vfs.h"

#include "qcloud_iot_import.h"
#include "qcloud_iot_export.h"


//#define DEBUG_DEV_INFO_USED

#ifdef DEBUG_DEV_INFO_USED

/* 产品名称, 与云端同步设备状态时需要  */
static char sg_product_id[MAX_SIZE_OF_PRODUCT_ID + 1]	 = "PRODUCT_ID";
/* 产品密钥, 若使能动态注册功能，控制台生成，必填。若不使能，则不用赋值  */
static char sg_product_secret[MAX_SIZE_OF_PRODUCT_KEY + 1]  = "YOUR_PRODUCT_SECRET";
/* 设备名称, 与云端同步设备状态时需要 */
static char sg_device_name[MAX_SIZE_OF_DEVICE_NAME + 1]  = "YOUR_DEVICE_NAME";

/* 客户端证书文件名  非对称加密使用, TLS 证书认证方式*/
static char sg_device_cert_file_name[MAX_SIZE_OF_DEVICE_CERT_FILE_NAME + 1]      = "YOUR_DEVICE_NAME_cert.crt";
/* 客户端私钥文件名 非对称加密使用, TLS 证书认证方式*/
static char sg_device_privatekey_file_name[MAX_SIZE_OF_DEVICE_KEY_FILE_NAME + 1] = "YOUR_DEVICE_NAME_private.key";

/* 设备密钥, TLS PSK认证方式*/
static char sg_device_secret[MAX_SIZE_OF_DEVICE_SERC + 1] = "YOUR_IOT_PSK";

/* 鉴权模式 */
static DeviceAuthMode sg_auth_mode = 0;

#else

/* 结构体形式存放设备基本信息文件 */
#define QCLOUD_IOT_DEVICE_INFO_FILE		"/TencentDevInfo.txt"
/* 存放鉴权模式文件 */
#define QCLOUD_IOT_AUTH_MODE_FILE		"/TencentAuthMode.txt"
/* 防止程序运行时反复读取鉴权模式的文件 */
static DeviceAuthMode sg_auth_mode_soft = AUTH_MODE_MAX;

#endif

bool HAL_ReadFromFile(const char *filename, uint8_t **dataBuff, uint32_t *dataLength)
{
    Log_i("filename:%s", filename);
	
    int fd = -1;
	int read_size = 0;
    uint32_t file_size = 0;
	struct stat st;

	/* 打开文件 */
    fd = vfs_open(filename, O_RDONLY, 0);
    if (fd < 0) {
        Log_e("vfs_open error, fd:%d", fd);
        return false;
    }

	/* 获取文件信息 */
    vfs_fstat(fd, &st);
    file_size = st.st_size;
	Log_i("fd:%d, vfs_file_size:%d", fd, file_size);
    if (file_size < 0) {
		Log_e("vfs_fstat error");
        vfs_close(fd);
        return false;
    }

	/* 处理入参 buffer */
    if (*dataBuff != NULL) {
    	Log_i("free dataBuff!!");
        HAL_Free(*dataBuff);
        *dataBuff = NULL;
    }
	
    *dataBuff = (uint8_t *)HAL_Malloc(file_size + 1);		/* 预留字符截止位 */
    if (NULL == *dataBuff) {
    	Log_e("malloc error!!");
        vfs_close(fd);
        return false;
    }
    memset(*dataBuff, 0, file_size + 1);

	/* 读取文件内容存到 buffer 中 */
    read_size = vfs_read(fd, *dataBuff, file_size);
    if (read_size != file_size) {
    	Log_e("vfs_read error!! read_size:%d, file_size:%d", read_size, file_size);
        HAL_Free(*dataBuff);
        *dataBuff = NULL;
        vfs_close(fd);
        return false;
    }
	*dataLength = file_size;

	/* 关闭文件 */
    vfs_close(fd);
	
    return true;
}

bool HAL_WriteToFile(const char *filename, uint8_t *dataBuff, uint32_t dataLength)
{
	Log_i("filename:%s, datalen:%d", filename, dataLength);

    int fd = -1;
    fd = vfs_open(filename, O_WRONLY | O_TRUNC, 0);
    if (fd < 0) {
		Log_i("Begin to Creat File:%s", filename);
        fd = vfs_open(filename, O_CREAT | O_WRONLY | O_TRUNC);
        if (fd < 0) {
			Log_e("vfs_open error, fd:%d", fd);
            return false;
        }
    }

    if (dataLength != vfs_write(fd, dataBuff, dataLength)) {
		Log_e("vfs_write error!! dataLength:%d", dataLength);
        vfs_close(fd);
        return false;
    }
    vfs_close(fd);
	
    return true;
}


void *HAL_MutexCreate(void)
{
	osiMutex_t *mutex = NULL;
	
	mutex = osiMutexCreate();
	
	if (NULL == mutex) {
        HAL_Printf("%s: create mutex failed", __FUNCTION__);
        return NULL;
    }
	
	return mutex;
}

void HAL_MutexDestroy(_IN_ void *mutex)
{
    if (NULL == mutex) {
		HAL_Printf("%s: destroy mutex failed", __FUNCTION__);
        return;
    }

    osiMutexDelete((osiMutex_t *)mutex);
}

void HAL_MutexLock(_IN_ void *mutex)
{
	if (NULL == mutex) {
		HAL_Printf("%s: Lock mutex failed", __FUNCTION__);
		return;
	}

	osiMutexLock((osiMutex_t *)mutex);
}

int HAL_MutexTryLock(_IN_ void *mutex)
{
    return osiMutexTryLock((osiMutex_t *)mutex, 0);
}


void HAL_MutexUnlock(_IN_ void *mutex)
{
    if (NULL == mutex) {
		HAL_Printf("%s: Unlock mutex failed", __FUNCTION__);
        return;
    }

    osiMutexUnlock((osiMutex_t *)mutex);
}

void *HAL_Malloc(_IN_ uint32_t size)
{
    return malloc(size);
}

void HAL_Free(_IN_ void *ptr)
{
    free(ptr);
}

void HAL_Printf(_IN_ const char *fmt, ...)
{
    va_list args;
	char buffer[1024] = {0};

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
    va_end(args);

	sys_arch_printf("CMIoT-TENCENT-SDK %s", buffer);

    //fflush(stdout);
}

int HAL_Snprintf(_IN_ char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);

    return rc;
}

int HAL_Vsnprintf(_IN_ char *str, _IN_ const int len, _IN_ const char *format, va_list ap)
{
    return vsnprintf(str, len, format, ap);
}

uint32_t HAL_UptimeMs(void)
{
    struct timeval time_val = {0};
    uint32_t time_ms;

    gettimeofday(&time_val, NULL);
    time_ms = time_val.tv_sec * 1000 + time_val.tv_usec / 1000;

    return time_ms;
}

void HAL_SleepMs(_IN_ uint32_t ms)
{
	vTaskDelay(osiMsToOSTick(ms));
    //usleep(1000 * ms);
}

#ifndef DEBUG_DEV_INFO_USED
int HAL_DevInfoFlashRead(void *pdevInfo) 
{
	int fd = -1;
	int read_size = 0;
	DeviceInfo *devInfo = (DeviceInfo *)pdevInfo;

	//POINTER_SANITY_CHECK(device_info, QCLOUD_ERR_INVAL);
	
	memset((char *)devInfo, 0, sizeof(DeviceInfo));

	/* 文件不存在时, 则首次进行创建, 并写入初始数值 */
	if (0 > (fd = vfs_open(QCLOUD_IOT_DEVICE_INFO_FILE, O_RDONLY, 0))) {
		if (0 < (fd = vfs_creat(QCLOUD_IOT_DEVICE_INFO_FILE, 0))) {
			vfs_write(fd, devInfo, sizeof(DeviceInfo));
			vfs_lseek(fd, 0, SEEK_SET);
			Log_i("Creat File: %s!", QCLOUD_IOT_DEVICE_INFO_FILE);
		} else {
			Log_e("Creat File: %s Failed!", QCLOUD_IOT_DEVICE_INFO_FILE);
			return QCLOUD_ERR_FAILURE;
		}
	}

	read_size = vfs_read(fd, devInfo, sizeof(DeviceInfo));
	Log_i("File: %s, read_size = %d, fize_size = %d, fd = %d", QCLOUD_IOT_DEVICE_INFO_FILE, read_size, sizeof(DeviceInfo), fd);
	
	vfs_close(fd);
	
	return (sizeof(DeviceInfo) == read_size)? QCLOUD_ERR_SUCCESS : QCLOUD_ERR_FAILURE;
}

int HAL_DevInfoFlashWrite(void *pdevInfo) 
{
	int fd = -1;
	int write_size = 0;
	DeviceInfo *devInfo = (DeviceInfo *)pdevInfo;

	//POINTER_SANITY_CHECK(device_info, QCLOUD_ERR_INVAL);
	
	if ((MAX_SIZE_OF_PRODUCT_ID) < strlen(devInfo->product_id)) {
		Log_e("product name(%s) length:(%lu) exceeding limitation", devInfo->product_id, strlen(devInfo->product_id));
		return QCLOUD_ERR_FAILURE;
	}
	if ((MAX_SIZE_OF_DEVICE_NAME) < strlen(devInfo->device_name)) {
		Log_e("device name(%s) length:(%lu) exceeding limitation", devInfo->device_name, strlen(devInfo->device_name));
		return QCLOUD_ERR_FAILURE;
	}
	if ((MAX_SIZE_OF_DEVICE_SERC) < strlen(devInfo->devSerc)) {
		Log_e("device secret(%s) length:(%lu) exceeding limitation", devInfo->devSerc, strlen(devInfo->devSerc));
		return QCLOUD_ERR_FAILURE;
	}
	
	if (0 > (fd = vfs_creat(QCLOUD_IOT_DEVICE_INFO_FILE, 0))) {
		Log_e("Open Device Info File Failed");
		return QCLOUD_ERR_FAILURE;
	}

	write_size = vfs_write(fd, devInfo, sizeof(DeviceInfo));
	Log_i("File: %s, write_size = %d, fize_size = %d, fd = %d", QCLOUD_IOT_DEVICE_INFO_FILE, write_size, sizeof(DeviceInfo), fd);
	
	vfs_close(fd);

	return (sizeof(DeviceInfo) == write_size)? QCLOUD_ERR_SUCCESS : QCLOUD_ERR_FAILURE;
}
#endif

int HAL_GetProductID(char *pProductId, uint8_t maxlen)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(sg_product_id) > maxlen){
		return QCLOUD_ERR_FAILURE;
	}

	memset(pProductId, '\0', maxlen);
	strncpy(pProductId, sg_product_id, maxlen);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pProductId) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	if (maxlen < strlen(device_info.product_id)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}
	
	memset(pProductId, '\0', maxlen);
	strncpy(pProductId, device_info.product_id, maxlen);

	return ret;
#endif
}

int HAL_GetProductKey(char *pProductKey, uint8_t maxlen)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(sg_product_secret) > maxlen){
		return QCLOUD_ERR_FAILURE;
	}

	memset(pProductKey, '\0', maxlen);
	strncpy(pProductKey, sg_product_secret, maxlen);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pProductKey) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	if (maxlen < strlen(device_info.product_key)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}
	
	memset(pProductKey, '\0', maxlen);
	strncpy(pProductKey, device_info.product_key, maxlen);

	return ret;
#endif
}


int HAL_GetDevName(char *pDevName, uint8_t maxlen)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(sg_device_name) > maxlen){
		return QCLOUD_ERR_FAILURE;
	}

	memset(pDevName, '\0', maxlen);
	strncpy(pDevName, sg_device_name, maxlen);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pDevName) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	if (maxlen < strlen(device_info.device_name)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}
	
	memset(pDevName, '\0', maxlen);
	strncpy(pDevName, device_info.device_name, maxlen);

	return ret;
#endif
}


int HAL_SetProductID(const char *pProductId)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(pProductId) > MAX_SIZE_OF_PRODUCT_ID){
		return QCLOUD_ERR_FAILURE;
	}

	memset(sg_product_id, '\0', MAX_SIZE_OF_PRODUCT_ID);
	strncpy(sg_product_id, pProductId, MAX_SIZE_OF_PRODUCT_ID);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pProductId) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	if (MAX_SIZE_OF_PRODUCT_ID < strlen(pProductId)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	strncpy(device_info.product_id, pProductId, MAX_SIZE_OF_PRODUCT_ID);
	ret |= HAL_DevInfoFlashWrite(&device_info);

	return ret;
#endif
}


int HAL_SetProductKey(const char *pProductKey)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(pProductKey) > MAX_SIZE_OF_PRODUCT_KEY){
		return QCLOUD_ERR_FAILURE;
	}

	memset(sg_product_secret, '\0', MAX_SIZE_OF_PRODUCT_KEY);
	strncpy(sg_product_secret, pProductKey, MAX_SIZE_OF_PRODUCT_KEY);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pProductKey) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	if (MAX_SIZE_OF_PRODUCT_KEY < strlen(pProductKey)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));	
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	strncpy(device_info.product_key, pProductKey, MAX_SIZE_OF_PRODUCT_KEY);
	ret |= HAL_DevInfoFlashWrite(&device_info);

	return ret;
#endif

}

int HAL_SetDevName(const char *pDevName)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(pDevName) > MAX_SIZE_OF_DEVICE_NAME){
		return QCLOUD_ERR_FAILURE;
	}

	memset(sg_device_name, '\0', MAX_SIZE_OF_DEVICE_NAME);
	strncpy(sg_device_name, pDevName, MAX_SIZE_OF_DEVICE_NAME);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pDevName) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	if (MAX_SIZE_OF_DEVICE_NAME < strlen(pDevName)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	strncpy(device_info.device_name, pDevName, MAX_SIZE_OF_DEVICE_NAME);
	ret |= HAL_DevInfoFlashWrite(&device_info);

	return ret;
#endif
}

int HAL_GetDevCertName(char *pDevCert, uint8_t maxlen)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(sg_device_cert_file_name) > maxlen){
		return QCLOUD_ERR_FAILURE;
	}

	memset(pDevCert, '\0', maxlen);
	strncpy(pDevCert, sg_device_cert_file_name, maxlen);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pDevCert) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	if (maxlen < strlen(device_info.devCertFileName)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}
	
	memset(pDevCert, '\0', maxlen);
	strncpy(pDevCert, device_info.devCertFileName, maxlen);

	return ret;
#endif
}

int HAL_GetDevPrivateKeyName(char *pDevPrivateKey, uint8_t maxlen)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(sg_device_privatekey_file_name) > maxlen){
		return QCLOUD_ERR_FAILURE;
	}

	memset(pDevPrivateKey, '\0', maxlen);
	strncpy(pDevPrivateKey, sg_device_privatekey_file_name, maxlen);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pDevPrivateKey) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	if (maxlen < strlen(device_info.devPrivateKeyFileName)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}
	
	memset(pDevPrivateKey, '\0', maxlen);
	strncpy(pDevPrivateKey, device_info.devPrivateKeyFileName, maxlen);

	return ret;
#endif

}

int HAL_SetDevCertName(char *pDevCert)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(pDevCert) > MAX_SIZE_OF_DEVICE_CERT_FILE_NAME){
		return QCLOUD_ERR_FAILURE;
	}

	memset(sg_device_cert_file_name, '\0', MAX_SIZE_OF_DEVICE_CERT_FILE_NAME);
	strncpy(sg_device_cert_file_name, pDevCert, MAX_SIZE_OF_DEVICE_CERT_FILE_NAME);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pDevCert) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	if (MAX_SIZE_OF_DEVICE_CERT_FILE_NAME < strlen(pDevCert)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	strncpy(device_info.devCertFileName, pDevCert, MAX_SIZE_OF_DEVICE_CERT_FILE_NAME);
	ret |= HAL_DevInfoFlashWrite(&device_info);

	return ret;
#endif
}

int HAL_SetDevPrivateKeyName(char *pDevPrivateKey)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(pDevPrivateKey) > MAX_SIZE_OF_DEVICE_KEY_FILE_NAME){
		return QCLOUD_ERR_FAILURE;
	}

	memset(sg_device_privatekey_file_name, '\0', MAX_SIZE_OF_DEVICE_KEY_FILE_NAME);
	strncpy(sg_device_privatekey_file_name, pDevPrivateKey, MAX_SIZE_OF_DEVICE_KEY_FILE_NAME);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pDevPrivateKey) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	if (MAX_SIZE_OF_DEVICE_KEY_FILE_NAME < strlen(pDevPrivateKey)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	strncpy(device_info.devPrivateKeyFileName, pDevPrivateKey, MAX_SIZE_OF_DEVICE_KEY_FILE_NAME);
	ret |= HAL_DevInfoFlashWrite(&device_info);

	return ret;
#endif
}


int HAL_GetDevSec(char *pDevSec, uint8_t maxlen)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(sg_device_secret) > maxlen){
		return QCLOUD_ERR_FAILURE;
	}

	memset(pDevSec, '\0', maxlen);
	strncpy(pDevSec, sg_device_secret, maxlen);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pDevSec) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	if (maxlen < strlen(device_info.devSerc)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}
	
	memset(pDevSec, '\0', maxlen);
	strncpy(pDevSec, device_info.devSerc, maxlen);

	return ret;
#endif


}

int HAL_SetDevSec(const char *pDevSec)
{
#ifdef DEBUG_DEV_INFO_USED
	if(strlen(pDevSec) > MAX_SIZE_OF_DEVICE_SERC){
		return QCLOUD_ERR_FAILURE;
	}

	memset(sg_device_secret, '\0', MAX_SIZE_OF_DEVICE_SERC);
	strncpy(sg_device_secret, pDevSec, MAX_SIZE_OF_DEVICE_SERC);

	return QCLOUD_ERR_SUCCESS;
#else
	int ret = 0;
	DeviceInfo device_info;

	if (NULL == pDevSec) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	if (MAX_SIZE_OF_DEVICE_SERC < strlen(pDevSec)) {
		Log_e("exceeds the max length!");
		return QCLOUD_ERR_FAILURE;
	}

	memset(&device_info, 0, sizeof(DeviceInfo));
	ret |= HAL_DevInfoFlashRead(&device_info);
	
	strncpy(device_info.devSerc, pDevSec, MAX_SIZE_OF_DEVICE_SERC);
	ret |= HAL_DevInfoFlashWrite(&device_info);

	return ret;
#endif
}


int HAL_GetDevInfo(void *pdevInfo)
{
	int ret;
	DeviceInfo *devInfo = (DeviceInfo *)pdevInfo;
	DeviceAuthMode authmode = AUTH_MODE_MAX;
		
	memset((char *)devInfo, 0, sizeof(DeviceInfo));
	ret = HAL_GetProductID(devInfo->product_id, MAX_SIZE_OF_PRODUCT_ID);
	ret |= HAL_GetDevName(devInfo->device_name, MAX_SIZE_OF_DEVICE_NAME); 
	
	ret |= HAL_GetAuthMode(&authmode);
	if (AUTH_MODE_CERT_TLS == authmode)
	{
		ret |= HAL_GetDevCertName(devInfo->devCertFileName, MAX_SIZE_OF_DEVICE_CERT_FILE_NAME);
		ret |= HAL_GetDevPrivateKeyName(devInfo->devPrivateKeyFileName, MAX_SIZE_OF_DEVICE_KEY_FILE_NAME);
	}
	else
	{
		ret |= HAL_GetDevSec(devInfo->devSerc, MAX_SIZE_OF_DEVICE_SERC);
	}

	if(QCLOUD_ERR_SUCCESS != ret){
		Log_e("Get device info err");
		ret = QCLOUD_ERR_DEV_INFO;
	}

	return ret;
}

int HAL_GetAuthMode(DeviceAuthMode *mode)
{
#ifdef DEBUG_DEV_INFO_USED
	if(mode == NULL) {
		return QCLOUD_ERR_FAILURE;
	}

	*mode = sg_auth_mode;

	return QCLOUD_ERR_SUCCESS;
#else
	int fd = -1;
	int read_size = 0;

	if (NULL == mode) {
		Log_e("ptr is NULL!");
		return QCLOUD_ERR_FAILURE;
	}

	/* 防止程序运行时反复读取鉴权模式的文件 */
	if (AUTH_MODE_MAX != sg_auth_mode_soft) {
		//Log_i("Get Auth Mode from Soft, Auth Mode = %d", sg_auth_mode_soft);
		*mode = sg_auth_mode_soft;
		return QCLOUD_ERR_SUCCESS;
	}

	/* 文件不存在时, 则首次进行创建, 并写入初始数值 */
	if (0 > (fd = vfs_open(QCLOUD_IOT_AUTH_MODE_FILE, O_RDONLY, 0))) {
		if (0 < (fd = vfs_creat(QCLOUD_IOT_AUTH_MODE_FILE, 0))) {
			DeviceAuthMode init_mode = 0;
			vfs_write(fd, &init_mode, sizeof(DeviceAuthMode));
			vfs_lseek(fd, 0, SEEK_SET);
			Log_i("Creat File: %s!", QCLOUD_IOT_AUTH_MODE_FILE);
		} else {
			Log_e("Creat File: %s Failed!", QCLOUD_IOT_AUTH_MODE_FILE);
			return QCLOUD_ERR_FAILURE;
		}
	}

	read_size = vfs_read(fd, mode, sizeof(DeviceAuthMode));
	Log_i("File: %s, read_size = %d, fize_size = %d, fd = %d", QCLOUD_IOT_AUTH_MODE_FILE, read_size, sizeof(DeviceAuthMode), fd);
	
	vfs_close(fd);

	if (sizeof(DeviceAuthMode) == read_size) {
		Log_i("Save Auth Mode in Soft, Auth Mode = %d", *mode);
		sg_auth_mode_soft = *mode;		/* 防止反复读取, 第一次读之后保存到软件中 */
		return QCLOUD_ERR_SUCCESS;
	}
	
	return QCLOUD_ERR_FAILURE;
#endif
}

int HAL_SetAuthMode(DeviceAuthMode mode)
{
#ifdef DEBUG_DEV_INFO_USED
	if(mode >= AUTH_MODE_MAX) {
		return QCLOUD_ERR_FAILURE;
	}

	sg_auth_mode = mode;

	return QCLOUD_ERR_SUCCESS;
#else
	int fd = -1;
	int write_size = 0;

	if (AUTH_MODE_MAX <= mode) {
		Log_e("Exceeds the max mode!");
		return QCLOUD_ERR_FAILURE;
	}

	if (0 > (fd = vfs_creat(QCLOUD_IOT_AUTH_MODE_FILE, 0))) {
		Log_e("Open Auth Mode File Failed");
		return QCLOUD_ERR_FAILURE;
	}

	write_size = vfs_write(fd, &mode, sizeof(DeviceAuthMode));
	Log_i("File: %s, write_size = %d, fize_size = %d, fd = %d", QCLOUD_IOT_AUTH_MODE_FILE, write_size, sizeof(DeviceAuthMode), fd);
	
	vfs_close(fd);

	if (sizeof(DeviceAuthMode) == write_size) {
		Log_i("Save Auth Mode in Soft, Auth Mode = %d", mode);
		sg_auth_mode_soft = mode;		/* 每一次设置成功刷新软件中数值 */
		return QCLOUD_ERR_SUCCESS;
	}
	
	return QCLOUD_ERR_FAILURE;
#endif
}


