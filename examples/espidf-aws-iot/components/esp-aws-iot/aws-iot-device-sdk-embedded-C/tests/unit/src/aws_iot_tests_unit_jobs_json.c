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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <CppUTest/TestHarness_c.h>
#include <aws_iot_jobs_json.h>
#include <aws_iot_tests_unit_helper_functions.h>
#include <aws_iot_log.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_TOKENS 50

static jsmntok_t tokens[NUM_TOKENS];

TEST_GROUP_C_SETUP(JobsJsonTests) {
}

TEST_GROUP_C_TEARDOWN(JobsJsonTests) {
}

static const char *defaultUpdateRequestSerialized =
		"{\"status\":\"IN_PROGRESS\",\"statusDetails\":{\"Step\":\"1\",\"StepStatus\":\"Foo\"},"
		"\"executionNumber\":2,\"expectedVersion\":1,\"includeJobExecutionState\":true,\"includeJobDocument\":true,\"clientToken\":\"1234\"}";

static void fillDefaultUpdateRequest(AwsIotJobExecutionUpdateRequest *request)
{
	request->status = JOB_EXECUTION_IN_PROGRESS;
	request->statusDetails = "{\"Step\":\"1\",\"StepStatus\":\"Foo\"}";
	request->expectedVersion = 1;
	request->executionNumber = 2;
	request->includeJobExecutionState = true;
	request->includeJobDocument = true;
	request->clientToken = "1234";
}

TEST_C(JobsJsonTests, SerializeUpdateRequest) {
	AwsIotJobExecutionUpdateRequest updateRequest;

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize update request \n");

	fillDefaultUpdateRequest(&updateRequest);

	char buffer[256];
	int charsUsed = aws_iot_jobs_json_serialize_update_job_execution_request(buffer, sizeof(buffer), &updateRequest);
	CHECK_EQUAL_C_INT((int)strlen(defaultUpdateRequestSerialized), charsUsed);
	CHECK_EQUAL_C_STRING(defaultUpdateRequestSerialized, buffer);

	jsmn_parser parser;
	jsmn_init(&parser);

	int num_parsed_tokens = jsmn_parse(&parser, buffer, (size_t)charsUsed, tokens, NUM_TOKENS);
	CHECK_C(num_parsed_tokens > 0);

	IOT_DEBUG("-->Success - serialize update request \n");
}

TEST_C(JobsJsonTests, SerializeUpdateRequestWithNullBuffer) {
	AwsIotJobExecutionUpdateRequest updateRequest;

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize update request w/ null buffer \n");

	fillDefaultUpdateRequest(&updateRequest);

	int charsUsed = aws_iot_jobs_json_serialize_update_job_execution_request(NULL, 0, &updateRequest);
	CHECK_EQUAL_C_INT((int)strlen(defaultUpdateRequestSerialized), charsUsed);

	IOT_DEBUG("-->Success - serialize update request w/ null buffer \n");
}

TEST_C(JobsJsonTests, SerializeUpdateRequestWithTooSmallBuffer) {
	AwsIotJobExecutionUpdateRequest updateRequest;

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize update request w/ too small buffer \n");

	fillDefaultUpdateRequest(&updateRequest);

	char buffer[11];
	// A check character which shouldn't be overwritten as we pass a length of 10
	buffer[10] = -1;

	int charsUsed = aws_iot_jobs_json_serialize_update_job_execution_request(buffer, 10, &updateRequest);
	CHECK_EQUAL_C_INT((int)strlen(defaultUpdateRequestSerialized), charsUsed);

	CHECK_EQUAL_C_CHAR(-1, buffer[10]);
	CHECK_EQUAL_C_CHAR(0, buffer[9]);
	// 9 because the 10th is \0
	CHECK_C(strncmp(defaultUpdateRequestSerialized, buffer, 9) == 0);

	IOT_DEBUG("-->Success - serialize update request w/ too small buffer \n");
}

static void fillDefaultDescribeJobExecutionRequest(
		AwsIotDescribeJobExecutionRequest *request)
{
	request->executionNumber = 1;
	request->includeJobDocument = true;
	request->clientToken = "1234";
}

static const char *defaultDescribeJobExecutionRequestSerialized =
		"{\"clientToken\":\"1234\",\"executionNumber\":1,\"includeJobDocument\":true}";

TEST_C(JobsJsonTests, SerializeDescribeJobExecutionRequest) {
	AwsIotDescribeJobExecutionRequest describeJobExecutionRequest;

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize describe job execution request \n");

	fillDefaultDescribeJobExecutionRequest(&describeJobExecutionRequest);

	char buffer[256];
	int charsUsed = aws_iot_jobs_json_serialize_describe_job_execution_request(buffer, 256, &describeJobExecutionRequest);
	CHECK_EQUAL_C_INT((int)strlen(defaultDescribeJobExecutionRequestSerialized), charsUsed);

	CHECK_EQUAL_C_STRING(defaultDescribeJobExecutionRequestSerialized, buffer);

	jsmn_parser parser;
	jsmn_init(&parser);

	int num_parsed_tokens = jsmn_parse(&parser, buffer, (size_t)charsUsed, tokens, NUM_TOKENS);
	CHECK_C(num_parsed_tokens > 0);

	IOT_DEBUG("-->Success - serialize describe job execution request \n");
}

