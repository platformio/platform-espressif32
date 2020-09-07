/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/**
 *
 * This example takes the parameters from the aws_iot_config.h file and establishes
 * a connection to the AWS IoT MQTT Platform. It performs several operations to
 * demonstrate the basic capabilities of the AWS IoT Jobs platform.
 *
 * If all the certs are correct, you should see the list of pending Job Executions
 * printed out by the iot_get_pending_callback_handler. If there are any existing pending
 * job executions each will be processed one at a time in the iot_next_job_callback_handler.
 * After all of the pending jobs have been processed the program will wait for
 * notifications for new pending jobs and process them one at a time as they come in.
 *
 * In the main body you can see how each callback is registered for each corresponding
 * Jobs topic.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "aws_iot_config.h"
#include "aws_iot_json_utils.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_jobs_interface.h"

/**
 * @brief Default cert location
 */
static char certDirectory[PATH_MAX + 1] = "../../../certs";

/**
 * @brief Default MQTT HOST URL is pulled from the aws_iot_config.h
 */
static char HostAddress[255] = AWS_IOT_MQTT_HOST;

/**
 * @brief Default MQTT port is pulled from the aws_iot_config.h
 */
static uint32_t port = AWS_IOT_MQTT_PORT;

static jsmn_parser jsonParser;
static jsmntok_t jsonTokenStruct[MAX_JSON_TOKEN_EXPECTED];
static int32_t tokenCount;

static void iot_get_pending_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
									IoT_Publish_Message_Params *params, void *pData) {
	IOT_UNUSED(pData);
	IOT_UNUSED(pClient);
	IOT_INFO("\nJOB_GET_PENDING_TOPIC callback");
	IOT_INFO("topic: %.*s", topicNameLen, topicName);
	IOT_INFO("payload: %.*s", (int) params->payloadLen, (char *)params->payload);

	jsmn_init(&jsonParser);

	tokenCount = jsmn_parse(&jsonParser, params->payload, (int) params->payloadLen, jsonTokenStruct, MAX_JSON_TOKEN_EXPECTED);

	if(tokenCount < 0) {
		IOT_WARN("Failed to parse JSON: %d", tokenCount);
		return;
	}

	/* Assume the top-level element is an object */
	if(tokenCount < 1 || jsonTokenStruct[0].type != JSMN_OBJECT) {
		IOT_WARN("Top Level is not an object");
		return;
	}

	jsmntok_t *jobs;

	jobs = findToken("inProgressJobs", params->payload, jsonTokenStruct);

	if (jobs) {
		IOT_INFO("inProgressJobs: %.*s", jobs->end - jobs->start, (char *)params->payload + jobs->start);
	}

	jobs = findToken("queuedJobs", params->payload, jsonTokenStruct);

	if (jobs) {
		IOT_INFO("queuedJobs: %.*s", jobs->end - jobs->start, (char *)params->payload + jobs->start);
	}
}

