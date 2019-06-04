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
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"


#define MAX_SIZE_OF_TOPIC_CONTENT 100

static char sg_cert_file[PATH_MAX + 1];      //客户端证书全路径
static char sg_key_file[PATH_MAX + 1];       //客户端密钥全路径

static DeviceInfo sg_devInfo;


static bool sg_has_rev_ack = false;


unsigned char bcc_checksum(unsigned char* data, unsigned char len)
{
	unsigned char i, result = 0;
	
	for (i = 0; i < len; i++) {
		result ^= data[i];
		//Log_i("###### data[%d] %c(0x%02x)", (int)i, data[i], data[i]);
	}
	return result;
}


void printUsage()
{
    printf("1. ./door come_home [targetDeviceName]\n");
    printf("2. ./door leave_home [targetDeviceName]\n");
}

bool log_handler(const char* message) {
	//实现日志回调的写方法
	//实现内容后请返回true
	return false;
}

static void event_handler(void *pclient, void *handle_context, MQTTEventMsg *msg) 
{	
	uintptr_t packet_id = (uintptr_t)msg->msg;
    sg_has_rev_ack = true;

	switch(msg->event_type) {
		case MQTT_EVENT_UNDEF:
			Log_i("undefined event occur.");
			break;

		case MQTT_EVENT_DISCONNECT:
			Log_i("MQTT disconnect.");
			break;

		case MQTT_EVENT_RECONNECT:
			Log_i("MQTT reconnect.");
			break;

		case MQTT_EVENT_SUBCRIBE_SUCCESS:
			Log_i("subscribe success, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_SUBCRIBE_TIMEOUT:
			Log_i("subscribe wait ack timeout, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_SUBCRIBE_NACK:
			Log_i("subscribe nack, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_PUBLISH_SUCCESS:
			Log_i("publish success, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_PUBLISH_TIMEOUT:
			Log_i("publish timeout, packet-id=%u", (unsigned int)packet_id);
			break;

		case MQTT_EVENT_PUBLISH_NACK:
			Log_i("publish nack, packet-id=%u", (unsigned int)packet_id);
			break;
		default:
			Log_i("Should NOT arrive here.");
			break;
	}
}

/**
 * 设置MQTT connet初始化参数
 *
 * @param initParams MQTT connet初始化参数
 *
 * @return 0: 参数初始化成功  非0: 失败
 */
static int _setup_connect_init_params(MQTTInitParams* initParams)
{
	int ret = 0;
	DeviceAuthMode authmode = AUTH_MODE_MAX;

	/* 获取鉴权模式 */
	if (QCLOUD_ERR_SUCCESS != HAL_GetAuthMode(&authmode)) 
	{
		Log_e("get auth mode error!");
		return QCLOUD_ERR_FAILURE;
	}
	Log_i("###### get auth mode %d", authmode);

	ret = HAL_GetDevInfo((void *)&sg_devInfo);
	Log_i("###### HAL_GetDevInfo ret = %d", ret);

	if (AUTH_MODE_CERT_TLS != authmode)
	{
		ret = HAL_SetProductID("LN19CSVR64");
		ret |= HAL_SetDevName("door1");
		ret |= HAL_SetDevSec("BhKEOITUbhtxU2z7rW+d0Q==");
		Log_i("###### HAL_Set ret = %d", ret);
	}
	else
	{		
		ret = HAL_SetProductID("6SF5233CVA");
		ret |= HAL_SetDevName("door1");
		ret |= HAL_SetDevCertName("door1_cert.crt");
		ret |= HAL_SetDevPrivateKeyName("door1_private.key");
		Log_i("###### HAL_Set ret = %d", ret);
	}

	ret = HAL_GetDevInfo((void *)&sg_devInfo);
	Log_i("###### HAL_GetDevInfo ret = %d", ret);

	initParams->product_id = sg_devInfo.product_id;
	initParams->device_name = sg_devInfo.device_name;

	if (AUTH_MODE_CERT_TLS == authmode)
	{
		/* 使用非对称加密*/
		char certs_dir[PATH_MAX + 1] = "certs";
		char current_path[PATH_MAX + 1];
		char *cwd = getcwd(current_path, sizeof(current_path));
		if (cwd == NULL)
		{
			Log_e("getcwd return NULL");
			return QCLOUD_ERR_FAILURE;
		}
		sprintf(sg_cert_file, "%s/%s/%s", current_path, certs_dir, sg_devInfo.devCertFileName);
		sprintf(sg_key_file, "%s/%s/%s", current_path, certs_dir, sg_devInfo.devPrivateKeyFileName);

		initParams->cert_file = sg_cert_file;
		initParams->key_file = sg_key_file;
	}
	else
	{	
    	initParams->device_secret = sg_devInfo.devSerc;
		unsigned char bcc = bcc_checksum((unsigned char*)sg_devInfo.devSerc, strlen(sg_devInfo.devSerc));
		Log_i("###### bcc 0x%02x", bcc);
	}

	initParams->command_timeout = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
	initParams->keep_alive_interval_ms = QCLOUD_IOT_MQTT_KEEP_ALIVE_INTERNAL;
	initParams->auto_connect_enable = 1;
    initParams->event_handle.h_fp = event_handler;
	
	Log_i("###### product_id %s, device_name %s, device_secret %s", 
			initParams->product_id, initParams->device_name, initParams->device_secret);

	
	Log_i("###### HAL_GetDevInfo: product_id %s, device_name %s, device_secret %s, devCertFileName %s, devPrivateKeyFileName %s", 
			sg_devInfo.product_id, sg_devInfo.device_name, sg_devInfo.devSerc, 
			sg_devInfo.devCertFileName, sg_devInfo.devPrivateKeyFileName);
	
    return QCLOUD_ERR_SUCCESS;
}

/**
 * 发送topic消息
 *
 * @param action 行为
 */
static int _publish_msg(void *client, char* action, char* targetDeviceName)
{
    if(NULL == action || NULL == targetDeviceName) 
        return -1;

    char topic_name[128] = {0};
    sprintf(topic_name,"%s/%s/%s", sg_devInfo.product_id, sg_devInfo.device_name, "event");

    PublishParams pub_params = DEFAULT_PUB_PARAMS;
    pub_params.qos = QOS1;

    char topic_content[MAX_SIZE_OF_TOPIC_CONTENT + 1] = {0};
    if(strcmp(action, "come_home") == 0 || strcmp(action, "leave_home") == 0)
    {
        int size = HAL_Snprintf(topic_content, sizeof(topic_content), "{\"action\": \"%s\", \"targetDevice\": \"%s\"}", action, targetDeviceName);
        if (size < 0 || size > sizeof(topic_content) - 1)
        {
            Log_e("payload content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_content));
            return -3;
        }

        pub_params.payload = topic_content;
        pub_params.payload_len = strlen(topic_content);
    }
    else
    {
        printUsage();
        return -2;
    }

    int rc = IOT_MQTT_Publish(client, topic_name, &pub_params);
    if (rc < 0) {
        Log_e("Client publish Topic:%s Failed :%d with content: %s", topic_name, rc, (char*)pub_params.payload);
        return rc;
    } 
    return 0;
}

int main(int argc, char **argv) 
{
    if(argc != 3) {
        printUsage();
        return -1;
    }

	DeviceAuthMode mode = 0;
    //init log level
    IOT_Log_Set_Level(DEBUG);
    IOT_Log_Set_MessageHandler(log_handler);

    int rc;

	HAL_GetAuthMode(&mode);
	mode = atoi(argv[2]);
	HAL_SetAuthMode(mode);

    //init connection
    MQTTInitParams init_params = DEFAULT_MQTTINIT_PARAMS;
    rc = _setup_connect_init_params(&init_params);
	if (rc != QCLOUD_ERR_SUCCESS) {
		Log_e("init params err,rc=%d", rc);
		return rc;
	}

    void *client = IOT_MQTT_Construct(&init_params);
    if (client != NULL) {
        Log_i("Cloud Device Construct Success");
    } else {
        Log_e("Cloud Device Construct Failed");
        return QCLOUD_ERR_FAILURE;
    }

	rc = IOT_MQTT_Destroy(&client);
	Log_i("### IOT_MQTT_Destroy rc = %d", rc);

	client = IOT_MQTT_Construct(&init_params);
    if (client != NULL) {
        Log_i("### Cloud Device Construct Success");
    } else {
        Log_e("### Cloud Device Construct Failed");
        return QCLOUD_ERR_FAILURE;
    }

    //publish msg
    char* action = argv[1];
    char* target_device_name = argv[2];
    rc = _publish_msg(client, action, target_device_name);
    if (rc < 0) {
        Log_e("Demo publish fail rc=%d", rc);
    }

    while (IOT_MQTT_IsConnected(client) || 
        rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT || 
        rc == QCLOUD_ERR_MQTT_RECONNECTED ) 
    {
        if (false != sg_has_rev_ack) break;
        Log_i("Wait for publish ack");
        rc = IOT_MQTT_Yield(client, 200);
        sleep(1);
    }

    rc = IOT_MQTT_Destroy(&client);

    return rc;
}
