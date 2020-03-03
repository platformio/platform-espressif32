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

#include <aws_iot_jobs_interface.h>

#include "aws_iot_tests_unit_mock_tls_params.h"
#include "aws_iot_tests_unit_helper_functions.h"
#include "aws_iot_config.h"
#include <aws_iot_tests_unit_helper_functions.h>
#include <CppUTest/TestHarness_c.h>
#include <aws_iot_log.h>

static AWS_IoT_Client client;
static IoT_Client_Connect_Params connectParams;
static IoT_Client_Init_Params mqttInitParams;

static const char *THING_NAME = "T1";
static const char *JOB_ID = "J1";

static int CALLBACK_DATA = 5;

static const char *expectedTopic;
static const void *expectedData;
static size_t expectedDataLen;

static int callbackCount = 0;

static bool expectError;
static bool expectedJsonContext;


static void testCallback(
			AWS_IoT_Client *pClient,
			char *topicName, uint16_t topicNameLen,
			IoT_Publish_Message_Params *params, void *pData)
{
	callbackCount++;

	CHECK_C(pData == &CALLBACK_DATA);
	CHECK_C(pClient == &client);

	CHECK_EQUAL_C_INT((int)strlen(expectedTopic), (int)topicNameLen);
	CHECK_C(strncmp(expectedTopic, topicName, (size_t) topicNameLen) == 0);

	CHECK_C(memcmp(expectedData, params->payload, expectedDataLen) == 0);
}

TEST_GROUP_C_SETUP(JobsInterfaceTest) {
	IoT_Error_t ret_val = SUCCESS;

	InitMQTTParamsSetup(&mqttInitParams, AWS_IOT_MQTT_HOST, AWS_IOT_MQTT_PORT, false, NULL);
	ret_val = aws_iot_mqtt_init(&client, &mqttInitParams);
	CHECK_EQUAL_C_INT(SUCCESS, ret_val);

	ConnectMQTTParamsSetup(&connectParams, (char *) AWS_IOT_MQTT_CLIENT_ID, (uint16_t) strlen(AWS_IOT_MQTT_CLIENT_ID));
	setTLSRxBufferForConnack(&connectParams, 0, 0);
	ret_val = aws_iot_mqtt_connect(&client, &connectParams);
	CHECK_EQUAL_C_INT(SUCCESS, ret_val);

	ResetTLSBuffer();

	/* Ensure that old data can't be used */
	lastSubscribeMsgLen = 0;
	lastUnsubscribeMsgLen = 0;
	lastPublishMessageTopicLen = 0;
	lastPublishMessagePayloadLen = 0;

	callbackCount = 0;
	expectedTopic = NULL;
	expectedData = NULL;
	expectedDataLen = 0;

	expectError = false;
	expectedJsonContext = false;
}

TEST_GROUP_C_TEARDOWN(JobsInterfaceTest) {
	IoT_Error_t rc = aws_iot_mqtt_disconnect(&client);
	IOT_UNUSED(rc);
}

