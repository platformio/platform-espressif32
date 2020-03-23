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
 * @file aws_iot_jobs_json.h
 * @brief Functions for mapping between json and the AWS Iot Job data structures.
 */

#ifdef DISABLE_IOT_JOBS
#error "Jobs API is disabled"
#endif

#ifndef AWS_IOT_JOBS_JSON_H_
#define AWS_IOT_JOBS_JSON_H_

#include <stdbool.h>
#include "jsmn.h"
#include "aws_iot_jobs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Serialize a job execution update request into a json string.
 *
 * \param requestBuffer buffer to contain the serialized request. If null
 *   this function will return the size of the buffer required
 * \param bufferSize the size of the buffer. If this is smaller than the required
 *   length the string will be truncated to fit.
 * \request the request to serialize.
 * \return The size of the json string to store the serialized request or -1
 *   if the request is invalid. Note that the return value should be checked against
 *   the size of the buffer and if its larger handle the fact that the string has
 *   been truncated.
 */
int aws_iot_jobs_json_serialize_update_job_execution_request(
		char *requestBuffer, size_t bufferSize,
		const AwsIotJobExecutionUpdateRequest *request);

/**
 * Serialize a job API request that contains only a client token.
 *
 * \param requestBuffer buffer to contain the serialized request. If null
 *   this function will return the size of the buffer required
 * \param bufferSize the size of the buffer. If this is smaller than the required
 *   length the string will be truncated to fit.
 * \param clientToken the client token to use for the request.
 * \return The size of the json string to store the serialized request or -1
 *   if the request is invalid. Note that the return value should be checked against
 *   the size of the buffer and if its larger handle the fact that the string has
 *   been truncated.
 */
int aws_iot_jobs_json_serialize_client_token_only_request(
		char *requestBuffer, size_t bufferSize,
		const char *clientToken);

/**
 * Serialize describe job execution request into json string.
 *
 * \param requestBuffer buffer to contain the serialized request. If null
 *   this function will return the size of the buffer required
 * \param bufferSize the size of the buffer. If this is smaller than the required
 *   length the string will be truncated to fit.
 * \param request the request to serialize.
 * \return The size of the json string to store the serialized request or -1
 *   if the request is invalid. Note that the return value should be checked against
 *   the size of the buffer and if its larger handle the fact that the string has
 *   been truncated.
 */
int aws_iot_jobs_json_serialize_describe_job_execution_request(
		char *requestBuffer, size_t bufferSize,
		const AwsIotDescribeJobExecutionRequest *request);

/**
 * Serialize start next job execution request into json string.
 *
 * \param requestBuffer buffer to contain the serialized request. If null
 *   this function will return the size of the buffer required
 * \param bufferSize the size of the buffer. If this is smaller than the required
 *   length the string will be truncated to fit.
 * \param request the start-next request to serialize.
 * \return The size of the json string to store the serialized request or -1
 *   if the request is invalid. Note that the return value should be checked against
 *   the size of the buffer and if its larger handle the fact that the string has
 *   been truncated.
 */
int aws_iot_jobs_json_serialize_start_next_job_execution_request(
		char *requestBuffer, size_t bufferSize,
		const AwsIotStartNextPendingJobExecutionRequest *request);

#ifdef __cplusplus
}
#endif

#endif /* AWS_IOT_JOBS_JSON_H_ */
