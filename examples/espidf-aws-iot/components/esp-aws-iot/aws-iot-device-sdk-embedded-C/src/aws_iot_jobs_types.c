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

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include "aws_iot_jobs_types.h"

const char *JOB_EXECUTION_QUEUED_STR = "QUEUED";
const char *JOB_EXECUTION_IN_PROGRESS_STR = "IN_PROGRESS";
const char *JOB_EXECUTION_FAILED_STR = "FAILED";
const char *JOB_EXECUTION_SUCCEEDED_STR = "SUCCEEDED";
const char *JOB_EXECUTION_CANCELED_STR = "CANCELED";
const char *JOB_EXECUTION_REJECTED_STR = "REJECTED";

JobExecutionStatus aws_iot_jobs_map_string_to_job_status(const char *str) {
	if (str == NULL || str[0] == '\0') {
		return JOB_EXECUTION_STATUS_NOT_SET;
	} else if (strcmp(str, JOB_EXECUTION_QUEUED_STR) == 0) {
		return JOB_EXECUTION_QUEUED;
	} else if(strcmp(str, JOB_EXECUTION_IN_PROGRESS_STR) == 0) {
		return JOB_EXECUTION_IN_PROGRESS;
	} else if(strcmp(str, JOB_EXECUTION_FAILED_STR) == 0) {
		return JOB_EXECUTION_FAILED;
	} else if(strcmp(str, JOB_EXECUTION_SUCCEEDED_STR) == 0) {
		return JOB_EXECUTION_SUCCEEDED;
	} else if(strcmp(str, JOB_EXECUTION_CANCELED_STR) == 0) {
		return JOB_EXECUTION_CANCELED;
	} else if(strcmp(str, JOB_EXECUTION_REJECTED_STR) == 0) {
		return JOB_EXECUTION_REJECTED;
	} else {
		return JOB_EXECUTION_UNKNOWN_STATUS;
	}
}

const char *aws_iot_jobs_map_status_to_string(JobExecutionStatus status) {
	switch(status) {
	case JOB_EXECUTION_QUEUED:
		return JOB_EXECUTION_QUEUED_STR;
	case JOB_EXECUTION_IN_PROGRESS:
		return JOB_EXECUTION_IN_PROGRESS_STR;
	case JOB_EXECUTION_FAILED:
		return JOB_EXECUTION_FAILED_STR;
	case JOB_EXECUTION_SUCCEEDED:
		return JOB_EXECUTION_SUCCEEDED_STR;
	case JOB_EXECUTION_CANCELED:
		return JOB_EXECUTION_CANCELED_STR;
	case JOB_EXECUTION_REJECTED:
		return JOB_EXECUTION_REJECTED_STR;
	case JOB_EXECUTION_STATUS_NOT_SET:
	case JOB_EXECUTION_UNKNOWN_STATUS:
	default:
		return NULL;
	}
}

#ifdef __cplusplus
}
#endif