static void publishTestMessage(AwsIotJobExecutionTopicType topicType, bool withJobId) {
	char replyTopic[MAX_JOB_TOPIC_LENGTH_BYTES + 1];
	AwsIotJobExecutionTopicReplyType replyType;
	char * message;

	if (topicType == JOB_NOTIFY_TOPIC) {
		replyType = JOB_REQUEST_TYPE;
		message = "{\"jobs\":{\"IN_PROGRESS\":[{}]},\"timestamp\":5}";
	} else if (topicType == JOB_NOTIFY_NEXT_TOPIC) {
		replyType = JOB_REQUEST_TYPE;
		message = "{\"execution\":{\"jobId\":\"J1\",\"status\":\"IN_PROGRESS\",\"queuedAt\":50,\"versionNumber\":20},\"timestamp\":5}";
	}
	else {
		replyType = JOB_REJECTED_REPLY_TYPE;
		message = "{\"code\":\"foo\",\"message\":\"bar\", \"clientToken\":\"baz\",\"timestamp\":6}";
	}

	const char *passedJobId = NULL;
	if (withJobId) {
		if (topicType == JOB_WILDCARD_TOPIC) {
			topicType = JOB_DESCRIBE_TOPIC;
		}
		passedJobId = JOB_ID;
	}

	int replyTopicLen = aws_iot_jobs_get_api_topic(
			replyTopic, MAX_JOB_TOPIC_LENGTH_BYTES + 1,
					topicType, replyType, THING_NAME, passedJobId);

	expectedData = message;
	expectedDataLen = strlen(message) + 1;
	expectedTopic = replyTopic;

	IoT_Publish_Message_Params params;
	params.payload = message;
	params.payloadLen = strlen(message);
	params.qos = QOS0;

	int preSendCount = callbackCount;

	setTLSRxBufferWithMsgOnSubscribedTopic(replyTopic, (size_t)replyTopicLen, QOS0, params, message);
	IoT_Error_t error = aws_iot_mqtt_yield(&client, 100);

	CHECK_EQUAL_C_INT(SUCCESS, error);
	CHECK_EQUAL_C_INT(preSendCount + 1, callbackCount);
}

static void testSubscribeAndUnsubscribe(AwsIotJobExecutionTopicType topicType, bool withJobId) {
	char expectedTopic[MAX_JOB_TOPIC_LENGTH_BYTES + 1];
	char topicBuffer[MAX_JOB_TOPIC_LENGTH_BYTES + 1];

	IOT_DEBUG("\n-->Running Jobs Interface Tests - test subscribe/unsubscribe %s %s \n", getJobTopicTypeName(topicType), (withJobId ? "(w/ job id)" : ""));

	AwsIotJobExecutionTopicReplyType replyType;
	if (topicType != JOB_NOTIFY_TOPIC && topicType != JOB_NOTIFY_NEXT_TOPIC) {
		replyType = JOB_WILDCARD_REPLY_TYPE;
	} else {
		replyType = JOB_REQUEST_TYPE;
	}

	const char *passedJobId = NULL;
	if (withJobId) passedJobId = JOB_ID;

	int expectedTopicLength = aws_iot_jobs_get_api_topic(
			expectedTopic, MAX_JOB_TOPIC_LENGTH_BYTES + 1,
					topicType, replyType,
					THING_NAME, passedJobId);

	CHECK_C(expectedTopicLength > 0 && expectedTopicLength < MAX_JOB_TOPIC_LENGTH_BYTES);

	/* Clear to make sure old data doesn't cause the test to pass when we shouldn't */
	lastSubscribeMsgLen = 0;
	lastPublishMessageTopicLen = 0;

	IoT_Publish_Message_Params unused;

	setTLSRxBufferForSuback(expectedTopic, (size_t)expectedTopicLength, QOS0, unused);

	IoT_Error_t iotError;
	if (topicType == JOB_WILDCARD_TOPIC && !withJobId) {
		iotError = aws_iot_jobs_subscribe_to_all_job_messages(
				&client, QOS0, THING_NAME, testCallback, &CALLBACK_DATA, topicBuffer, sizeof(topicBuffer));
	} else {
		iotError = aws_iot_jobs_subscribe_to_job_messages(
				&client, QOS0, THING_NAME, passedJobId, topicType, replyType, testCallback, &CALLBACK_DATA, topicBuffer, sizeof(topicBuffer));
	}

	CHECK_EQUAL_C_INT(SUCCESS, iotError);

	CHECK_EQUAL_C_INT(expectedTopicLength, (int)strlen(topicBuffer));
	CHECK_EQUAL_C_INT(expectedTopicLength, (int)lastSubscribeMsgLen);
	CHECK_EQUAL_C_STRING(expectedTopic, topicBuffer);
	CHECK_EQUAL_C_STRING(expectedTopic, LastSubscribeMessage);

	publishTestMessage(topicType, withJobId);

	LastUnsubscribeMessage[0] = 0;
	lastUnsubscribeMsgLen = 0;

	setTLSRxBufferForUnsuback();
	iotError = aws_iot_jobs_unsubscribe_from_job_messages(&client, topicBuffer);

	CHECK_EQUAL_C_INT(SUCCESS, iotError);

	CHECK_EQUAL_C_STRING(expectedTopic, LastUnsubscribeMessage);

	IOT_DEBUG("-->Success - test subscribe/unsubscribe %s %s \n", getJobTopicTypeName(topicType), (withJobId ? "(w/ job id)" : ""));
}

