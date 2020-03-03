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

#ifndef DISABLE_IOT_JOBS
#ifndef DISABLE_IOT_JOBS_INTERFACE

#include "aws_iot_test_integration_common.h"
#include <aws_iot_jobs_interface.h>
#include <aws_iot_jobs_json.h>
#include <inttypes.h>
#include <stdio.h>
#include <assert.h>

static AWS_IoT_Client client;

static jsmn_parser jsonParser;
static jsmntok_t jsonTokenStruct[MAX_JSON_TOKEN_EXPECTED];
static int32_t tokenCount;

static void aws_iot_mqtt_tests_disconnect_callback_handler(AWS_IoT_Client *pClient, void *param) {
}

void iot_get_pending_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
									IoT_Publish_Message_Params *params, void *pData) {
	IoT_Error_t rc = SUCCESS;
	char topicToPublishGetNext[MAX_JOB_TOPIC_LENGTH_BYTES];

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

	AwsIotDescribeJobExecutionRequest describeRequest;
	describeRequest.executionNumber = 0;
	describeRequest.includeJobDocument = true;
	describeRequest.clientToken = NULL;

	rc = aws_iot_jobs_describe(&client, QOS0, AWS_IOT_MY_THING_NAME, JOB_ID_NEXT, &describeRequest, topicToPublishGetNext, sizeof(topicToPublishGetNext), NULL, 0);
}

void iot_next_job_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
									IoT_Publish_Message_Params *params, void *pData) {
	char topicToPublishUpdate[MAX_JOB_TOPIC_LENGTH_BYTES];
	char messageBuffer[200];

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
		}
	} else {
		IOT_INFO("execution property not found, nothing to do, jobs integration test complete");
		
		bool *callbackDone = (bool *) pData;
		*callbackDone = true;
	}
}

