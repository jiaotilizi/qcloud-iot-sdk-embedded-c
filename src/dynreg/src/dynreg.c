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

#include "ca.h"
#include "device.h"
#include "qcloud_iot_import.h"
#include "qcloud_iot_export.h"
#include "utils_httpc.h"
#include "utils_hmac.h"
#include "utils_aes.h"
#include "lite-utils.h"
#include "utils_base64.h"

/* Knuth's TAOCP section 3.6 */
#define		M	((1U<<31) -1)
#define		A	48271
#define		Q	44488		// M/A
#define		R	3399		// M%A; R < Q !!!

#define 	CODE_RESAULT	"code"
#define 	ENCRYPT_TYPE	"encryptionType"
#define 	PSK_DATA		"psk"
#define 	CERT_DATA		"clientCert"
#define 	KEY_DATA		"clientKey"

typedef enum {
	eCERT_TYPE = 1,
	ePSK_TYPE = 2,	
}eAuthType;


/*Global value*/
static unsigned int _seed=1;

int rand_r(unsigned int* seed)
{   int32_t X;

    X = *seed;
    X = A*(X%Q) - R * (int32_t) (X/Q);
    if (X < 0)
	X += M;

    *seed = X;
    return X;
}

int rand_d(void) {
  return rand_r(&_seed);
}

void srand_d(unsigned int i)
{ 
	_seed=i;
}



static int _get_json_resault_code(char *json) {
	int32_t resault = -1;
	char *v = LITE_json_value_of(CODE_RESAULT, json);
	
	if (v == NULL) {
        Log_e("Invalid json content: %s", json);
		return -1;
	}
	
	if (LITE_get_int32(&resault, v) != QCLOUD_ERR_SUCCESS) {
        Log_e("Invalid json content: %s", json);
        HAL_Free(v);
		return -1;
	}
	
    HAL_Free(v);
	
	return resault;
}

static int _get_json_encry_type(char *json) {
	int32_t type = -1;
	char *v = LITE_json_value_of(ENCRYPT_TYPE, json);
	
	if (v == NULL) {
        Log_e("Get encry type fail, %s", json);
		return -1;
	}
	
	if (LITE_get_int32(&type, v) != QCLOUD_ERR_SUCCESS) {
        Log_e("Invalid json content: %s", json);
        HAL_Free(v);
		return -1;
	}
	
    HAL_Free(v);
	
	return type;
}

static char * _get_json_psk(char *json) {

	char *psk = LITE_json_value_of(PSK_DATA, json);
	
	if (psk == NULL) {
        Log_e("Get psk fail: %s", json);
	}
		
	return psk;
}

static char * _get_json_cert_data(char *json) {

	char *cert = LITE_json_value_of(CERT_DATA, json);
	
	if (cert == NULL) {
        Log_e("Get clientCert fail: %s", json);
	}
		
	return cert;
}

static char * _get_json_key_data(char *json) {

	char *key = LITE_json_value_of(KEY_DATA, json);
	
	if (key == NULL) {
        Log_e("Get clientCert fail: %s", json);
	}
		
	return key;
}

/*\\n in data change to '\n'*/
static void _deal_transfer(char *data, uint32_t dataLen)
{
        int i;

        for(i = 0; i < dataLen; i++){
                if((data[i] == '\\')&&(data[i+1] == 'n')){
                        data[i] = ' ';
                        data[i+1] = '\n';
                }
        }

}

static int _cert_file_save(const char *fileName, char *data,	uint32_t dataLen)
{
    FILE *fp;
	char filePath[FILE_PATH_MAX_LEN];
    uint32_t len;  
	int Ret = QCLOUD_ERR_FAILURE;


	memset(filePath, 0, FILE_PATH_MAX_LEN);
	HAL_Snprintf(filePath, FILE_PATH_MAX_LEN, "/%s", fileName);

    if( ( fp = fopen(filePath, "w+" ) ) == NULL ) {
        Log_e("fail to open file %s", fileName);
        goto exit;
    }    
    
    _deal_transfer(data, dataLen);
	len =fprintf(fp,"%s",data);
    fclose(fp);

	if(len == dataLen){
		 Log_d("save %s file succes", fileName);
		 Ret = QCLOUD_ERR_SUCCESS;
	}
	
exit:

    return Ret;
}