TEST_C(JobsInterfaceTest, TestSubscribeAndUnsubscribe) {
	testSubscribeAndUnsubscribe(JOB_NOTIFY_TOPIC, false);
	testSubscribeAndUnsubscribe(JOB_NOTIFY_NEXT_TOPIC, false);
	testSubscribeAndUnsubscribe(JOB_DESCRIBE_TOPIC, true);
	testSubscribeAndUnsubscribe(JOB_GET_PENDING_TOPIC, false);
	testSubscribeAndUnsubscribe(JOB_UPDATE_TOPIC, true);
	testSubscribeAndUnsubscribe(JOB_WILDCARD_TOPIC, true);
	testSubscribeAndUnsubscribe(JOB_WILDCARD_TOPIC, false);
	testSubscribeAndUnsubscribe(JOB_START_NEXT_TOPIC, false);
}

static void testSendQuery(bool withJobId, bool withClientToken, AwsIotJobExecutionTopicType topicType) {
	char expectedTopic[MAX_JOB_TOPIC_LENGTH_BYTES + 1];
	char topicBuffer[MAX_JOB_TOPIC_LENGTH_BYTES + 1];
	char messageBuffer[MAX_SIZE_OF_JOB_REQUEST + 1];

	IOT_DEBUG("\n-->Running Jobs Interface Tests - test send query %s %s \n", getJobTopicTypeName(topicType), (withClientToken ? "(w/ clientToken)" : ""));

	char *clientToken = NULL;
	char *bufferToPass = NULL;
	size_t bufferLenToPass = 0;
	if (withClientToken) {
		clientToken = "FooBar";
		bufferToPass = messageBuffer;
		bufferLenToPass = MAX_SIZE_OF_JOB_REQUEST + 1;
	}

	const char *passedJobId = NULL;
	if (withJobId) passedJobId = JOB_ID;

	int expectedTopicLength = aws_iot_jobs_get_api_topic(
			expectedTopic, MAX_JOB_TOPIC_LENGTH_BYTES + 1,
			topicType, JOB_REQUEST_TYPE,
			THING_NAME, passedJobId);

	CHECK_C(expectedTopicLength > 0 && expectedTopicLength <= MAX_JOB_TOPIC_LENGTH_BYTES);

	LastPublishMessageTopic[0] = 0;
	lastPublishMessageTopicLen = 0;
	LastPublishMessagePayload[0] = 0;
	lastPublishMessagePayloadLen = 0;

	setTLSRxBufferForPuback();
	IoT_Error_t iotError;

	if (topicType == JOB_DESCRIBE_TOPIC) {
		AwsIotDescribeJobExecutionRequest request;
		request.executionNumber = 0;
		request.includeJobDocument = false;
		request.clientToken = clientToken;

		iotError = aws_iot_jobs_describe(
				&client, QOS0, THING_NAME, passedJobId, &request, topicBuffer, sizeof(topicBuffer), bufferToPass, bufferLenToPass);
	} else if (topicType == JOB_START_NEXT_TOPIC) {
		AwsIotStartNextPendingJobExecutionRequest request;
		request.statusDetails = NULL;
		request.clientToken = clientToken;

		iotError = aws_iot_jobs_start_next(
				&client, QOS0, THING_NAME, &request, topicBuffer, sizeof(topicBuffer), messageBuffer, sizeof(messageBuffer));
	} else {
		iotError = aws_iot_jobs_send_query(
				&client, QOS0, THING_NAME, passedJobId, clientToken, topicBuffer, sizeof(topicBuffer), bufferToPass, bufferLenToPass, topicType);
	}

	CHECK_EQUAL_C_INT(SUCCESS, iotError);
	CHECK_EQUAL_C_INT(expectedTopicLength, (int)lastPublishMessageTopicLen);
	CHECK_EQUAL_C_STRING(expectedTopic, LastPublishMessageTopic);

	if (withClientToken) {
		CHECK_EQUAL_C_INT((int)strlen(LastPublishMessagePayload), (int)lastPublishMessagePayloadLen);
		CHECK_EQUAL_C_STRING("{\"clientToken\":\"FooBar\"}", LastPublishMessagePayload);
	} else if (topicType == JOB_START_NEXT_TOPIC) {
		CHECK_EQUAL_C_INT(2, (int)lastPublishMessagePayloadLen);
		CHECK_EQUAL_C_STRING("{}", LastPublishMessagePayload);
	} else {
		CHECK_EQUAL_C_INT(0, (int)lastPublishMessagePayloadLen);
	}

	IOT_DEBUG("-->Success - test send query %s %s \n", getJobTopicTypeName(topicType), (withClientToken ? "(w/ clientToken)" : ""));
}

