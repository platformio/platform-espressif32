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

#include "aws_iot_jobs_topics.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <CppUTest/TestHarness_c.h>
#include <aws_iot_tests_unit_helper_functions.h>
#include <aws_iot_log.h>

#ifdef __cplusplus
extern "C" {
#endif

TEST_GROUP_C_SETUP(JobsTopicsTests) {
}

TEST_GROUP_C_TEARDOWN(JobsTopicsTests) {
}

#define TEST_THING_NAME "TestThing"
#define TEST_TOPIC_PREFIX "$aws/things/" TEST_THING_NAME "/jobs"
#define TEST_JOB_ID "TestJob"
#define TEST_NEXT_JOB_ID "$next"
#define TEST_TOPIC_W_JOB_PREFIX TEST_TOPIC_PREFIX "/" TEST_JOB_ID

static int generateTopicForTestThingAndJob(
		char *buffer, size_t bufferSize,
		AwsIotJobExecutionTopicType topicType, AwsIotJobExecutionTopicReplyType replyType)
{
	return aws_iot_jobs_get_api_topic(
			buffer, bufferSize, topicType, replyType, TEST_THING_NAME, TEST_JOB_ID);
}

static int generateTopicForTestThing(
		char *buffer, size_t bufferSize,
		AwsIotJobExecutionTopicType topicType, AwsIotJobExecutionTopicReplyType replyType)
{
	return aws_iot_jobs_get_api_topic(
			buffer, bufferSize, topicType, replyType, TEST_THING_NAME, NULL);
}

static void testGenerateValidApiTopic(
		AwsIotJobExecutionTopicType topicType, AwsIotJobExecutionTopicReplyType replyType,
		bool includeThingName, const char *expectedOutput)
{
	int requestedSize;
	int expectedSize = (int)strlen(expectedOutput);

	IOT_DEBUG("\n-->Running Jobs Topics Tests - generate valid topics %s w/ reply type \n", getJobTopicTypeName(topicType), getJobReplyTypeName(replyType));

	if (includeThingName) {
		requestedSize = generateTopicForTestThingAndJob(NULL, 0, topicType, replyType);
		CHECK_EQUAL_C_INT(expectedSize, requestedSize);
	} else {
		requestedSize = generateTopicForTestThing(NULL, 0, topicType, replyType);
		CHECK_EQUAL_C_INT(expectedSize, requestedSize);
	}

	CHECK_C(requestedSize > 3);
	char *buffer = (char *) malloc((size_t)(requestedSize + 1) * sizeof(char));
	buffer[3] = -1;

	if (includeThingName) {
		requestedSize = generateTopicForTestThingAndJob(buffer, 3, topicType, replyType);
	} else {
		requestedSize = generateTopicForTestThing(buffer, 3, topicType, replyType);
	}
	CHECK_EQUAL_C_INT(expectedSize, requestedSize);

	CHECK_EQUAL_C_CHAR(expectedOutput[0], buffer[0]);
	CHECK_EQUAL_C_CHAR(expectedOutput[1], buffer[1]);
	CHECK_EQUAL_C_CHAR('\0', buffer[2]);
	CHECK_EQUAL_C_CHAR(-1, buffer[3]);

	if (includeThingName) {
		requestedSize = generateTopicForTestThingAndJob(buffer, (size_t)requestedSize + 1, topicType, replyType);
	} else {
		requestedSize = generateTopicForTestThing(buffer, (size_t)requestedSize + 1, topicType, replyType);
	}

	CHECK_EQUAL_C_INT(expectedSize, requestedSize);
	CHECK_EQUAL_C_STRING(expectedOutput, buffer);

	free(buffer);

	IOT_DEBUG("-->Success - generate valid topics %s w/ reply type \n", getJobTopicTypeName(topicType), getJobReplyTypeName(replyType));
}

static void testGenerateValidApiTopicWithAllReplyTypes(
		AwsIotJobExecutionTopicType topicType, bool includeThingName,
		const char *expectedBaseTopic)
{
	char buffer[1024];

	testGenerateValidApiTopic(
			topicType, JOB_REQUEST_TYPE, includeThingName, expectedBaseTopic);

	snprintf(buffer, 1024, "%s/accepted", expectedBaseTopic);
	testGenerateValidApiTopic(
				topicType, JOB_ACCEPTED_REPLY_TYPE, includeThingName, buffer);

	snprintf(buffer, 1024, "%s/rejected", expectedBaseTopic);
	testGenerateValidApiTopic(
				topicType, JOB_REJECTED_REPLY_TYPE, includeThingName, buffer);

	snprintf(buffer, 1024, "%s/+", expectedBaseTopic);
	testGenerateValidApiTopic(
				topicType, JOB_WILDCARD_REPLY_TYPE, includeThingName, buffer);

}

TEST_C(JobsTopicsTests, GenerateValidTopics) {
	testGenerateValidApiTopicWithAllReplyTypes(JOB_UPDATE_TOPIC, true, TEST_TOPIC_W_JOB_PREFIX "/update");
	testGenerateValidApiTopicWithAllReplyTypes(JOB_DESCRIBE_TOPIC, true, TEST_TOPIC_W_JOB_PREFIX "/get");
	testGenerateValidApiTopicWithAllReplyTypes(JOB_WILDCARD_TOPIC, true, TEST_TOPIC_W_JOB_PREFIX "/+");
	testGenerateValidApiTopic(JOB_WILDCARD_TOPIC, JOB_REQUEST_TYPE, false, TEST_TOPIC_PREFIX "/#");
	testGenerateValidApiTopic(JOB_NOTIFY_TOPIC, JOB_REQUEST_TYPE, false, TEST_TOPIC_PREFIX "/notify");
	testGenerateValidApiTopic(JOB_NOTIFY_NEXT_TOPIC, JOB_REQUEST_TYPE, false, TEST_TOPIC_PREFIX "/notify-next");
	testGenerateValidApiTopic(JOB_START_NEXT_TOPIC, JOB_REQUEST_TYPE, false, TEST_TOPIC_PREFIX "/start-next");
}

TEST_C(JobsTopicsTests, GenerateWithMissingJobId) {
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_UPDATE_TOPIC, JOB_REQUEST_TYPE));
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_DESCRIBE_TOPIC, JOB_REQUEST_TYPE));
}

TEST_C(JobsTopicsTests, GenerateWithInvalidTopicOrReplyType) {
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_UNRECOGNIZED_TOPIC, JOB_REQUEST_TYPE));
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, -1, JOB_REQUEST_TYPE));

	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_DESCRIBE_TOPIC, JOB_UNRECOGNIZED_TOPIC_TYPE));
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_DESCRIBE_TOPIC, -1));
}

TEST_C(JobsTopicsTests, GenerateWithInvalidCombinations) {
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_NOTIFY_TOPIC, JOB_ACCEPTED_REPLY_TYPE));
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_NOTIFY_TOPIC, JOB_REJECTED_REPLY_TYPE));
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_NOTIFY_TOPIC, JOB_WILDCARD_REPLY_TYPE));
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_NOTIFY_NEXT_TOPIC, JOB_ACCEPTED_REPLY_TYPE));
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_NOTIFY_NEXT_TOPIC, JOB_REJECTED_REPLY_TYPE));
	CHECK_EQUAL_C_INT(-1, generateTopicForTestThing(NULL, 0, JOB_NOTIFY_NEXT_TOPIC, JOB_WILDCARD_REPLY_TYPE));
}

#ifdef __cplusplus
}
#endif
