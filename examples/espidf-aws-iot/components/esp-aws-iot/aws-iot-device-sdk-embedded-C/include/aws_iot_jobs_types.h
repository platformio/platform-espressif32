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

/**
 * @file aws_iot_jobs_types.h
 * @brief Structures defining the interface with the AWS IoT Jobs system
 *
 * This file defines the structures returned by and sent to the AWS IoT Jobs system.
 *
 */

#ifdef DISABLE_IOT_JOBS
#error "Jobs API is disabled"
#endif

#ifndef AWS_IOT_JOBS_TYPES_H_
#define AWS_IOT_JOBS_TYPES_H_

#include <stdbool.h>
#include <stdint.h>
#include "jsmn.h"
#include "timer_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	JOB_EXECUTION_STATUS_NOT_SET = 0,
	JOB_EXECUTION_QUEUED,
	JOB_EXECUTION_IN_PROGRESS,
	JOB_EXECUTION_FAILED,
	JOB_EXECUTION_SUCCEEDED,
	JOB_EXECUTION_CANCELED,
	JOB_EXECUTION_REJECTED,
	/***
	 * Used for any status not in the supported list of statuses
	 */
	JOB_EXECUTION_UNKNOWN_STATUS = 99
} JobExecutionStatus;

extern const char *JOB_EXECUTION_QUEUED_STR;
extern const char *JOB_EXECUTION_IN_PROGRESS_STR;
extern const char *JOB_EXECUTION_FAILED_STR;
extern const char *JOB_EXECUTION_SUCCESS_STR;
extern const char *JOB_EXECUTION_CANCELED_STR;
extern const char *JOB_EXECUTION_REJECTED_STR;

/**
 * Convert a string to its matching status.
 *
 * \return the matching status, or JOB_EXECUTION_UNKNOWN_STATUS if the string was not recognized.
 */
JobExecutionStatus aws_iot_jobs_map_string_to_job_status(const char *str);

/**
 * Convert a status to its string.
 *
 * \return a string representing the status, or null if the status is not recognized.
 */
const char *aws_iot_jobs_map_status_to_string(JobExecutionStatus status);

/**
 * A request to update the status of a job execution.
 */
typedef struct {
	int64_t expectedVersion;	// set to 0 to ignore
	int64_t executionNumber;	// set to 0 to ignore
	JobExecutionStatus status;
	const char *statusDetails;
	bool includeJobExecutionState;
	bool includeJobDocument;
	const char *clientToken;
} AwsIotJobExecutionUpdateRequest;

/**
 * A request to get the status of a job execution.
 */
typedef struct {
	int64_t executionNumber;	// set to 0 to ignore
	bool includeJobDocument;
	const char *clientToken;
} AwsIotDescribeJobExecutionRequest;

/**
 * A request to get and start the next pending (not in a terminal state) job execution for a Thing.
 */
typedef struct {
	const char *statusDetails;
	const char *clientToken;
} AwsIotStartNextPendingJobExecutionRequest;

#ifdef __cplusplus
}
#endif

#endif /* AWS_IOT_JOBS_TYPES_H_ */