int aws_iot_jobs_basic_test() {
	char certDirectory[15] = "../../certs";
	char clientCRT[PATH_MAX + 1];
	char root_CA[PATH_MAX + 1];
	char clientKey[PATH_MAX + 1];
	char CurrentWD[PATH_MAX + 1];
	char clientId[50];
	char cPayload[100];
	IoT_Client_Init_Params initParams = IoT_Client_Init_Params_initializer;
	IoT_Client_Connect_Params connectParams;
	IoT_Publish_Message_Params paramsQOS0;
	IoT_Error_t rc = SUCCESS;
	struct timeval connectTime, waitCallBackTime;
	struct timeval start, end;
	unsigned int connectCounter = 0;

	IOT_DEBUG("\nConnecting Client ");
	do {
		getcwd(CurrentWD, sizeof(CurrentWD));
		snprintf(root_CA, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_ROOT_CA_FILENAME);
		snprintf(clientCRT, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_CERTIFICATE_FILENAME);
		snprintf(clientKey, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_PRIVATE_KEY_FILENAME);
		srand((unsigned int)time(NULL));
		snprintf(clientId, 50, "%s_%d", INTEGRATION_TEST_CLIENT_ID, rand() % 10000);

		printf("\n\nClient ID : %s \n", clientId);

		IOT_DEBUG("Root CA Path : %s\n clientCRT : %s\n clientKey : %s\n", root_CA, clientCRT, clientKey);
		initParams.pHostURL = AWS_IOT_MQTT_HOST;
		initParams.port = AWS_IOT_MQTT_PORT;
		initParams.pRootCALocation = root_CA;
		initParams.pDeviceCertLocation = clientCRT;
		initParams.pDevicePrivateKeyLocation = clientKey;
		initParams.mqttCommandTimeout_ms = 10000;
		initParams.tlsHandshakeTimeout_ms = 10000;
		initParams.disconnectHandler = aws_iot_mqtt_tests_disconnect_callback_handler;
		initParams.enableAutoReconnect = false;
		aws_iot_mqtt_init(&client, &initParams);

		connectParams.keepAliveIntervalInSec = 10;
		connectParams.isCleanSession = true;
		connectParams.MQTTVersion = MQTT_3_1_1;
		connectParams.pClientID = (char *)&clientId;
		connectParams.clientIDLen = strlen(clientId);
		connectParams.isWillMsgPresent = false;
		connectParams.pUsername = NULL;
		connectParams.usernameLen = 0;
		connectParams.pPassword = NULL;
		connectParams.passwordLen = 0;

		gettimeofday(&start, NULL);
		rc = aws_iot_mqtt_connect(&client, &connectParams);
		gettimeofday(&end, NULL);
		timersub(&end, &start, &connectTime);

		connectCounter++;
	} while(rc != SUCCESS && connectCounter < CONNECT_MAX_ATTEMPT_COUNT);

	if(SUCCESS == rc) {
		IOT_DEBUG("## Connect Success. Time sec: %ld, usec: %ld\n", (long int)connectTime.tv_sec, (long int)connectTime.tv_usec);
	} else {
		IOT_ERROR("## Connect Failed. error code %d\n", rc);
		return -1;
	}

	bool callbackDone = false;
	char topicToSubscribeGetPending[MAX_JOB_TOPIC_LENGTH_BYTES];
	char topicToSubscribeNotifyNext[MAX_JOB_TOPIC_LENGTH_BYTES];
	char topicToSubscribeGetNext[MAX_JOB_TOPIC_LENGTH_BYTES];

	char topicToPublishGetPending[MAX_JOB_TOPIC_LENGTH_BYTES];

	rc = aws_iot_jobs_subscribe_to_job_messages(
		&client, QOS0, AWS_IOT_MY_THING_NAME, NULL, JOB_GET_PENDING_TOPIC, JOB_WILDCARD_REPLY_TYPE,
		iot_get_pending_callback_handler, &callbackDone, topicToSubscribeGetPending, sizeof(topicToSubscribeGetPending));

	if(SUCCESS != rc) {
		IOT_ERROR("Error subscribing JOB_GET_PENDING_TOPIC: %d ", rc);
		return rc;
	}

	rc = aws_iot_jobs_subscribe_to_job_messages(
		&client, QOS0, AWS_IOT_MY_THING_NAME, NULL, JOB_NOTIFY_NEXT_TOPIC, JOB_REQUEST_TYPE,
		iot_next_job_callback_handler, &callbackDone, topicToSubscribeNotifyNext, sizeof(topicToSubscribeNotifyNext));

	if(SUCCESS != rc) {
		IOT_ERROR("Error subscribing JOB_NOTIFY_NEXT_TOPIC: %d ", rc);
		return rc;
	}

	rc = aws_iot_jobs_subscribe_to_job_messages(
		&client, QOS0, AWS_IOT_MY_THING_NAME, JOB_ID_NEXT, JOB_DESCRIBE_TOPIC, JOB_WILDCARD_REPLY_TYPE,
		iot_next_job_callback_handler, &callbackDone, topicToSubscribeGetNext, sizeof(topicToSubscribeGetNext));

	if(SUCCESS != rc) {
		IOT_ERROR("Error subscribing JOB_DESCRIBE_TOPIC ($next): %d ", rc);
		return rc;
	}

	paramsQOS0.qos = QOS0;
	paramsQOS0.payload = (void *) cPayload;
	paramsQOS0.isRetained = 0;
	paramsQOS0.payloadLen = strlen(cPayload);

	rc = aws_iot_jobs_send_query(&client, QOS0, AWS_IOT_MY_THING_NAME, NULL, NULL, topicToPublishGetPending, sizeof(topicToPublishGetPending), NULL, 0, JOB_GET_PENDING_TOPIC);
	gettimeofday(&start, NULL);
	while (!callbackDone) {
		aws_iot_mqtt_yield(&client, 5000);
		
		gettimeofday(&end, NULL);
		timersub(&end, &start, &waitCallBackTime);
		
		if(waitCallBackTime.tv_sec > 10) break;
	}

	return 0;
}

#endif
#endif
