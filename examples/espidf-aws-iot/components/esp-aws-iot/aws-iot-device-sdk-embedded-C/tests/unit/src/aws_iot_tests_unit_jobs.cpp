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

#include <CppUTest/CommandLineTestRunner.h>
#include <CppUTest/TestHarness_c.h>


TEST_GROUP_C(JobsTypesTests) {
  TEST_GROUP_C_SETUP_WRAPPER(JobsTypesTests)
  TEST_GROUP_C_TEARDOWN_WRAPPER(JobsTypesTests)
};

TEST_GROUP_C_WRAPPER(JobsTypesTests, StringToStatus)
TEST_GROUP_C_WRAPPER(JobsTypesTests, StatusToString)

TEST_GROUP_C(JobsJsonTests) {
  TEST_GROUP_C_SETUP_WRAPPER(JobsJsonTests)
  TEST_GROUP_C_TEARDOWN_WRAPPER(JobsJsonTests)
};

TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeUpdateRequest)
TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeUpdateRequestWithNullBuffer)
TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeUpdateRequestWithTooSmallBuffer)
TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeDescribeJobExecutionRequest)
TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeDescribeJobExecutionRequestWithNullBuffer)
TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeDescribeJobExecutionRequestWithTooSmallBuffer)
TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeStartNextRequest)
TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeStartNextRequestWithNullBuffer)
TEST_GROUP_C_WRAPPER(JobsJsonTests, SerializeStartNextRequestWithTooSmallBuffer)

TEST_GROUP_C(JobsTopicsTests) {
  TEST_GROUP_C_SETUP_WRAPPER(JobsTopicsTests)
  TEST_GROUP_C_TEARDOWN_WRAPPER(JobsTopicsTests)
};

TEST_GROUP_C_WRAPPER(JobsTopicsTests, GenerateValidTopics)
TEST_GROUP_C_WRAPPER(JobsTopicsTests, GenerateWithMissingJobId)
TEST_GROUP_C_WRAPPER(JobsTopicsTests, GenerateWithInvalidTopicOrReplyType)
TEST_GROUP_C_WRAPPER(JobsTopicsTests, GenerateWithInvalidCombinations)

TEST_GROUP_C(JobsInterfaceTest) {
	TEST_GROUP_C_SETUP_WRAPPER(JobsInterfaceTest)
	TEST_GROUP_C_TEARDOWN_WRAPPER(JobsInterfaceTest)
};

TEST_GROUP_C_WRAPPER(JobsInterfaceTest, TestSubscribeAndUnsubscribe)
TEST_GROUP_C_WRAPPER(JobsInterfaceTest, TestSendQuery)
TEST_GROUP_C_WRAPPER(JobsInterfaceTest, TestSendUpdate)
