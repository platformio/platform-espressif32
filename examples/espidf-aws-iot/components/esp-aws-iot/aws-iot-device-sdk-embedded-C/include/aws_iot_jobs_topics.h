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
 * @file aws_iot_job_topics.h
 * @brief Functions for parsing and creating MQTT topics used by the AWS IoT Jobs system.
 */

#ifdef DISABLE_IOT_JOBS
#error "Jobs API is disabled"
#endif

#ifndef AWS_IOT_JOBS_TOPICS_H_
#define AWS_IOT_JOBS_TOPICS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JOB_ID_NEXT 	"$next"
#define JOB_ID_WILDCARD "+"

/**
 * The type of job topic.
 */
typedef enum {
	JOB_UNRECOGNIZED_TOPIC = 0,
	JOB_GET_PENDING_TOPIC,
	JOB_START_NEXT_TOPIC,
	JOB_DESCRIBE_TOPIC,
	JOB_UPDATE_TOPIC,
	JOB_NOTIFY_TOPIC,
	JOB_NOTIFY_NEXT_TOPIC,
	JOB_WILDCARD_TOPIC
} AwsIotJobExecutionTopicType;

/**
 * The type of reply topic, or #JOB_REQUEST_TYPE for
 * topics that are not replies.
 */
typedef enum {
	JOB_UNRECOGNIZED_TOPIC_TYPE = 0,
	JOB_REQUEST_TYPE,
	JOB_ACCEPTED_REPLY_TYPE,
	JOB_REJECTED_REPLY_TYPE,
	JOB_WILDCARD_REPLY_TYPE
} AwsIotJobExecutionTopicReplyType;

/**
 * @brief Get the topic matching the provided details and put into the provided buffer.
 *
 * If the buffer is not large enough to store the full topic the topic will be truncated
 * to fit, with the last character always being a null terminator.
 *
 * \param buffer the buffer to put the results into
 * \param bufferSize the size of the buffer
 * \param topicType the type of the topic
 * \param replyType the reply type of the topic
 * \param thingName the name of the thing in the topic
 * \param jobId the name of the job id in the topic
 * \return the number of characters in the topic excluding the null terminator. A return
 *   value of bufferSize or more means that the topic string was truncated.
 */
int aws_iot_jobs_get_api_topic(char *buffer, size_t bufferSize,
		AwsIotJobExecutionTopicType topicType, AwsIotJobExecutionTopicReplyType replyType,
		const char* thingName, const char* jobId);

#ifdef __cplusplus
}
#endif

#endif /* AWS_IOT_JOBS_TOPICS_H_ */
