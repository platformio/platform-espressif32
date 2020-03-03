/*
* Copyright 2015-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
* http://aws.amazon.com/apache2.0
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
*/

#include "aws_iot_jobs_interface.h"
#include "aws_iot_log.h"
#include "aws_iot_jobs_json.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHECK_GENERATE_STRING_RESULT(result, bufferSize) \
	if (result < 0) { \
		return FAILURE; \
	} else if ((unsigned) result >= bufferSize) { \
		return LIMIT_EXCEEDED_ERROR; \
	}


IoT_Error_t aws_iot_jobs_subscribe_to_job_messages(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const char *jobId,
		AwsIotJobExecutionTopicType topicType,
		AwsIotJobExecutionTopicReplyType replyType,
		pApplicationHandler_t pApplicationHandler,
		void *pApplicationHandlerData,
		char *topicBuffer,
		uint16_t topicBufferSize)
{
	int requiredSize = aws_iot_jobs_get_api_topic(topicBuffer, topicBufferSize, topicType, replyType, thingName, jobId);
	CHECK_GENERATE_STRING_RESULT(requiredSize, topicBufferSize);

	return aws_iot_mqtt_subscribe(pClient, topicBuffer, (uint16_t)strlen(topicBuffer), qos, pApplicationHandler, pApplicationHandlerData);
}

IoT_Error_t aws_iot_jobs_subscribe_to_all_job_messages(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		pApplicationHandler_t pApplicationHandler,
		void *pApplicationHandlerData,
		char *topicBuffer,
		uint16_t topicBufferSize)
{
	return aws_iot_jobs_subscribe_to_job_messages(pClient, qos, thingName, NULL, JOB_WILDCARD_TOPIC, JOB_WILDCARD_REPLY_TYPE,
			pApplicationHandler, pApplicationHandlerData, topicBuffer, topicBufferSize);
}

IoT_Error_t aws_iot_jobs_unsubscribe_from_job_messages(
		AWS_IoT_Client *pClient,
		char *topicBuffer) 
{
	return aws_iot_mqtt_unsubscribe(pClient, topicBuffer, (uint16_t)strlen(topicBuffer));
}

IoT_Error_t aws_iot_jobs_send_query(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const char *jobId,
		const char *clientToken,
		char *topicBuffer,
		uint16_t topicBufferSize,
		char *messageBuffer,
		size_t messageBufferSize,
		AwsIotJobExecutionTopicType topicType)
{
	if (thingName == NULL || topicBuffer == NULL || (clientToken != NULL && messageBuffer == NULL)) {
		return NULL_VALUE_ERROR;
	}

	int neededSize = aws_iot_jobs_get_api_topic(topicBuffer, topicBufferSize, topicType, JOB_REQUEST_TYPE, thingName, jobId);
	CHECK_GENERATE_STRING_RESULT(neededSize, topicBufferSize);
	uint16_t topicSize = (uint16_t) neededSize;

	char emptyBuffer[1];
	size_t messageLength;
	if (clientToken == NULL) {
		messageLength = 0;
		messageBuffer = emptyBuffer;
	} else {
		int serializeResult = aws_iot_jobs_json_serialize_client_token_only_request(messageBuffer, messageBufferSize, clientToken);
		CHECK_GENERATE_STRING_RESULT(serializeResult, messageBufferSize);
		messageLength = (size_t)serializeResult;
	}

	IoT_Publish_Message_Params publishParams;
	publishParams.qos = qos;
	publishParams.isRetained = 0;
	publishParams.isDup = 0;
	publishParams.id = 0;
	publishParams.payload = messageBuffer;
	publishParams.payloadLen = messageLength;

	return aws_iot_mqtt_publish(pClient, topicBuffer, topicSize, &publishParams);
}

