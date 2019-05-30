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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "mqtt_client.h"

#include "ca.h"
#include "device.h"
#include "qcloud_iot_import.h"
#include "qcloud_iot_export.h"

#include "utils_base64.h"
#include "utils_list.h"
#include "log_upload.h"
#include "lite-utils.h"

#define HOST_STR_LENGTH 64
static char s_qcloud_iot_host[HOST_STR_LENGTH] = {0};
// AUTH_WITH_NOTLS
static int s_qcloud_iot_port_no_tls = 1883;
static int s_qcloud_iot_port_tls = 8883;

// AUTH_MODE_KEY_TLS
#define DECODE_PSK_LENGTH 48		/*控制台允许的最大长度为64，对应到原文最大长度64/4*3 = 48*/
static unsigned char sg_psk_str[DECODE_PSK_LENGTH];



static uint16_t _get_random_start_packet_id(void)
{
    srand((unsigned)time(NULL));
    return rand() % 65536 + 1;
}

void* IOT_MQTT_Construct(MQTTInitParams *pParams)
{
	POINTER_SANITY_CHECK(pParams, NULL);
	STRING_PTR_SANITY_CHECK(pParams->product_id, NULL);
	STRING_PTR_SANITY_CHECK(pParams->device_name, NULL);
    
    iot_device_info_init();
	if (iot_device_info_set(pParams->product_id, pParams->device_name) != QCLOUD_ERR_SUCCESS)
    {
        Log_e("failed to set device info!");
        return NULL;
    }

	Qcloud_IoT_Client* mqtt_client = NULL;
	DeviceAuthMode authmode = AUTH_MODE_MAX;

	// 初始化MQTTClient
	if ((mqtt_client = (Qcloud_IoT_Client*) HAL_Malloc (sizeof(Qcloud_IoT_Client))) == NULL) {
		Log_e("malloc MQTTClient failed");
		return NULL;
	}

	int rc = qcloud_iot_mqtt_init(mqtt_client, pParams);
	if (rc != QCLOUD_ERR_SUCCESS) {
		Log_e("mqtt init failed: %d", rc);
		HAL_Free(mqtt_client);
		return NULL;
	}

	MQTTConnectParams connect_params = DEFAULT_MQTTCONNECT_PARAMS;
	connect_params.client_id = iot_device_info_get()->client_id;
    // 超过11.5分钟的心跳间隔自动转为11.5（11.5 * 60）的时长
	connect_params.keep_alive_interval = Min(pParams->keep_alive_interval_ms / 1000, 690);
	connect_params.clean_session = pParams->clean_session;
	connect_params.auto_connect_enable = pParams->auto_connect_enable;

	/* 获取鉴权模式 */
	if (QCLOUD_ERR_SUCCESS != HAL_GetAuthMode(&authmode)) 
	{
		Log_e("get auth mode error!");
		return NULL;
	}

	if (AUTH_MODE_KEY_NO_TLS == authmode)
	{
		size_t src_len = strlen(pParams->device_secret);
		size_t len;
		memset(sg_psk_str, 0x00, DECODE_PSK_LENGTH);
		rc = qcloud_iot_utils_base64decode(sg_psk_str, sizeof( sg_psk_str ), &len, (unsigned char *)pParams->device_secret, src_len );
		connect_params.device_secret = (char *)sg_psk_str;
		connect_params.device_secret_len = len;
		if (rc != QCLOUD_ERR_SUCCESS) {
			Log_e("Device secret decode err, secret:%s", pParams->device_secret);
			HAL_Free(mqtt_client);
			return NULL;
		}
	}

	rc = qcloud_iot_mqtt_connect(mqtt_client, &connect_params);
	if (rc != QCLOUD_ERR_SUCCESS) {
		Log_e("mqtt connect with id: %s failed: %d", mqtt_client->options.conn_id, rc);
		HAL_Free(mqtt_client);
		return NULL;
	}
	else {
		Log_i("mqtt connect with id: %s success", mqtt_client->options.conn_id);
	}

#ifdef LOG_UPLOAD    
	//log subscribe topics
	int log_level;
    rc = qcloud_get_log_level(mqtt_client, &log_level);
    //rc = qcloud_log_topic_subscribe(mqtt_client);
    if (rc < 0) {
        Log_e("client get log topic failed: %d", rc);
    }
    set_log_mqtt_client((void *)mqtt_client);

    IOT_Log_Upload(true);
#endif

	return mqtt_client;
}

