#include <string.h>
#include <stdio.h>
#include <network_interface.h>

#include "network_interface.h"
#include "aws_iot_tests_unit_mock_tls_params.h"


void _iot_tls_set_connect_params(Network *pNetwork, char *pRootCALocation, char *pDeviceCertLocation,
								 char *pDevicePrivateKeyLocation, char *pDestinationURL,
								 uint16_t destinationPort, uint32_t timeout_ms, bool ServerVerificationFlag) {
	pNetwork->tlsConnectParams.DestinationPort = destinationPort;
	pNetwork->tlsConnectParams.pDestinationURL = pDestinationURL;
	pNetwork->tlsConnectParams.pDeviceCertLocation = pDeviceCertLocation;
	pNetwork->tlsConnectParams.pDevicePrivateKeyLocation = pDevicePrivateKeyLocation;
	pNetwork->tlsConnectParams.pRootCALocation = pRootCALocation;
	pNetwork->tlsConnectParams.timeout_ms = timeout_ms;
	pNetwork->tlsConnectParams.ServerVerificationFlag = ServerVerificationFlag;
}

IoT_Error_t iot_tls_init(Network *pNetwork, char *pRootCALocation, char *pDeviceCertLocation,
						 char *pDevicePrivateKeyLocation, char *pDestinationURL,
						 uint16_t destinationPort, uint32_t timeout_ms, bool ServerVerificationFlag) {
	_iot_tls_set_connect_params(pNetwork, pRootCALocation, pDeviceCertLocation, pDevicePrivateKeyLocation,
								pDestinationURL, destinationPort, timeout_ms, ServerVerificationFlag);

	pNetwork->connect = iot_tls_connect;
	pNetwork->read = iot_tls_read;
	pNetwork->write = iot_tls_write;
	pNetwork->disconnect = iot_tls_disconnect;
	pNetwork->isConnected = iot_tls_is_connected;
	pNetwork->destroy = iot_tls_destroy;

	return SUCCESS;
}

IoT_Error_t iot_tls_connect(Network *pNetwork, TLSConnectParams *params) {
	IOT_UNUSED(pNetwork);

	if(NULL != params) {
		_iot_tls_set_connect_params(pNetwork, params->pRootCALocation, params->pDeviceCertLocation,
									params->pDevicePrivateKeyLocation, params->pDestinationURL, params->DestinationPort,
									params->timeout_ms, params->ServerVerificationFlag);
	}

	if(NULL != invalidEndpointFilter && 0 == strcmp(invalidEndpointFilter, pNetwork->tlsConnectParams.pDestinationURL)) {
		return NETWORK_ERR_NET_UNKNOWN_HOST;
	}

	if(invalidPortFilter == pNetwork->tlsConnectParams.DestinationPort) {
		return NETWORK_ERR_NET_CONNECT_FAILED;
	}

	if(NULL != invalidRootCAPathFilter && 0 == strcmp(invalidRootCAPathFilter, pNetwork->tlsConnectParams.pRootCALocation)) {
		return NETWORK_ERR_NET_CONNECT_FAILED;
	}

	if(NULL != invalidCertPathFilter && 0 == strcmp(invalidCertPathFilter, pNetwork->tlsConnectParams.pDeviceCertLocation)) {
		return NETWORK_ERR_NET_CONNECT_FAILED;
	}

	if(NULL != invalidPrivKeyPathFilter && 0 == strcmp(invalidPrivKeyPathFilter, pNetwork->tlsConnectParams.pDevicePrivateKeyLocation)) {
		return NETWORK_ERR_NET_CONNECT_FAILED;
	}
	return SUCCESS;
}

IoT_Error_t iot_tls_is_connected(Network *pNetwork) {
	IOT_UNUSED(pNetwork);

	/* Use this to add implementation which can check for physical layer disconnect */
	return NETWORK_PHYSICAL_LAYER_CONNECTED;
}

static size_t iot_tls_mqtt_read_variable_length_int(const unsigned char *buffer, size_t startPos) {
	size_t result = 0;
	size_t pos = startPos;
	size_t multiplier = 1;
	do {
		result = (buffer[pos] & 0x7f) * multiplier;
		multiplier *= 0x80;
		pos++;
	} while ((buffer[pos - 1] & 0x80) && pos - startPos < 4);

	return result;
}

static size_t iot_tls_mqtt_get_end_of_variable_length_int(const unsigned char *buffer, size_t startPos) {
	size_t pos = startPos;
	while ((buffer[pos] & 0x80) && pos - startPos < 3) pos++;
	pos++;

	return pos;
}

static uint16_t iot_tls_mqtt_get_fixed_uint16_from_message(const unsigned char *msgBuffer, size_t startPos) {
	uint8_t firstByte = (uint8_t)(msgBuffer[startPos]);
	uint8_t secondByte = (uint8_t)(msgBuffer[startPos + 1]);
	return (uint16_t) (secondByte + (256 * firstByte));
}