static void iot_next_job_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
									IoT_Publish_Message_Params *params, void *pData) {
	char topicToPublishUpdate[MAX_JOB_TOPIC_LENGTH_BYTES];
	char messageBuffer[200];

	IOT_UNUSED(pData);
	IOT_UNUSED(pClient);
	IOT_INFO("\nJOB_NOTIFY_NEXT_TOPIC / JOB_DESCRIBE_TOPIC($next) callback");
	IOT_INFO("topic: %.*s", topicNameLen, topicName);
	IOT_INFO("payload: %.*s", (int) params->payloadLen, (char *)params->payload);

	jsmn_init(&jsonParser);

	tokenCount = jsmn_parse(&jsonParser, params->payload, (int) params->payloadLen, jsonTokenStruct, MAX_JSON_TOKEN_EXPECTED);

	if(tokenCount < 0) {
		IOT_WARN("Failed to parse JSON: %d", tokenCount);
		return;
	}

	/* Assume the top-level element is an object */
	if(tokenCount < 1 || jsonTokenStruct[0].type != JSMN_OBJECT) {
		IOT_WARN("Top Level is not an object");
		return;
	}

	jsmntok_t *tokExecution;

	tokExecution = findToken("execution", params->payload, jsonTokenStruct);

	if (tokExecution) {
		IOT_INFO("execution: %.*s", tokExecution->end - tokExecution->start, (char *)params->payload + tokExecution->start);

		jsmntok_t *tok;

		tok = findToken("jobId", params->payload, tokExecution);

		if (tok) {
			IoT_Error_t rc;
			char jobId[MAX_SIZE_OF_JOB_ID + 1];
			AwsIotJobExecutionUpdateRequest updateRequest;

			rc = parseStringValue(jobId, MAX_SIZE_OF_JOB_ID + 1, params->payload, tok);
			if(SUCCESS != rc) {
				IOT_ERROR("parseStringValue returned error : %d ", rc);
				return;
			}

			IOT_INFO("jobId: %s", jobId);

			tok = findToken("jobDocument", params->payload, tokExecution);

			/*
			 * Do your job processing here.
			 */

			if (tok) {
				IOT_INFO("jobDocument: %.*s", tok->end - tok->start, (char *)params->payload + tok->start);
				/* Alternatively if the job still has more steps the status can be set to JOB_EXECUTION_IN_PROGRESS instead */
				updateRequest.status = JOB_EXECUTION_SUCCEEDED;
				updateRequest.statusDetails = "{\"exampleDetail\":\"a value appropriate for your successful job\"}";
			} else {
				updateRequest.status = JOB_EXECUTION_FAILED;
				updateRequest.statusDetails = "{\"failureDetail\":\"Unable to process job document\"}";
			}

			updateRequest.expectedVersion = 0;
			updateRequest.executionNumber = 0;
			updateRequest.includeJobExecutionState = false;
			updateRequest.includeJobDocument = false;
			updateRequest.clientToken = NULL;

			rc = aws_iot_jobs_send_update(pClient, QOS0, AWS_IOT_MY_THING_NAME, jobId, &updateRequest,
					topicToPublishUpdate, sizeof(topicToPublishUpdate), messageBuffer, sizeof(messageBuffer));
			if(SUCCESS != rc) {
				IOT_ERROR("aws_iot_jobs_send_update returned error : %d ", rc);
				return;
			}
		}
	} else {
		IOT_INFO("execution property not found, nothing to do");
	}
}

static void iot_update_accepted_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
									IoT_Publish_Message_Params *params, void *pData) {
	IOT_UNUSED(pData);
	IOT_UNUSED(pClient);
	IOT_INFO("\nJOB_UPDATE_TOPIC / accepted callback");
	IOT_INFO("topic: %.*s", topicNameLen, topicName);
	IOT_INFO("payload: %.*s", (int) params->payloadLen, (char *)params->payload);
}

static void iot_update_rejected_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
									IoT_Publish_Message_Params *params, void *pData) {
	IOT_UNUSED(pData);
	IOT_UNUSED(pClient);
	IOT_INFO("\nJOB_UPDATE_TOPIC / rejected callback");
	IOT_INFO("topic: %.*s", topicNameLen, topicName);
	IOT_INFO("payload: %.*s", (int) params->payloadLen, (char *)params->payload);

	/* Do error handling here for when the update was rejected */
}

static void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data) {
	IOT_WARN("MQTT Disconnect");
	IoT_Error_t rc = FAILURE;

	if(NULL == pClient) {
		return;
	}

	IOT_UNUSED(data);

	if(aws_iot_is_autoreconnect_enabled(pClient)) {
		IOT_INFO("Auto Reconnect is enabled, Reconnecting attempt will start now");
	} else {
		IOT_WARN("Auto Reconnect not enabled. Starting manual reconnect...");
		rc = aws_iot_mqtt_attempt_reconnect(pClient);
		if(NETWORK_RECONNECTED == rc) {
			IOT_WARN("Manual Reconnect Successful");
		} else {
			IOT_WARN("Manual Reconnect Failed - %d", rc);
		}
	}
}