int IOT_MQTT_Destroy(void **pClient) {
	POINTER_SANITY_CHECK(*pClient, QCLOUD_ERR_INVAL);

	Qcloud_IoT_Client *mqtt_client = (Qcloud_IoT_Client *)(*pClient);

	int rc = qcloud_iot_mqtt_disconnect(mqtt_client);

#ifdef MQTT_RMDUP_MSG_ENABLED
    reset_repeat_packet_id_buffer();
#endif

    HAL_MutexDestroy(mqtt_client->lock_generic);
	HAL_MutexDestroy(mqtt_client->lock_write_buf);

    HAL_MutexDestroy(mqtt_client->lock_list_sub);
    HAL_MutexDestroy(mqtt_client->lock_list_pub);

    list_destroy(mqtt_client->list_pub_wait_ack);
    list_destroy(mqtt_client->list_sub_wait_ack);

    HAL_Free(*pClient);
    *pClient = NULL;
#ifdef LOG_UPLOAD    
    set_log_mqtt_client(NULL);
#endif    
    Log_i("mqtt release!");

	return rc;
}

int IOT_MQTT_Yield(void *pClient, uint32_t timeout_ms) {

	Qcloud_IoT_Client   *mqtt_client = (Qcloud_IoT_Client *)pClient;
    int rc = qcloud_iot_mqtt_yield(mqtt_client, timeout_ms);

#ifdef LOG_UPLOAD
    /* do instant log uploading if MQTT communication error */
    if (rc == QCLOUD_ERR_SUCCESS)
        IOT_Log_Upload(false);
    else
        IOT_Log_Upload(true);
#endif

	return rc;
}

int IOT_MQTT_Publish(void *pClient, char *topicName, PublishParams *pParams) {

	Qcloud_IoT_Client   *mqtt_client = (Qcloud_IoT_Client *)pClient;

	return qcloud_iot_mqtt_publish(mqtt_client, topicName, pParams);
}

int IOT_MQTT_Subscribe(void *pClient, char *topicFilter, SubscribeParams *pParams) {

	Qcloud_IoT_Client   *mqtt_client = (Qcloud_IoT_Client *)pClient;

	return qcloud_iot_mqtt_subscribe(mqtt_client, topicFilter, pParams);
}

int IOT_MQTT_Unsubscribe(void *pClient, char *topicFilter) {

	Qcloud_IoT_Client   *mqtt_client = (Qcloud_IoT_Client *)pClient;

	return qcloud_iot_mqtt_unsubscribe(mqtt_client, topicFilter);
}

bool IOT_MQTT_IsConnected(void *pClient) {
    IOT_FUNC_ENTRY;

    POINTER_SANITY_CHECK(pClient, QCLOUD_ERR_INVAL);

    Qcloud_IoT_Client   *mqtt_client = (Qcloud_IoT_Client *)pClient;

    IOT_FUNC_EXIT_RC(get_client_conn_state(mqtt_client) == 1)
}

