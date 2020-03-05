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
 * @file aws_iot_jobs_interface.h
 * @brief An interface for interacting with the AWS IoT Jobs system.
 *
 * This file defines utility functions for interacting with the AWS IoT jobs
 * APIs over MQTT. It provides functions for managing subscriptions to job
 * related topics and for sending queries and updates requests for jobs.
 * Callers are responsible for managing the subscriptions and associating
 * responses with the queries and update messages.
 */
#ifndef AWS_IOT_JOBS_INTERFACE_H_
#define AWS_IOT_JOBS_INTERFACE_H_

#ifdef DISABLE_IOT_JOBS
#error "Jobs API is disabled"
#endif

/**
 * @file aws_iot_jobs_interface.h
 * @brief Functions for interacting with the AWS IoT Jobs system.
 */
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_jobs_topics.h"
#include "aws_iot_jobs_types.h"
#include "aws_iot_error.h"
#include "aws_iot_json_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Subscribe to jobs messages for the given thing and/or jobs.
 *
 * The function can be used to subscribe to all job related messages. To subscribe
 * to messages not related to a job the jobId passed should be null. The jobId
 * can also be "+" to subscribe to messages related to any job, or "$next" to
 * indicate the next pending job.
 *
 * See also #aws_iot_jobs_subscribe_to_all_job_messages to subscribe to all possible
 * messages in one operation.
 *
 * \note Subscribing with the same thing, job and topic type more than
 *   once is undefined.
 *
 * \param pClient the client to use
 * \param qos the qos to use
 * \param thingName the name of the thing to subscribe to
 * \param jobId the job id to subscribe to. To subscribe to messages not related to 
 *   a job the jobId passed should be null. The jobId can also be "+" to subscribe to 
 *   messages related to any job, or "$next" to indicate the next pending job.
 * \param topicType the topic type to subscribe to
 * \param replyType the reply topic type to subscribe to
 * \param pApplicationHandler the callback handler
 * \param pApplicationHandlerData the callback context data. This must remain valid at least until
 *   aws_iot_jobs_unsubscribe_from_job_messages is called.
 * \param topicBuffer. A buffer to use to hold the topic name for the subscription. This buffer
 *   must remain valid at least until aws_iot_jobs_unsubscribe_from_job_messages is called.
 * \param topicBufferSize the size of the topic buffer. The function will fail
 *   with LIMIT_EXCEEDED_ERROR if this is too small.
 * \return the result of subscribing to the topic (see aws_iot_mqtt_subscribe)
 */
IoT_Error_t aws_iot_jobs_subscribe_to_job_messages(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const char *jobId,
		AwsIotJobExecutionTopicType topicType,
		AwsIotJobExecutionTopicReplyType replyType,
		pApplicationHandler_t pApplicationHandler,
		void *pApplicationHandlerData,
		char *topicBuffer,
		uint16_t topicBufferSize);

/**
 * @brief Subscribe to all job messages.
 *
 * Subscribe to all job messages for the given thing.
 *
 * \note Subscribing with the same thing more than once is undefined.
 *
 * \param pClient the client to use
 * \param qos the qos to use
 * \param thingName the name of the thing to subscribe to
 * \param pApplicationHandler the callback handler
 * \param pApplicationHandlerData the callback context data. This must remain valid at least until
 *   aws_iot_jobs_unsubscribe_from_job_messages is called.
 * \param topicBuffer. A buffer to use to hold the topic name for the subscription. This buffer
 *   must remain valid at least until aws_iot_jobs_unsubscribe_from_job_messages is called.
 * \param topicBufferSize the size of the topic buffer. The function will fail
 *   with LIMIT_EXCEEDED_ERROR if this is too small.
 * \return the result of subscribing to the topic (see aws_iot_mqtt_subscribe)
 */
IoT_Error_t aws_iot_jobs_subscribe_to_all_job_messages(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		pApplicationHandler_t pApplicationHandler,
		void *pApplicationHandlerData,
		char *topicBuffer,
		uint16_t topicBufferSize);

/**
 * @brief Unsubscribe from a job subscription
 *
 * Remove the subscription created using #aws_iot_jobs_subscribe_to_job_messages or
 *   #aws_iot_jobs_subscribe_to_all_job_messages.
 *
 * \param pClient the client to use
 * \param topicBuffer the topic buffer passed to #aws_iot_jobs_subscribe_to_job_messages or 
 *   #aws_iot_jobs_subscribe_to_all_job_messages when the subscription was created.
 * \return #SUCCESS or the first error encountered.
 */
IoT_Error_t aws_iot_jobs_unsubscribe_from_job_messages(
		AWS_IoT_Client *pClient,
		char *topicBuffer);