static uint16_t iot_tls_mqtt_copy_string_from_message(char *dest, const unsigned char *msgBuffer, size_t startPos) {
	uint16_t length = iot_tls_mqtt_get_fixed_uint16_from_message(msgBuffer, startPos);

	snprintf(dest, length + 1u, "%s", &(msgBuffer[startPos + 2])); // Added one for null character
	return length;
}

IoT_Error_t iot_tls_write(Network *pNetwork, unsigned char *pMsg, size_t len, Timer *timer, size_t *written_len) {
	size_t i = 0;
	uint8_t firstPacketByte;
	size_t mqttPacketLength;
	size_t variableHeaderStart;
	IoT_Error_t status = SUCCESS;
	IOT_UNUSED(pNetwork);
	IOT_UNUSED(timer);

	if(TxBuffer.mockedError != SUCCESS ) {
		status = TxBuffer.mockedError;

		/* Clear the error before returning. */
		TxBuffer.mockedError = SUCCESS;

		return status;
	}

	for(i = 0; (i < len) && left_ms(timer) > 0; i++) {
		TxBuffer.pBuffer[i] = pMsg[i];
	}
	TxBuffer.len = len;
	*written_len = len;

	mqttPacketLength = iot_tls_mqtt_read_variable_length_int(TxBuffer.pBuffer, 1);
	variableHeaderStart = iot_tls_mqtt_get_end_of_variable_length_int(TxBuffer.pBuffer, 1);

	firstPacketByte = TxBuffer.pBuffer[0];
	/* Save last two subscribed topics */
	if((firstPacketByte == 0x82 ? true : false)) {
		snprintf(SecondLastSubscribeMessage, lastSubscribeMsgLen + 1u, "%s", LastSubscribeMessage);
		secondLastSubscribeMsgLen = lastSubscribeMsgLen;

		lastSubscribeMsgLen = iot_tls_mqtt_copy_string_from_message(
				LastSubscribeMessage, TxBuffer.pBuffer, variableHeaderStart + 2);
	} else if (firstPacketByte == 0xA2) {
		lastUnsubscribeMsgLen = iot_tls_mqtt_copy_string_from_message(
						LastUnsubscribeMessage, TxBuffer.pBuffer, variableHeaderStart + 2);
	} else if ((firstPacketByte & 0x30) == 0x30) {
		QoS qos = (QoS) (firstPacketByte & 0x6 >> 1);

		lastPublishMessageTopicLen =
				iot_tls_mqtt_copy_string_from_message(LastPublishMessageTopic, TxBuffer.pBuffer, variableHeaderStart);

		size_t payloadStart = variableHeaderStart + 2 + lastPublishMessageTopicLen;

		if (qos > QOS0) {
			payloadStart += 2;
		}

		lastPublishMessagePayloadLen = mqttPacketLength - payloadStart + 2; /* + 2 as the first two bytes don't count towards the length */
		memcpy(LastPublishMessagePayload, TxBuffer.pBuffer + payloadStart, lastPublishMessagePayloadLen);
		LastPublishMessagePayload[lastPublishMessagePayloadLen] = 0;
	}

	return status;
}

static unsigned char isTimerExpired(struct timeval target_time) {
	unsigned char ret_val = 0;
	struct timeval now, result;

	if(target_time.tv_sec != 0 || target_time.tv_usec != 0) {
		gettimeofday(&now, NULL);
		timersub(&(target_time), &now, &result);
		if(result.tv_sec < 0 || (result.tv_sec == 0 && result.tv_usec <= 0)) {
			ret_val = 1;
		}
	} else {
		ret_val = 1;
	}
	return ret_val;
}

IoT_Error_t iot_tls_read(Network *pNetwork, unsigned char *pMsg, size_t len, Timer *pTimer, size_t *read_len) {
	IoT_Error_t status = SUCCESS;

	IOT_UNUSED(pNetwork);
	IOT_UNUSED(pTimer);

	if(RxBuffer.mockedError != SUCCESS) {
		status = RxBuffer.mockedError;

		/* Clear the error before returning. */
		RxBuffer.mockedError = SUCCESS;

		return status;
	}

	if(RxIndex > TLSMaxBufferSize - 1) {
		RxIndex = TLSMaxBufferSize - 1;
	}

	if(RxBuffer.len <= RxIndex || !isTimerExpired(RxBuffer.expiry_time)) {
		return NETWORK_SSL_NOTHING_TO_READ;
	}

	if((false == RxBuffer.NoMsgFlag) && (RxIndex < RxBuffer.len)) {
		memcpy(pMsg, &(RxBuffer.pBuffer[RxIndex]), len);
		RxIndex += len;
		*read_len = len;
	}

	return status;
}

IoT_Error_t iot_tls_disconnect(Network *pNetwork) {
	IOT_UNUSED(pNetwork);
	return SUCCESS;
}

IoT_Error_t iot_tls_destroy(Network *pNetwork) {
	IOT_UNUSED(pNetwork);
	return SUCCESS;
}