int qcloud_iot_mqtt_init(Qcloud_IoT_Client *pClient, MQTTInitParams *pParams) {
    IOT_FUNC_ENTRY;
	DeviceAuthMode authmode = AUTH_MODE_MAX;

    POINTER_SANITY_CHECK(pClient, QCLOUD_ERR_INVAL);
    POINTER_SANITY_CHECK(pParams, QCLOUD_ERR_INVAL);

	memset(pClient, 0x0, sizeof(Qcloud_IoT_Client));

    int size = HAL_Snprintf(s_qcloud_iot_host, HOST_STR_LENGTH, "%s.%s", pParams->product_id, QCLOUD_IOT_MQTT_DIRECT_DOMAIN);
    if (size < 0 || size > HOST_STR_LENGTH - 1) {
		IOT_FUNC_EXIT_RC(QCLOUD_ERR_FAILURE);
	}

    int i = 0;
    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i) {
        pClient->sub_handles[i].topic_filter = NULL;
        pClient->sub_handles[i].message_handler = NULL;
        pClient->sub_handles[i].qos = QOS0;
        pClient->sub_handles[i].message_handler_data = NULL;
    }

    if (pParams->command_timeout < MIN_COMMAND_TIMEOUT)
    	pParams->command_timeout = MIN_COMMAND_TIMEOUT;
    if (pParams->command_timeout > MAX_COMMAND_TIMEOUT)
    	pParams->command_timeout = MAX_COMMAND_TIMEOUT;
    pClient->command_timeout_ms = pParams->command_timeout;

    // packet id 取随机数 1- 65536
    pClient->next_packet_id = _get_random_start_packet_id();
    pClient->write_buf_size = QCLOUD_IOT_MQTT_TX_BUF_LEN;
    pClient->read_buf_size = QCLOUD_IOT_MQTT_RX_BUF_LEN;
    pClient->is_ping_outstanding = 0;
    pClient->was_manually_disconnected = 0;
    pClient->counter_network_disconnected = 0;
    
    pClient->event_handle = pParams->event_handle;

    pClient->lock_generic = HAL_MutexCreate();
    if (NULL == pClient->lock_generic) {
        IOT_FUNC_EXIT_RC(QCLOUD_ERR_FAILURE);
    }

    set_client_conn_state(pClient, NOTCONNECTED);

    if ((pClient->lock_write_buf = HAL_MutexCreate()) == NULL) {
    	Log_e("create write buf lock failed.");
    	goto error;
    }
    if ((pClient->lock_list_sub = HAL_MutexCreate()) == NULL) {
    	Log_e("create sub list lock failed.");
    	goto error;
    }
    if ((pClient->lock_list_pub = HAL_MutexCreate()) == NULL) {
    	Log_e("create pub list lock failed.");
    	goto error;
    }

    if ((pClient->list_pub_wait_ack = list_new()) == NULL) {
    	Log_e("create pub wait list failed.");
    	goto error;
    }
	pClient->list_pub_wait_ack->free = HAL_Free;

    if ((pClient->list_sub_wait_ack = list_new()) == NULL) {
    	Log_e("create sub wait list failed.");
        goto error;
    }
	pClient->list_sub_wait_ack->free = HAL_Free;

	/* 获取鉴权模式 */
	if (QCLOUD_ERR_SUCCESS != HAL_GetAuthMode(&authmode)) 
	{
		Log_e("get auth mode error!");
		IOT_FUNC_EXIT_RC(QCLOUD_ERR_FAILURE);
	}

	if (AUTH_MODE_CERT_TLS == authmode)
	{
	    bool certEmpty = (pParams->cert_file == NULL || pParams->key_file == NULL);
	    if (certEmpty) {
	        Log_e("cert file or key file is empty!");
	        IOT_FUNC_EXIT_RC(QCLOUD_ERR_INVAL);
	    }
	    Log_d("cert file: %s", pParams->cert_file);
	    Log_d("key file: %s", pParams->key_file);

	    pClient->network_stack.ssl_connect_params.cert_file = pParams->cert_file;
	    pClient->network_stack.ssl_connect_params.key_file = pParams->key_file;
	    pClient->network_stack.ssl_connect_params.ca_crt = iot_ca_get();
	    pClient->network_stack.ssl_connect_params.ca_crt_len = strlen(pClient->network_stack.ssl_connect_params.ca_crt);
		pClient->network_stack.ssl_connect_params.timeout_ms = QCLOUD_IOT_TLS_HANDSHAKE_TIMEOUT;
		pClient->network_stack.host = s_qcloud_iot_host;
    	pClient->network_stack.port = s_qcloud_iot_port_tls;
	}
	else if (AUTH_MODE_KEY_TLS == authmode)
	{
	    if (pParams->device_secret != NULL) {
	        size_t src_len = strlen(pParams->device_secret);
	        size_t len;
	        memset(sg_psk_str, 0x00, DECODE_PSK_LENGTH);
	        qcloud_iot_utils_base64decode(sg_psk_str, sizeof( sg_psk_str ), &len, (unsigned char *)pParams->device_secret, src_len );
	        pClient->network_stack.ssl_connect_params.psk = (char *)sg_psk_str;
	        pClient->network_stack.ssl_connect_params.psk_length = len;
	    } else {
	        Log_e("psk is empty!");
	        IOT_FUNC_EXIT_RC(QCLOUD_ERR_INVAL);
	    }
	    pClient->network_stack.ssl_connect_params.psk_id = iot_device_info_get()->client_id;
	    if (iot_device_info_get()->client_id == NULL) {
	        Log_e("psk id is empty!");
	        IOT_FUNC_EXIT_RC(QCLOUD_ERR_INVAL);
	    }
	    pClient->network_stack.ssl_connect_params.ca_crt = iot_ca_get();
	    pClient->network_stack.ssl_connect_params.ca_crt_len = strlen(pClient->network_stack.ssl_connect_params.ca_crt);
		pClient->network_stack.ssl_connect_params.timeout_ms = QCLOUD_IOT_TLS_HANDSHAKE_TIMEOUT;
		pClient->network_stack.host = s_qcloud_iot_host;
    	pClient->network_stack.port = s_qcloud_iot_port_tls;
	}
	else /* AUTH_MODE_KEY_NO_TLS == authmode */
	{
		pClient->network_stack.host = s_qcloud_iot_host;
    	pClient->network_stack.port = s_qcloud_iot_port_no_tls;
	}

    // 底层网络操作相关的数据结构初始化
    qcloud_iot_mqtt_network_init(&(pClient->network_stack));

    // ping定时器以及重连延迟定时器相关初始化
    InitTimer(&(pClient->ping_timer));
    InitTimer(&(pClient->reconnect_delay_timer));

    IOT_FUNC_EXIT_RC(QCLOUD_ERR_SUCCESS);