/**
 * @brief Send a query to one of the job query APIs.
 *
 * Send a query to one of the job query APIs. If jobId is null this
 * requests a list of pending jobs for the thing. If jobId is
 * not null then it sends a query for the details of that job.
 * If jobId is $next then it sends a query for the details for
 * the next pending job.
 *
 * \param pClient the client to use
 * \param qos the qos to use
 * \param thingName the thing name to query for
 * \param jobId the id of the job to query for. If null a list
 *   of all pending jobs for the thing is requested.
 * \param clientToken the client token to use for the query.
 *   If null no clientToken is sent resulting in an empty message.
 * \param topicBuffer the topic buffer to use. This may be discarded
 *   as soon as this function returns
 * \param topicBufferSize the size of topicBuffer
 * \param messageBuffer the message buffer to use. May be NULL
 *   if clientToken is NULL
 * \param messageBufferSize the size of messageBuffer
 * \param topicType the topic type to publish query to
 * \return LIMIT_EXCEEDED_ERROR if the topic buffer or
 *   message buffer is too small, NULL_VALUE_ERROR if the any of
 *   the required inputs are NULL, otherwise the result
 *   of the mqtt publish
 */
IoT_Error_t aws_iot_jobs_send_query(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const char *jobId,
		const char *clientToken,
		char *topicBuffer,
		uint16_t topicBufferSize,
		char *messageBuffer,
		size_t messageBufferSize,
		AwsIotJobExecutionTopicType topicType);

/**
 * @brief Send a start next command to the job start-next API.
 *
 * Send a start next command to the job start-next API.
 *
 * \param pClient the client to use
 * \param qos the qos to use
 * \param thingName the thing name to query for
 * \param startNextRequest the start-next request to send
 * \param topicBuffer the topic buffer to use. This may be discarded
 *   as soon as this function returns
 * \param topicBufferSize the size of topicBuffer
 * \param messageBuffer the message buffer to use. May be NULL
 *   if clientToken is NULL
 * \param messageBufferSize the size of messageBuffer
 * \return LIMIT_EXCEEDED_ERROR if the topic buffer or
 *   message buffer is too small, NULL_VALUE_ERROR if the any of
 *   the required inputs are NULL, otherwise the result
 *   of the mqtt publish
 */
IoT_Error_t aws_iot_jobs_start_next(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const AwsIotStartNextPendingJobExecutionRequest *startNextRequest,
		char *topicBuffer,
		uint16_t topicBufferSize,
		char *messageBuffer,
		size_t messageBufferSize);

/**
 * @brief Send a describe job query to the job query API.
 *
 * Send a describe job query to the job query API. If jobId is null this
 * requests a list of pending jobs for the thing. If jobId is
 * not null then it sends a query for the details of that job.
 *
 * \param pClient the client to use
 * \param qos the qos to use
 * \param thingName the thing name to query for
 * \param jobId the id of the job to query for. If null a list
 *   of all pending jobs for the thing is requested.
 * \param describeRequest the describe request to send
 * \param topicBuffer the topic buffer to use. This may be discarded
 *   as soon as this function returns
 * \param topicBufferSize the size of topicBuffer
 * \param messageBuffer the message buffer to use. May be NULL
 *   if clientToken is NULL
 * \param messageBufferSize the size of messageBuffer
 * \return LIMIT_EXCEEDED_ERROR if the topic buffer or
 *   message buffer is too small, NULL_VALUE_ERROR if the any of
 *   the required inputs are NULL, otherwise the result
 *   of the mqtt publish
 */
IoT_Error_t aws_iot_jobs_describe(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const char *jobId,
		const AwsIotDescribeJobExecutionRequest *describeRequest,
		char *topicBuffer,
		uint16_t topicBufferSize,
		char *messageBuffer,
		size_t messageBufferSize);

/**
 * @brief Send an update about a job execution.
 *
 * Send an update about a job execution.
 *
 * \param pClient the client to use
 * \param qos the qos to use
 * \param thingName the thing name to send the update for
 * \param jobId the id of the job to send the update for
 * \param updateRequest the update request to send
 * \param topicBuffer the topic buffer to use. This may be discarded
 *   as soon as this function returns
 * \param topicBufferSize the size of topicBuffer
 * \param messageBuffer the message buffer to use.
 * \param messageBufferSize the size of messageBuffer
 * \return LIMIT_EXCEEDED_ERROR if the topic buffer or
 *   message buffer is too small, NULL_VALUE_ERROR if the any of
 *   the required inputs are NULL, otherwise the result
 *   of the mqtt publish
 */
IoT_Error_t aws_iot_jobs_send_update(
		AWS_IoT_Client *pClient, QoS qos,
		const char *thingName,
		const char *jobId,
		const AwsIotJobExecutionUpdateRequest *updateRequest,
		char *topicBuffer,
		uint16_t topicBufferSize,
		char *messageBuffer,
		size_t messageBufferSize);

#ifdef __cplusplus
}
#endif

#endif /* AWS_IOT_JOBS_INTERFACE_H_ */
