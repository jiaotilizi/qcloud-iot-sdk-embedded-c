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

#ifdef __cplusplus
extern "C" {
#endif

#include "device.h"

#include <stdbool.h>
#include <string.h>

static DeviceInfo   sg_device_info;
static bool         sg_devinfo_initialized;


static char sg_flash_psk_file[] = "/usr/tencentyun_psk.txt";    /* 腾讯云产品设备密钥文件 */


int IOT_Device_Info_Flash_Read(DeviceInfo *device_info) 
{
	FILE *fp = NULL;
	int read_size = 0;

	POINTER_SANITY_CHECK(device_info, QCLOUD_ERR_INVAL);
	
	memset(device_info, 0x0, sizeof(DeviceInfo));
	
	if (NULL == (fp = fopen(sg_flash_psk_file, "r+")))
	{
		Log_e("Open Device Info File Failed");
		return QCLOUD_ERR_FAILURE;
	}

	read_size = fread(device_info, sizeof(DeviceInfo), 1, fp);
	fclose(fp);

	return (1 == read_size)? QCLOUD_ERR_SUCCESS : QCLOUD_ERR_FAILURE;
}

int IOT_Device_Info_Flash_Write(DeviceInfo *device_info) 
{
	FILE *fp = NULL;
	int write_size = 0;

	POINTER_SANITY_CHECK(device_info, QCLOUD_ERR_INVAL);
	
	if ((MAX_SIZE_OF_PRODUCT_ID) < strlen(device_info->product_id))
	{
		Log_e("product name(%s) length:(%lu) exceeding limitation", device_info->product_id, strlen(device_info->product_id));
		return QCLOUD_ERR_FAILURE;
	}
	if ((MAX_SIZE_OF_DEVICE_NAME) < strlen(device_info->device_name))
	{
		Log_e("device name(%s) length:(%lu) exceeding limitation", device_info->device_name, strlen(device_info->device_name));
		return QCLOUD_ERR_FAILURE;
	}
	if ((MAX_SIZE_OF_DEVICE_SERC) < strlen(device_info->devSerc))
	{
		Log_e("device secret(%s) length:(%lu) exceeding limitation", device_info->devSerc, strlen(device_info->devSerc));
		return QCLOUD_ERR_FAILURE;
	}
	
	if (NULL == (fp = fopen(sg_flash_psk_file, "w+")))
	{
		Log_e("Open Device Info File Failed");
		return QCLOUD_ERR_FAILURE;
	}

	write_size = fwrite(device_info, sizeof(DeviceInfo), 1, fp);
	fclose(fp);

	return (1 == write_size)? QCLOUD_ERR_SUCCESS : QCLOUD_ERR_FAILURE;
}


int iot_device_info_init() {
	if (sg_devinfo_initialized) {
		Log_e("device info has been initialized.");
		return 0;
	}

	memset(&sg_device_info, 0x0, sizeof(DeviceInfo));
	sg_devinfo_initialized = true;
	
	return QCLOUD_ERR_SUCCESS;
}

int iot_device_info_set(const char *product_id, const char *device_name) {	

	memset(&sg_device_info, 0x0, sizeof(DeviceInfo));
	if ((MAX_SIZE_OF_PRODUCT_ID) < strlen(product_id))
	{
		Log_e("product name(%s) length:(%lu) exceeding limitation", product_id, strlen(product_id));
		return QCLOUD_ERR_FAILURE;
	}
	if ((MAX_SIZE_OF_DEVICE_NAME) < strlen(device_name))
	{
		Log_e("device name(%s) length:(%lu) exceeding limitation", device_name, strlen(device_name));
		return QCLOUD_ERR_FAILURE;
	}

	strncpy(sg_device_info.product_id, product_id, MAX_SIZE_OF_PRODUCT_ID);
	strncpy(sg_device_info.device_name, device_name, MAX_SIZE_OF_DEVICE_NAME);

	/* construct device-id(@product_id+@device_name) */
	memset(sg_device_info.client_id, 0x0, MAX_SIZE_OF_CLIENT_ID);
    int ret = HAL_Snprintf(sg_device_info.client_id, MAX_SIZE_OF_CLIENT_ID, "%s%s", product_id, device_name);
    if ((ret < 0) || (ret >= MAX_SIZE_OF_CLIENT_ID)) {
        Log_e("set device info failed");
        return QCLOUD_ERR_FAILURE;
    }

    Log_i("SDK_Ver: %s, Product_ID: %s, Device_Name: %s", QCLOUD_IOT_DEVICE_SDK_VERSION, product_id, device_name);
	return QCLOUD_ERR_SUCCESS;
}

DeviceInfo* iot_device_info_get(void)
{
    return &sg_device_info;
}

#ifdef __cplusplus
}
#endif