IoT_Error_t aws_iot_jobs_start_next(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const AwsIotStartNextPendingJobExecutionRequest *startNextRequest,
		char *topicBuffer,
		uint16_t topicBufferSize,
		char *messageBuffer,
		size_t messageBufferSize)
{
	if (thingName == NULL || topicBuffer == NULL || startNextRequest == NULL) {
		return NULL_VALUE_ERROR;
	}

	int neededSize = aws_iot_jobs_get_api_topic(topicBuffer, topicBufferSize, JOB_START_NEXT_TOPIC, JOB_REQUEST_TYPE, thingName, NULL);
	CHECK_GENERATE_STRING_RESULT(neededSize, topicBufferSize);
	uint16_t topicSize = (uint16_t) neededSize;

	int serializeResult = aws_iot_jobs_json_serialize_start_next_job_execution_request(messageBuffer, messageBufferSize, startNextRequest);
	CHECK_GENERATE_STRING_RESULT(serializeResult, messageBufferSize);

	IoT_Publish_Message_Params publishParams;
	publishParams.qos = qos;
	publishParams.isRetained = 0;
	publishParams.isDup = 0;
	publishParams.id = 0;
	publishParams.payload = messageBuffer;
	publishParams.payloadLen = (size_t) serializeResult;

	return aws_iot_mqtt_publish(pClient, topicBuffer, topicSize, &publishParams);
}

IoT_Error_t aws_iot_jobs_describe(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const char *jobId,
		const AwsIotDescribeJobExecutionRequest *describeRequest,
		char *topicBuffer,
		uint16_t topicBufferSize,
		char *messageBuffer,
		size_t messageBufferSize)
{
	if (thingName == NULL || topicBuffer == NULL || describeRequest == NULL) {
		return NULL_VALUE_ERROR;
	}

	int neededSize = aws_iot_jobs_get_api_topic(topicBuffer, topicBufferSize, JOB_DESCRIBE_TOPIC, JOB_REQUEST_TYPE, thingName, jobId);
	CHECK_GENERATE_STRING_RESULT(neededSize, topicBufferSize);
	uint16_t topicSize = (uint16_t) neededSize;

	char emptyBuffer[1];
	size_t messageLength;
	if (messageBuffer == NULL) {
		messageLength = 0;
		messageBuffer = emptyBuffer;
	} else {
		int serializeResult = aws_iot_jobs_json_serialize_describe_job_execution_request(messageBuffer, messageBufferSize, describeRequest);
		CHECK_GENERATE_STRING_RESULT(serializeResult, messageBufferSize);
		messageLength = (size_t) serializeResult;
	}

	IoT_Publish_Message_Params publishParams;
	publishParams.qos = qos;
	publishParams.isRetained = 0;
	publishParams.isDup = 0;
	publishParams.id = 0;
	publishParams.payload = messageBuffer;
	publishParams.payloadLen = messageLength;

	return aws_iot_mqtt_publish(pClient, topicBuffer, topicSize, &publishParams);
}

IoT_Error_t aws_iot_jobs_send_update(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const char *jobId,
		const AwsIotJobExecutionUpdateRequest *updateRequest,
		char *topicBuffer,
		uint16_t topicBufferSize,
		char *messageBuffer,
		size_t messageBufferSize)
{
	if (thingName == NULL || topicBuffer == NULL || jobId == NULL || updateRequest == NULL) {
		return NULL_VALUE_ERROR;
	}

	int neededSize = aws_iot_jobs_get_api_topic(topicBuffer, topicBufferSize, JOB_UPDATE_TOPIC, JOB_REQUEST_TYPE, thingName, jobId);
	CHECK_GENERATE_STRING_RESULT(neededSize, topicBufferSize);
	uint16_t topicSize = (uint16_t) neededSize;

	int serializeResult = aws_iot_jobs_json_serialize_update_job_execution_request(messageBuffer, messageBufferSize, updateRequest);
	CHECK_GENERATE_STRING_RESULT(serializeResult, messageBufferSize);

	IoT_Publish_Message_Params publishParams;
	publishParams.qos = qos;
	publishParams.isRetained = 0;
	publishParams.isDup = 0;
	publishParams.id = 0;
	publishParams.payload = messageBuffer;
	publishParams.payloadLen = (size_t) serializeResult;

	return aws_iot_mqtt_publish(pClient, topicBuffer, topicSize, &publishParams);
}

#ifdef __cplusplus
}
#endif