static int _parse_devinfo(char *jdoc, DeviceInfo *pDevInfo)
{
	int ret = 0;	
	size_t len;
	int datalen;
	int enType;
	unsigned int keybits;	
	char key[UTILS_AES_BLOCK_LEN + 1];
	char decodeBuff_cert[DECODE_BUFF_LEN_FOR_CERT] = {0};
	char decodeBuff_key[DECODE_BUFF_LEN_FOR_KEY] = {0};
	unsigned char iv[16];
	char *payload = NULL;
	
	char *clientCert; 	/* AUTH_MODE_CERT_TLS 模式使用 */
	char *clientKey; 	/* AUTH_MODE_CERT_TLS 模式使用 */
	char *psk; 			/* AUTH_MODE_KEY_NO_TLS/AUTH_MODE_KEY_TLS 模式使用 */
	DeviceAuthMode authmode = AUTH_MODE_MAX;
	
	Log_d("recv：%s", jdoc);

	/* 获取鉴权模式 */
	if (QCLOUD_ERR_SUCCESS != HAL_GetAuthMode(&authmode)) 
	{
		Log_e("get auth mode error!");
		ret = QCLOUD_ERR_FAILURE;
		goto exit;
	}

	ret = _get_json_resault_code(jdoc);
	if(QCLOUD_ERR_SUCCESS != ret){
		Log_e("response err, ret:%d", ret);	
		goto exit;
	}
	
	payload = LITE_json_value_of("payload", jdoc);
	if (payload == NULL) {
        Log_e("Invalid json content: %s", jdoc);
		ret = QCLOUD_ERR_FAILURE;
		goto exit;
	}else{
		Log_d("payload:%s", payload);
	}

	if (AUTH_MODE_CERT_TLS == authmode)
	{
		ret = qcloud_iot_utils_base64decode((uint8_t *)decodeBuff_cert, sizeof(decodeBuff_cert), &len, 
											(uint8_t *)payload, strlen(payload));
	}
	else
	{
		ret = qcloud_iot_utils_base64decode((uint8_t *)decodeBuff_key, sizeof(decodeBuff_key), &len, 
											(uint8_t *)payload, strlen(payload));
	}
	if (ret != QCLOUD_ERR_SUCCESS) {
		Log_e("Response decode err, response:%s", payload);
		ret = QCLOUD_ERR_FAILURE;
		goto exit;
	}

	datalen =  len + (UTILS_AES_BLOCK_LEN - len%UTILS_AES_BLOCK_LEN);
	keybits = AES_KEY_BITS_128;
	memset(key, 0, UTILS_AES_BLOCK_LEN);
	strncpy(key, pDevInfo->product_key, UTILS_AES_BLOCK_LEN);
	memset( iv , '0', UTILS_AES_BLOCK_LEN);	
	
	if (AUTH_MODE_CERT_TLS == authmode)
	{
		ret = utils_aes_cbc((uint8_t *)decodeBuff_cert, datalen, (uint8_t *)decodeBuff_cert, 
							DECODE_BUFF_LEN_FOR_CERT, UTILS_AES_DECRYPT, (uint8_t *)key, keybits, iv);
	}
	else
	{
		ret = utils_aes_cbc((uint8_t *)decodeBuff_key, datalen, (uint8_t *)decodeBuff_key, 
							DECODE_BUFF_LEN_FOR_KEY, UTILS_AES_DECRYPT, (uint8_t *)key, keybits, iv);
	}
	
	if(QCLOUD_ERR_SUCCESS == ret){
		//Log_d("The decrypted data is:%s", decodeBuff);
			
	}else{
		Log_e("data decry err,ret:%d", ret);
		goto exit;
	}

	if (AUTH_MODE_CERT_TLS == authmode)
	{
		enType = _get_json_encry_type(decodeBuff_cert);
	}
	else
	{
		enType = _get_json_encry_type(decodeBuff_key);
	}
	if(enType < 0){
		Log_e("invlid encryt type, decrypt maybe faild");
		ret = QCLOUD_ERR_FAILURE;
		goto exit;
	}

	if (AUTH_MODE_CERT_TLS == authmode) 
	{
		if(eCERT_TYPE != enType){
			Log_e("encryt type should be cert type");
			ret = QCLOUD_ERR_FAILURE;
			goto exit;
		}

		clientCert = _get_json_cert_data(decodeBuff_cert);
		if(NULL != clientCert){
			memset(pDevInfo->devCertFileName, 0, MAX_SIZE_OF_DEVICE_CERT_FILE_NAME);
			HAL_Snprintf(pDevInfo->devCertFileName, MAX_SIZE_OF_DEVICE_CERT_FILE_NAME, "%s.crt", pDevInfo->device_name);
			if(QCLOUD_ERR_SUCCESS != _cert_file_save(pDevInfo->devCertFileName, clientCert,	strlen(clientCert))){
				Log_e("save %s file fail", pDevInfo->devCertFileName);
				ret = QCLOUD_ERR_FAILURE;
			}

			HAL_Free(clientCert);
			
		}else{
			Log_e("Get clientCert data fail");
			ret = QCLOUD_ERR_FAILURE;
		}

		clientKey = _get_json_key_data(decodeBuff_cert);
		if(NULL != clientKey){
			memset(pDevInfo->devPrivateKeyFileName, 0, MAX_SIZE_OF_DEVICE_KEY_FILE_NAME);
			HAL_Snprintf(pDevInfo->devPrivateKeyFileName, MAX_SIZE_OF_DEVICE_KEY_FILE_NAME, "%s.key", pDevInfo->device_name);
			if(QCLOUD_ERR_SUCCESS != _cert_file_save(pDevInfo->devPrivateKeyFileName, clientKey,	strlen(clientKey))){
				Log_e("save %s file fail", pDevInfo->devPrivateKeyFileName);
				ret = QCLOUD_ERR_FAILURE;
			}

			HAL_Free(clientKey);
			
		}else{
			Log_e("Get clientCert data fail");
			ret = QCLOUD_ERR_FAILURE;
		}
	}
	else
	{
		if(ePSK_TYPE != enType){
			Log_e("encryt type should be psk type");	
			ret = QCLOUD_ERR_FAILURE;
			goto exit;
		}

		psk = _get_json_psk(decodeBuff_key);
		if(NULL != psk){
			if(strlen(psk) > MAX_SIZE_OF_DEVICE_SERC){
				Log_e("psk exceed max len,%s", psk);
				strcpy(pDevInfo->devSerc, psk);
			}else{
				strncpy(pDevInfo->devSerc, psk, MAX_SIZE_OF_DEVICE_SERC);
				pDevInfo->devSerc[MAX_SIZE_OF_DEVICE_SERC] = '\0';
			}
			HAL_Free(psk);	
		}else{
			Log_e("Get psk data fail");
		}
	}

	
exit:

	if(payload){
		HAL_Free(payload);
	}

	return ret;
}

