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

#include "aws_iot_jobs_topics.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define BASE_THINGS_TOPIC "$aws/things/"

#define NOTIFY_OPERATION "notify"
#define NOTIFY_NEXT_OPERATION "notify-next"
#define GET_OPERATION "get"
#define START_NEXT_OPERATION "start-next"
#define WILDCARD_OPERATION "+"
#define UPDATE_OPERATION "update"
#define ACCEPTED_REPLY "accepted"
#define REJECTED_REPLY "rejected"
#define WILDCARD_REPLY "+"

static const char *_get_operation_for_base_topic(AwsIotJobExecutionTopicType topicType) {
	switch (topicType) {
	case JOB_UPDATE_TOPIC:
		return UPDATE_OPERATION;
	case JOB_NOTIFY_TOPIC:
		return NOTIFY_OPERATION;
	case JOB_NOTIFY_NEXT_TOPIC:
	    return NOTIFY_NEXT_OPERATION;
	case JOB_GET_PENDING_TOPIC:
	case JOB_DESCRIBE_TOPIC:
		return GET_OPERATION;
	case JOB_START_NEXT_TOPIC:
	    return START_NEXT_OPERATION;
	case JOB_WILDCARD_TOPIC:
		return WILDCARD_OPERATION;
	case JOB_UNRECOGNIZED_TOPIC:
	default:
		return NULL;
	}
}

static bool _base_topic_requires_job_id(AwsIotJobExecutionTopicType topicType) {
	switch (topicType) {
	case JOB_UPDATE_TOPIC:
	case JOB_DESCRIBE_TOPIC:
		return true;
	case JOB_NOTIFY_TOPIC:
	case JOB_NOTIFY_NEXT_TOPIC:
	case JOB_START_NEXT_TOPIC:
	case JOB_GET_PENDING_TOPIC:
	case JOB_WILDCARD_TOPIC:
	case JOB_UNRECOGNIZED_TOPIC:
	default:
		return false;
	}
}

static const char *_get_suffix_for_topic_type(AwsIotJobExecutionTopicReplyType replyType) {
	switch (replyType) {
	case JOB_REQUEST_TYPE:
		return "";
		break;
	case JOB_ACCEPTED_REPLY_TYPE:
		return "/" ACCEPTED_REPLY;
		break;
	case JOB_REJECTED_REPLY_TYPE:
		return "/" REJECTED_REPLY;
		break;
	case JOB_WILDCARD_REPLY_TYPE:
		return "/" WILDCARD_REPLY;
		break;
	case JOB_UNRECOGNIZED_TOPIC_TYPE:
	default:
		return NULL;
	}
}

int aws_iot_jobs_get_api_topic(char *buffer, size_t bufferSize,
		AwsIotJobExecutionTopicType topicType, AwsIotJobExecutionTopicReplyType replyType,
		const char* thingName, const char* jobId)
{
	if (thingName == NULL) {
		return -1;
	}

	if ((topicType == JOB_NOTIFY_TOPIC || topicType == JOB_NOTIFY_NEXT_TOPIC) && replyType != JOB_REQUEST_TYPE) {
		return -1;
	}

	bool requireJobId = _base_topic_requires_job_id(topicType);
	if (jobId == NULL && requireJobId) {
		return -1;
	}

	const char *operation = _get_operation_for_base_topic(topicType);
	if (operation == NULL) {
		return -1;
	}

	const char *suffix = _get_suffix_for_topic_type(replyType);

	if (requireJobId || (topicType == JOB_WILDCARD_TOPIC && jobId != NULL)) {
		return snprintf(buffer, bufferSize, BASE_THINGS_TOPIC "%s/jobs/%s/%s%s", thingName, jobId, operation, suffix);
	} else if (topicType == JOB_WILDCARD_TOPIC) {
		return snprintf(buffer, bufferSize, BASE_THINGS_TOPIC "%s/jobs/#", thingName);
	} else {
		return snprintf(buffer, bufferSize, BASE_THINGS_TOPIC "%s/jobs/%s%s", thingName, operation, suffix);
	}
}

#ifdef __cplusplus
}
#endif
