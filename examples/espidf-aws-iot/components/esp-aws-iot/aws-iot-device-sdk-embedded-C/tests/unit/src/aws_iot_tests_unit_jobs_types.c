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
#include <aws_iot_jobs_types.h>
#include <aws_iot_tests_unit_helper_functions.h>
#include <aws_iot_log.h>

#ifdef __cplusplus
extern "C" {
#endif

TEST_GROUP_C_SETUP(JobsTypesTests) {
}

TEST_GROUP_C_TEARDOWN(JobsTypesTests) {
}

TEST_C(JobsTypesTests, StringToStatus) {
	char emptyString = '\0';

	IOT_DEBUG("\n-->Running Jobs Types Tests - string to status \n");

	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_STATUS_NOT_SET, (int)aws_iot_jobs_map_string_to_job_status(NULL));
	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_STATUS_NOT_SET, (int)aws_iot_jobs_map_string_to_job_status(&emptyString));
	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_QUEUED, (int)aws_iot_jobs_map_string_to_job_status("QUEUED"));
	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_IN_PROGRESS, (int)aws_iot_jobs_map_string_to_job_status("IN_PROGRESS"));
	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_FAILED, (int)aws_iot_jobs_map_string_to_job_status("FAILED"));
	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_SUCCEEDED, (int)aws_iot_jobs_map_string_to_job_status("SUCCEEDED"));
	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_CANCELED, (int)aws_iot_jobs_map_string_to_job_status("CANCELED"));
	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_REJECTED, (int)aws_iot_jobs_map_string_to_job_status("REJECTED"));
	CHECK_EQUAL_C_INT((int)JOB_EXECUTION_UNKNOWN_STATUS, (int)aws_iot_jobs_map_string_to_job_status("invalidStatus"));

	IOT_DEBUG("-->Success - string to status \n");
}

TEST_C(JobsTypesTests, StatusToString) {
	IOT_DEBUG("\n-->Running Jobs Types Tests - status to string \n");

	CHECK_EQUAL_C_STRING("QUEUED", aws_iot_jobs_map_status_to_string(JOB_EXECUTION_QUEUED));
	CHECK_EQUAL_C_STRING("IN_PROGRESS", aws_iot_jobs_map_status_to_string(JOB_EXECUTION_IN_PROGRESS));
	CHECK_EQUAL_C_STRING("FAILED", aws_iot_jobs_map_status_to_string(JOB_EXECUTION_FAILED));
	CHECK_EQUAL_C_STRING("SUCCEEDED", aws_iot_jobs_map_status_to_string(JOB_EXECUTION_SUCCEEDED));
	CHECK_EQUAL_C_STRING("CANCELED", aws_iot_jobs_map_status_to_string(JOB_EXECUTION_CANCELED));
	CHECK_EQUAL_C_STRING("REJECTED", aws_iot_jobs_map_status_to_string(JOB_EXECUTION_REJECTED));
	CHECK_EQUAL_C_STRING(NULL, aws_iot_jobs_map_status_to_string(JOB_EXECUTION_STATUS_NOT_SET));
	CHECK_EQUAL_C_STRING(NULL, aws_iot_jobs_map_status_to_string(JOB_EXECUTION_UNKNOWN_STATUS));

	IOT_DEBUG("-->Success - status to string \n");
}

#ifdef __cplusplus
}
#endif