static int _post_reg_request_by_http(char *request_buf, DeviceInfo *pDevInfo)
{  
    int Ret = 0;
	HTTPClient		 http_client;			 /* http client */
	HTTPClientData	 http_data; 			 /* http client data */

	const char       *url_format = "%s://%s/register/dev";
    char             url[REG_URL_MAX_LEN] = {0};
	int 			 port;
	const char 		 *ca_crt = NULL;
	char 			 respbuff_cert[DYN_RESPONSE_BUFF_LEN_FOR_CERT];
	char 			 respbuff_key[DYN_RESPONSE_BUFF_LEN_FOR_KEY];
	DeviceAuthMode 	 authmode = AUTH_MODE_MAX;

	/* 获取鉴权模式 */
	if (QCLOUD_ERR_SUCCESS != HAL_GetAuthMode(&authmode)) 
	{
		Log_e("get auth mode error!");
		return QCLOUD_ERR_FAILURE;;
	}

	/*format URL*/
	if ((AUTH_MODE_CERT_TLS == authmode) || (AUTH_MODE_KEY_TLS == authmode))
	{
		HAL_Snprintf(url, REG_URL_MAX_LEN, url_format, "https", DYN_REG_SERVER_URL);
		port = DYN_REG_SERVER_PORT_TLS;
		ca_crt = iot_ca_get();
	}
	else /* AUTH_MODE_KEY_NO_TLS */
	{
		HAL_Snprintf(url, REG_URL_MAX_LEN, url_format, "http", DYN_REG_SERVER_URL);
		port = DYN_REG_SERVER_PORT;
	}

	
	memset((char *)&http_client, 0, sizeof(HTTPClient));  
	memset((char *)&http_data, 0, sizeof(HTTPClientData)); 

	http_client.header = "Accept: text/xml,application/json;*/*\r\n";

    http_data.post_content_type = "application/x-www-form-urlencoded";
    http_data.post_buf = request_buf;
    http_data.post_buf_len = strlen(request_buf);

    Ret = qcloud_http_client_common(&http_client, url, port, ca_crt, HTTP_POST, &http_data);
    if (QCLOUD_ERR_SUCCESS != Ret) {
        Log_e("qcloud_http_client_common failed, Ret = %d", Ret);
        return Ret;
    }

	if (AUTH_MODE_CERT_TLS == authmode)
	{
		memset(respbuff_cert, 0, DYN_RESPONSE_BUFF_LEN_FOR_CERT);
		http_data.response_buf_len = DYN_RESPONSE_BUFF_LEN_FOR_CERT;
	    http_data.response_buf = respbuff_cert;
	}
	else
	{
		memset(respbuff_key, 0, DYN_RESPONSE_BUFF_LEN_FOR_KEY);
		http_data.response_buf_len = DYN_RESPONSE_BUFF_LEN_FOR_KEY;
	    http_data.response_buf = respbuff_key;
	}
        
    Ret = qcloud_http_recv_data(&http_client, DYN_REG_RES_HTTP_TIMEOUT_MS, &http_data);
    if (QCLOUD_ERR_SUCCESS != Ret) {
        Log_e("dynamic register response fail, Ret = %d", Ret);        
    } else {		
		/*Parse dev info*/
		Ret = _parse_devinfo(http_data.response_buf, pDevInfo);
		if(QCLOUD_ERR_SUCCESS != Ret){
			Log_e("parse device info err");
		}
    }

    qcloud_http_client_close(&http_client);

    return Ret;    
}