TEST_C(JobsJsonTests, SerializeDescribeJobExecutionRequestWithNullBuffer) {
	AwsIotDescribeJobExecutionRequest describeJobExecutionRequest;

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize describe job execution request w/ null buffer \n");

	fillDefaultDescribeJobExecutionRequest(&describeJobExecutionRequest);

	int charsUsed = aws_iot_jobs_json_serialize_describe_job_execution_request(NULL, 0, &describeJobExecutionRequest);
	CHECK_EQUAL_C_INT(0, charsUsed);

	IOT_DEBUG("-->Success - serialize describe job execution request w/ null buffer \n");
}

TEST_C(JobsJsonTests, SerializeDescribeJobExecutionRequestWithTooSmallBuffer) {
	AwsIotDescribeJobExecutionRequest describeJobExecutionRequest;

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize describe job execution request w/ too small buffer \n");

	fillDefaultDescribeJobExecutionRequest(&describeJobExecutionRequest);

	char buffer[11];
	// A check character which shouldn't be overwritten as we pass a length of 10
	buffer[10] = -1;

	int charsUsed = aws_iot_jobs_json_serialize_describe_job_execution_request(buffer, 10, &describeJobExecutionRequest);
	CHECK_EQUAL_C_INT((int)strlen(defaultDescribeJobExecutionRequestSerialized), charsUsed);

	CHECK_EQUAL_C_CHAR(-1, buffer[10]);
	CHECK_EQUAL_C_CHAR(0, buffer[9]);
	// 9 because the 10th is \0
	CHECK_C(strncmp(defaultDescribeJobExecutionRequestSerialized, buffer, 9) == 0);

	IOT_DEBUG("-->Success - serialize describe job execution request w/ too small buffer \n");
}

static void fillDefaultStartNextRequest(AwsIotStartNextPendingJobExecutionRequest *request)
{
	request->statusDetails = "{\"Step\":\"1\",\"StepStatus\":\"Foo\"}";
	request->clientToken = "1234";
}

static const char *defaultStartNextRequestSerialized =
		"{\"statusDetails\":{\"Step\":\"1\",\"StepStatus\":\"Foo\"},\"clientToken\":\"1234\"}";

TEST_C(JobsJsonTests, SerializeStartNextRequest) {
	AwsIotStartNextPendingJobExecutionRequest startNextRequest;
	fillDefaultStartNextRequest(&startNextRequest);

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize start next request \n");

	char buffer[256];
	int charsUsed = aws_iot_jobs_json_serialize_start_next_job_execution_request(buffer, 256, &startNextRequest);
	CHECK_EQUAL_C_INT((int)strlen(defaultStartNextRequestSerialized), charsUsed);

	CHECK_EQUAL_C_STRING(defaultStartNextRequestSerialized, buffer);
	jsmn_parser parser;
	jsmn_init(&parser);

	int num_parsed_tokens = jsmn_parse(&parser, buffer, (size_t)charsUsed, tokens, NUM_TOKENS);
	CHECK_C(num_parsed_tokens > 0);

	IOT_DEBUG("-->Success - serialize start next request \n");
}

TEST_C(JobsJsonTests, SerializeStartNextRequestWithNullBuffer) {
	AwsIotStartNextPendingJobExecutionRequest startNextRequest;

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize start next request w/ null buffer \n");

	fillDefaultStartNextRequest(&startNextRequest);

	int charsUsed = aws_iot_jobs_json_serialize_start_next_job_execution_request(NULL, 0, &startNextRequest);
	CHECK_EQUAL_C_INT((int)strlen(defaultStartNextRequestSerialized), charsUsed);

	IOT_DEBUG("-->Success - serialize start next request w/ null buffer \n");
}

TEST_C(JobsJsonTests, SerializeStartNextRequestWithTooSmallBuffer) {
	AwsIotStartNextPendingJobExecutionRequest startNextRequest;

	IOT_DEBUG("\n-->Running Jobs Json Tests - serialize start next request w/ too small buffer \n");

	fillDefaultStartNextRequest(&startNextRequest);

	char buffer[11];
	// A check character which shouldn't be overwritten as we pass a length of 10
	buffer[10] = -1;

	int charsUsed = aws_iot_jobs_json_serialize_start_next_job_execution_request(buffer, 10, &startNextRequest);
	CHECK_EQUAL_C_INT((int) strlen(defaultStartNextRequestSerialized), charsUsed);

	CHECK_EQUAL_C_CHAR(-1, buffer[10]);
	CHECK_EQUAL_C_CHAR(0, buffer[9]);
	// 9 because the 10th is \0
	CHECK_C(strncmp(defaultStartNextRequestSerialized, buffer, 9) == 0);

	IOT_DEBUG("-->Success - serialize start next request w/ too small buffer \n");
}

#ifdef __cplusplus
}
#endif