TEST_C(JobsInterfaceTest, TestSendQuery) {
	int token;
	for (token = 0; token < 2; ++token) {
		testSendQuery(false, token == 0, JOB_GET_PENDING_TOPIC);
		testSendQuery(true, token == 0, JOB_DESCRIBE_TOPIC);
		testSendQuery(false, token == 0, JOB_START_NEXT_TOPIC);
	}
}

TEST_C(JobsInterfaceTest, TestSendUpdate) {
	AwsIotJobExecutionUpdateRequest updateRequest;
	updateRequest.clientToken = "FooBar";
	updateRequest.status = JOB_EXECUTION_SUCCEEDED;
	updateRequest.expectedVersion = 1;
	updateRequest.executionNumber = 0;
	updateRequest.statusDetails = NULL;
	updateRequest.includeJobExecutionState = false;
	updateRequest.includeJobDocument = false;

	char expectedTopic[MAX_JOB_TOPIC_LENGTH_BYTES + 1];
	char topicBuffer[MAX_JOB_TOPIC_LENGTH_BYTES + 1];
	char messageBuffer[AWS_IOT_MQTT_TX_BUF_LEN + 1];

	IOT_DEBUG("\n-->Running Jobs Interface Tests - test send update \n");

	int expectedTopicLength = aws_iot_jobs_get_api_topic(
			expectedTopic, MAX_JOB_TOPIC_LENGTH_BYTES + 1,
			JOB_UPDATE_TOPIC, JOB_REQUEST_TYPE,
			THING_NAME, JOB_ID);

	CHECK_C(expectedTopicLength > 0 && expectedTopicLength <= MAX_JOB_TOPIC_LENGTH_BYTES);

	LastPublishMessageTopic[0] = 0;
	lastPublishMessageTopicLen = 0;
	LastPublishMessagePayload[0] = 0;
	lastPublishMessagePayloadLen = 0;

	setTLSRxBufferForPuback();
	IoT_Error_t iotError;
	iotError = aws_iot_jobs_send_update(
			&client, QOS0, THING_NAME, JOB_ID, &updateRequest,
			topicBuffer, sizeof(topicBuffer),
			messageBuffer, sizeof(messageBuffer));

	CHECK_EQUAL_C_INT(SUCCESS, iotError);

	CHECK_EQUAL_C_INT(expectedTopicLength, (int)lastPublishMessageTopicLen);
	CHECK_EQUAL_C_STRING(expectedTopic, LastPublishMessageTopic);

	CHECK_EQUAL_C_STRING("{\"status\":\"SUCCEEDED\",\"expectedVersion\":1,\"clientToken\":\"FooBar\"}", LastPublishMessagePayload);

	IOT_DEBUG("-->Success - test send update \n");
}