static int _cal_dynreg_sign(DeviceInfo *pDevInfo, char *signout, int max_signlen, int nonce, uint32_t timestamp)
{
    int sign_len;
	size_t olen = 0;
    char  *pSignSource = NULL;
    const char *sign_fmt ="deviceName=%s&nonce=%d&productId=%s&timestamp=%d";
	char sign[DYN_REG_SIGN_LEN] = {0};

	/*format sign data*/
    sign_len = strlen(sign_fmt) + strlen(pDevInfo->device_name) + strlen(pDevInfo->product_id)+ sizeof(int) + sizeof(uint32_t) + DYN_BUFF_DATA_MORE;
    pSignSource = HAL_Malloc(sign_len);
    if (pSignSource == NULL) {
        Log_e("malloc sign source buff fail");
        return QCLOUD_ERR_FAILURE;
    }
    memset(pSignSource, 0, sign_len);
    HAL_Snprintf((char *)pSignSource, sign_len, sign_fmt, pDevInfo->device_name, nonce, pDevInfo->product_id, timestamp);

	/*cal hmac sha1*/
    utils_hmac_sha1(pSignSource, strlen(pSignSource), sign, pDevInfo->product_key, strlen(pDevInfo->product_key));

	/*base64 encode*/
	qcloud_iot_utils_base64encode((uint8_t *)signout, max_signlen, &olen, (const uint8_t *)sign, strlen(sign));

	HAL_Free(pSignSource);

    return (olen > max_signlen)?QCLOUD_ERR_FAILURE:QCLOUD_ERR_SUCCESS;
}


int qcloud_iot_dyn_reg_dev(DeviceInfo *pDevInfo)
{
	const char  *para_format = "{\"deviceName\":\"%s\",\"nonce\":%d,\"productId\":\"%s\",\"timestamp\":%d,\"signature\":\"%s\"}";
	int nonce;
	int Ret;
	uint32_t timestamp;
	int len;
	char sign[DYN_REG_SIGN_LEN] = {0};
	char *pRequest = NULL;
	DeviceAuthMode authmode = AUTH_MODE_MAX;

	if(strlen(pDevInfo->product_key) < UTILS_AES_BLOCK_LEN){
		Log_e("product key inllegal");
		return QCLOUD_ERR_FAILURE;		
	}

	srand_d(HAL_UptimeMs());
	nonce = rand_d();
	timestamp = time(0);


	/*cal sign*/
	if(QCLOUD_ERR_SUCCESS == _cal_dynreg_sign(pDevInfo, sign, DYN_REG_SIGN_LEN, nonce, timestamp)){
		Log_d("sign:%s", sign);
	}else{
		Log_e("cal sign fail");
		return QCLOUD_ERR_FAILURE;		
	}

	/*format http request*/
	len = strlen(para_format) + strlen(pDevInfo->product_id) + strlen(pDevInfo->device_name) + sizeof(int)\
														+ sizeof(uint32_t) + strlen(sign) + DYN_BUFF_DATA_MORE;
	pRequest = HAL_Malloc(len);	
	if(!pRequest){
		Log_e("malloc request memory fail");
		return QCLOUD_ERR_FAILURE;	
	}
	memset(pRequest, 0, len);
	HAL_Snprintf(pRequest, len, para_format, pDevInfo->device_name, nonce, pDevInfo->product_id, timestamp, sign);
	Log_d("request:%s",pRequest);

	/* 获取鉴权模式 */
	if (QCLOUD_ERR_SUCCESS != HAL_GetAuthMode(&authmode)) 
	{
		Log_e("get auth mode error!");
		return QCLOUD_ERR_FAILURE;;
	}
	
	Log_d("resbuff len:%d", ((AUTH_MODE_CERT_TLS == authmode)? DYN_RESPONSE_BUFF_LEN_FOR_CERT : DYN_RESPONSE_BUFF_LEN_FOR_KEY));
	/*post request*/
	Ret =_post_reg_request_by_http(pRequest, pDevInfo);		
	if(QCLOUD_ERR_SUCCESS == Ret){
		Log_d("request dev info success");	
	}else{
		Log_e("request dev info fail");	
	}

	HAL_Free(pRequest);
	
	return Ret;
}

#ifdef __cplusplus
}
#endif