int main(int argc, char **argv) {
	char rootCA[PATH_MAX + 1];
	char clientCRT[PATH_MAX + 1];
	char clientKey[PATH_MAX + 1];
	char CurrentWD[PATH_MAX + 1];
	char cPayload[100];

	IoT_Error_t rc = FAILURE;

	AWS_IoT_Client client;
	IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
	IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

	IoT_Publish_Message_Params paramsQOS0;

	getcwd(CurrentWD, sizeof(CurrentWD));
	snprintf(rootCA, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_ROOT_CA_FILENAME);
	snprintf(clientCRT, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_CERTIFICATE_FILENAME);
	snprintf(clientKey, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_PRIVATE_KEY_FILENAME);

	IOT_DEBUG("rootCA %s", rootCA);
	IOT_DEBUG("clientCRT %s", clientCRT);
	IOT_DEBUG("clientKey %s", clientKey);

	mqttInitParams.enableAutoReconnect = false; // We enable this later below
	mqttInitParams.pHostURL = HostAddress;
	mqttInitParams.port = port;
	mqttInitParams.pRootCALocation = rootCA;
	mqttInitParams.pDeviceCertLocation = clientCRT;
	mqttInitParams.pDevicePrivateKeyLocation = clientKey;
	mqttInitParams.mqttCommandTimeout_ms = 20000;
	mqttInitParams.tlsHandshakeTimeout_ms = 5000;
	mqttInitParams.isSSLHostnameVerify = true;
	mqttInitParams.disconnectHandler = disconnectCallbackHandler;
	mqttInitParams.disconnectHandlerData = NULL;

	rc = aws_iot_mqtt_init(&client, &mqttInitParams);
	if(SUCCESS != rc) {
		IOT_ERROR("aws_iot_mqtt_init returned error : %d ", rc);
		return rc;
	}

	connectParams.keepAliveIntervalInSec = 600;
	connectParams.isCleanSession = true;
	connectParams.MQTTVersion = MQTT_3_1_1;
	connectParams.pClientID = AWS_IOT_MQTT_CLIENT_ID;
	connectParams.clientIDLen = (uint16_t) strlen(AWS_IOT_MQTT_CLIENT_ID);
	connectParams.isWillMsgPresent = false;

	IOT_INFO("Connecting...");
	rc = aws_iot_mqtt_connect(&client, &connectParams);
	if(SUCCESS != rc) {
		IOT_ERROR("Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
		return rc;
	}
	/*
	 * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
	 *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
	 *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
	 */
	rc = aws_iot_mqtt_autoreconnect_set_status(&client, true);
	if(SUCCESS != rc) {
		IOT_ERROR("Unable to set Auto Reconnect to true - %d", rc);
		return rc;
	}

	char topicToSubscribeGetPending[MAX_JOB_TOPIC_LENGTH_BYTES];
	char topicToSubscribeNotifyNext[MAX_JOB_TOPIC_LENGTH_BYTES];
	char topicToSubscribeGetNext[MAX_JOB_TOPIC_LENGTH_BYTES];
	char topicToSubscribeUpdateAccepted[MAX_JOB_TOPIC_LENGTH_BYTES];
	char topicToSubscribeUpdateRejected[MAX_JOB_TOPIC_LENGTH_BYTES];

	char topicToPublishGetPending[MAX_JOB_TOPIC_LENGTH_BYTES];
	char topicToPublishGetNext[MAX_JOB_TOPIC_LENGTH_BYTES];

	rc = aws_iot_jobs_subscribe_to_job_messages(
		&client, QOS0, AWS_IOT_MY_THING_NAME, NULL, JOB_GET_PENDING_TOPIC, JOB_WILDCARD_REPLY_TYPE,
		iot_get_pending_callback_handler, NULL, topicToSubscribeGetPending, sizeof(topicToSubscribeGetPending));

	if(SUCCESS != rc) {
		IOT_ERROR("Error subscribing JOB_GET_PENDING_TOPIC: %d ", rc);
		return rc;
	}

	rc = aws_iot_jobs_subscribe_to_job_messages(
		&client, QOS0, AWS_IOT_MY_THING_NAME, NULL, JOB_NOTIFY_NEXT_TOPIC, JOB_REQUEST_TYPE,
		iot_next_job_callback_handler, NULL, topicToSubscribeNotifyNext, sizeof(topicToSubscribeNotifyNext));

	if(SUCCESS != rc) {
		IOT_ERROR("Error subscribing JOB_NOTIFY_NEXT_TOPIC: %d ", rc);
		return rc;
	}

	rc = aws_iot_jobs_subscribe_to_job_messages(
		&client, QOS0, AWS_IOT_MY_THING_NAME, JOB_ID_NEXT, JOB_DESCRIBE_TOPIC, JOB_WILDCARD_REPLY_TYPE,
		iot_next_job_callback_handler, NULL, topicToSubscribeGetNext, sizeof(topicToSubscribeGetNext));

	if(SUCCESS != rc) {
		IOT_ERROR("Error subscribing JOB_DESCRIBE_TOPIC ($next): %d ", rc);
		return rc;
	}

	rc = aws_iot_jobs_subscribe_to_job_messages(
		&client, QOS0, AWS_IOT_MY_THING_NAME, JOB_ID_WILDCARD, JOB_UPDATE_TOPIC, JOB_ACCEPTED_REPLY_TYPE,
		iot_update_accepted_callback_handler, NULL, topicToSubscribeUpdateAccepted, sizeof(topicToSubscribeUpdateAccepted));

	if(SUCCESS != rc) {
		IOT_ERROR("Error subscribing JOB_UPDATE_TOPIC/accepted: %d ", rc);
		return rc;
	}

	rc = aws_iot_jobs_subscribe_to_job_messages(
		&client, QOS0, AWS_IOT_MY_THING_NAME, JOB_ID_WILDCARD, JOB_UPDATE_TOPIC, JOB_REJECTED_REPLY_TYPE,
		iot_update_rejected_callback_handler, NULL, topicToSubscribeUpdateRejected, sizeof(topicToSubscribeUpdateRejected));

	if(SUCCESS != rc) {
		IOT_ERROR("Error subscribing JOB_UPDATE_TOPIC/rejected: %d ", rc);
		return rc;
	}

	paramsQOS0.qos = QOS0;
	paramsQOS0.payload = (void *) cPayload;
	paramsQOS0.isRetained = 0;
	paramsQOS0.payloadLen = strlen(cPayload);

	rc = aws_iot_jobs_send_query(&client, QOS0, AWS_IOT_MY_THING_NAME, NULL, NULL, topicToPublishGetPending, sizeof(topicToPublishGetPending), NULL, 0, JOB_GET_PENDING_TOPIC);
	if(SUCCESS != rc) {
		IOT_ERROR("Error calling aws_iot_jobs_send_query: %d ", rc);
		return rc;
	}

	AwsIotDescribeJobExecutionRequest describeRequest;
	describeRequest.executionNumber = 0;
	describeRequest.includeJobDocument = true;
	describeRequest.clientToken = NULL;

	rc = aws_iot_jobs_describe(&client, QOS0, AWS_IOT_MY_THING_NAME, JOB_ID_NEXT, &describeRequest, topicToPublishGetNext, sizeof(topicToPublishGetNext), NULL, 0);

	while(SUCCESS == rc) {
		//Max time the yield function will wait for read messages
		rc = aws_iot_mqtt_yield(&client, 50000);
	}

	return rc;
}
