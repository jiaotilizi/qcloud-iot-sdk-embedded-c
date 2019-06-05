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
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"


static char sg_cert_file[PATH_MAX + 1];      //客户端证书全路径
static char sg_key_file[PATH_MAX + 1];       //客户端密钥全路径


static DeviceInfo sg_devInfo;


void response_message_callback(void* coap_message, void* userContext)
{
	int ret_code = IOT_COAP_GetMessageCode(coap_message);
	switch (ret_code) {
		case COAP_EVENT_RECEIVE_ACK:
			Log_i("message received ACK, msgid: %d", IOT_COAP_GetMessageId(coap_message));
			break;
		case COAP_EVENT_RECEIVE_RESPCONTENT:
		{
			char* payload = NULL;
			int payload_len = 0;
			int ret = -1;
			ret = IOT_COAP_GetMessagePayload(coap_message, &payload, &payload_len);
			if (ret == QCLOUD_ERR_SUCCESS) {
				Log_i("message received response, content: %s", payload);
			}
			else {
				Log_e("message received response, content error.");
			}
		}

			break;
		case COAP_EVENT_UNAUTHORIZED:
			Log_i("coap client auth token expired or invalid, msgid: %d", IOT_COAP_GetMessageId(coap_message));
			break;
		case COAP_EVENT_FORBIDDEN:
			Log_i("coap URI is invalid for this device, msgid: %d", IOT_COAP_GetMessageId(coap_message));
			break;
		case COAP_EVENT_INTERNAL_SERVER_ERROR:
			Log_i("coap server internal error, msgid: %d", IOT_COAP_GetMessageId(coap_message));
			break;
		case COAP_EVENT_ACK_TIMEOUT:
			Log_i("message receive ACK timeout, msgid: %d", IOT_COAP_GetMessageId(coap_message));
			break;
		case COAP_EVENT_SEPRESP_TIMEOUT:
			Log_i("message received ACK but receive response timeout, msgid: %d", IOT_COAP_GetMessageId(coap_message));
			break;
		default:
			break;
	}
}

void event_handler(void *pcontext, CoAPEventMessage *message)
{
	switch (message->event_type) {
		case COAP_EVENT_RECEIVE_ACK:
			Log_i("message received ACK, msgid: %d", (unsigned)(uintptr_t)message->message);
			break;
		case COAP_EVENT_RECEIVE_RESPCONTENT:
			Log_i("message received response, content: %s", IOT_COAP_GetMessageId(message->message));
			break;
		case COAP_EVENT_UNAUTHORIZED:
			Log_i("coap client auth token expired or invalid, msgid: %d", (unsigned)(uintptr_t)message->message);
			break;
		case COAP_EVENT_FORBIDDEN:
			Log_i("coap URI is invalid for this device, msgid: %d", (unsigned)(uintptr_t)message->message);
			break;
		case COAP_EVENT_INTERNAL_SERVER_ERROR:
			Log_i("coap server internal error, msgid: %d", (unsigned)(uintptr_t)message->message);
			break;
		case COAP_EVENT_ACK_TIMEOUT:
			Log_i("message receive ACK timeout, msgid: %d", (unsigned)(uintptr_t)message->message);
			break;
		case COAP_EVENT_SEPRESP_TIMEOUT:
			Log_i("message received ACK but receive response timeout, msgid: %d", (unsigned)(uintptr_t)message->message);
			break;
		default:
			Log_e("unrecogonized event type: %d", message->event_type);
			break;
	}
}

static int _setup_connect_init_params(CoAPInitParams* initParams)
{
	int ret = 0;
	DeviceAuthMode authmode = AUTH_MODE_MAX;

	/* 获取鉴权模式 */
	ret = HAL_GetAuthMode(&authmode);
	Log_d("###### HAL_GetAuthMode: mode = %d, ret = %d", authmode, ret);

	if (AUTH_MODE_CERT_TLS != authmode)
	{
		ret = HAL_SetProductID("LN19CSVR64");
		ret |= HAL_SetDevName("door1");
		ret |= HAL_SetDevSec("BhKEOITUbhtxU2z7rW+d0Q==");
	}
	else
	{		
		ret = HAL_SetProductID("6SF5233CVA");
		ret |= HAL_SetDevName("door1");
		ret |= HAL_SetDevCertName("door1_cert.crt");
		ret |= HAL_SetDevPrivateKeyName("door1_private.key");
	}
	Log_d("###### HAL_SetDevInfo: ret = %d", ret);

	ret = HAL_GetDevInfo((void *)&sg_devInfo);
	Log_d("###### HAL_GetDevInfo: ret = %d", ret);
	
	initParams->device_name = sg_devInfo.device_name;
	initParams->product_id = sg_devInfo.product_id;

	if (AUTH_MODE_CERT_TLS == authmode) {
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
	} else {
		initParams->device_secret = sg_devInfo.devSerc;
	}

	initParams->command_timeout = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
	initParams->event_handle.h_fp = event_handler;
	initParams->max_retry_count = 3;


    return QCLOUD_ERR_SUCCESS;
}


int main(int argc, char **argv)
{
	int rc = QCLOUD_ERR_SUCCESS;
	
	IOT_Log_Set_Level(DEBUG);

	HAL_SetAuthMode(AUTH_MODE_KEY_NO_TLS);
	
	CoAPInitParams init_params = DEFAULT_COAPINIT_PARAMS;
    rc = _setup_connect_init_params(&init_params);
	if (rc != QCLOUD_ERR_SUCCESS) {
		Log_e("init params err,rc=%d", rc);
		return rc;
	}

	void *coap_client = IOT_COAP_Construct(&init_params);
	if (coap_client == NULL) {
		Log_e("COAP Client construct failed.");
		return QCLOUD_ERR_FAILURE;
	}	

	

    do {
    	SendMsgParams send_params = DEFAULT_SENDMSG_PARAMS;
    	send_params.pay_load = "{\"name\":\"hello world\"}";
    	send_params.pay_load_len = strlen("{\"name\":\"hello world\"}");
    	send_params.resp_callback = response_message_callback;

        char topicName[128] = "";
        sprintf(topicName, "%s/%s/data", sg_devInfo.product_id, sg_devInfo.device_name);
        Log_i("topic name is %s", topicName);

        rc = IOT_COAP_SendMessage(coap_client, topicName, &send_params);
	    if (rc < 0) {
	        Log_e("client publish topic failed :%d.", rc);
	    }
	    else {
	    	Log_d("client topic has been sent, msg_id: %d", rc);
	    }

	    if (!(argc >= 2 && !strcmp("loop", argv[1]))) {
	    	sleep(1);
	    }

    	rc = IOT_COAP_Yield(coap_client, 200);

		if (rc != QCLOUD_ERR_SUCCESS){
			Log_e("exit with error: %d", rc);
			break;
		}

		if (argc >= 2)
			sleep(1);

    } while (argc >= 2 && !strcmp("loop", argv[1]));

    IOT_COAP_Destroy(&coap_client);

    return QCLOUD_ERR_SUCCESS;
}