error:
	if (pClient->list_pub_wait_ack) {
		pClient->list_pub_wait_ack->free(pClient->list_pub_wait_ack);
		pClient->list_pub_wait_ack = NULL;
	}
	if (pClient->list_sub_wait_ack) {
		pClient->list_sub_wait_ack->free(pClient->list_sub_wait_ack);
		pClient->list_sub_wait_ack = NULL;
	}
	if (pClient->lock_generic) {
		HAL_MutexDestroy(pClient->lock_generic);
		pClient->lock_generic = NULL;
	}
	if (pClient->lock_list_sub) {
		HAL_MutexDestroy(pClient->lock_list_sub);
		pClient->lock_list_sub = NULL;
	}
	if (pClient->lock_list_pub) {
		HAL_MutexDestroy(pClient->lock_list_pub);
		pClient->lock_list_pub = NULL;
	}
	if (pClient->lock_write_buf) {
		HAL_MutexDestroy(pClient->lock_write_buf);
		pClient->lock_write_buf = NULL;
	}

	IOT_FUNC_EXIT_RC(QCLOUD_ERR_FAILURE)
}

int qcloud_iot_mqtt_set_autoreconnect(Qcloud_IoT_Client *pClient, bool value) {
    IOT_FUNC_ENTRY;

    POINTER_SANITY_CHECK(pClient, QCLOUD_ERR_INVAL);

    pClient->options.auto_connect_enable = (uint8_t) value;

    IOT_FUNC_EXIT_RC(QCLOUD_ERR_SUCCESS);
}

bool qcloud_iot_mqtt_is_autoreconnect_enabled(Qcloud_IoT_Client *pClient) {
    IOT_FUNC_ENTRY;

    POINTER_SANITY_CHECK(pClient, QCLOUD_ERR_INVAL);

    bool is_enabled = false;
    if (pClient->options.auto_connect_enable == 1) {
        is_enabled = true;
    }

    IOT_FUNC_EXIT_RC(is_enabled);
}

int qcloud_iot_mqtt_get_network_disconnected_count(Qcloud_IoT_Client *pClient) {
    IOT_FUNC_ENTRY;

    POINTER_SANITY_CHECK(pClient, QCLOUD_ERR_INVAL);

    IOT_FUNC_EXIT_RC(pClient->counter_network_disconnected);
}

int qcloud_iot_mqtt_reset_network_disconnected_count(Qcloud_IoT_Client *pClient) {
    IOT_FUNC_ENTRY;

    POINTER_SANITY_CHECK(pClient, QCLOUD_ERR_INVAL);

    pClient->counter_network_disconnected = 0;

    IOT_FUNC_EXIT_RC(QCLOUD_ERR_SUCCESS);
}

#ifdef __cplusplus
}
#endif
