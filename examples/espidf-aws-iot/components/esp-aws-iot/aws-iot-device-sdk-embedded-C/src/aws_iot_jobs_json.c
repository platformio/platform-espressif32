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

#define __STDC_FORMAT_MACROS
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

#include "jsmn.h"
#include "aws_iot_jobs_json.h"

struct _SerializeState {
	int totalSize;
	char *nextPtr;
	size_t remaingSize;
};

static void _printToBuffer(struct _SerializeState *state, const char *fmt, ...) {
	if (state->totalSize == -1) return;

	va_list vl;
	va_start(vl, fmt);
	int len = vsnprintf(state->nextPtr, state->remaingSize, fmt, vl);
	if (len < 0) {
		state->totalSize = -1;
	} else {
		state->totalSize += len;
		if (state->nextPtr != NULL) {
			if (state->remaingSize > (size_t) len) {
				state->remaingSize -= (size_t) len;
				state->nextPtr += len;
			} else {
				state->remaingSize = 0;
				state->nextPtr = NULL;
			}
		}
	}
	va_end(vl);
}

static void _printKey(struct _SerializeState *state, bool first, const char *key) {
	if (first) {
		_printToBuffer(state, "{\"%s\":", key);
	} else {
		_printToBuffer(state, ",\"%s\":", key);
	}
}

static void _printStringValue(struct _SerializeState *state, const char *value) {
	if (value == NULL) {
		_printToBuffer(state, "null");
	} else {
		_printToBuffer(state, "\"%s\"", value);
	}
}

static void _printLongValue(struct _SerializeState *state, int64_t value) {
	_printToBuffer(state, "%lld", value);
}

static void _printBooleanValue(struct _SerializeState *state, bool value) {
	if(value) {
		_printToBuffer(state, "true");
	} else {
		_printToBuffer(state, "false");
	}
}

int aws_iot_jobs_json_serialize_update_job_execution_request(
		char *requestBuffer, size_t bufferSize,
		const AwsIotJobExecutionUpdateRequest *request)
{
	const char *statusStr = aws_iot_jobs_map_status_to_string(request->status);
	if (statusStr == NULL) return -1;
	if (requestBuffer == NULL) bufferSize = 0;

	struct _SerializeState state = { 0, requestBuffer, bufferSize };
	_printKey(&state, true, "status");
	_printStringValue(&state, statusStr);
	if (request->statusDetails != NULL) {
		_printKey(&state, false, "statusDetails");
		_printToBuffer(&state, "%s", request->statusDetails);
	}
	if (request->executionNumber != 0) {
		_printKey(&state, false, "executionNumber");
		_printLongValue(&state, request->executionNumber);
	}
	if (request->expectedVersion != 0) {
		_printKey(&state, false, "expectedVersion");
		_printLongValue(&state, request->expectedVersion);
	}
	if (request->includeJobExecutionState) {
		_printKey(&state, false, "includeJobExecutionState");
		_printBooleanValue(&state, request->includeJobExecutionState);
	}
	if (request->includeJobDocument) {
		_printKey(&state, false, "includeJobDocument");
		_printBooleanValue(&state, request->includeJobDocument);
	}
	if (request->clientToken != NULL) {
		_printKey(&state, false, "clientToken");
		_printStringValue(&state, request->clientToken);
	}

	_printToBuffer(&state, "}");

	return state.totalSize;
}

int aws_iot_jobs_json_serialize_client_token_only_request(
		char *requestBuffer, size_t bufferSize,
		const char *clientToken)
{
	struct _SerializeState state = { 0, requestBuffer, bufferSize };
	_printKey(&state, true, "clientToken");
	_printStringValue(&state, clientToken);
	_printToBuffer(&state, "}");

	return state.totalSize;
}

int aws_iot_jobs_json_serialize_describe_job_execution_request(
		char *requestBuffer, size_t bufferSize,
		const AwsIotDescribeJobExecutionRequest *request)
{
	bool first = true;

	if (requestBuffer == NULL) return 0;

	struct _SerializeState state = { 0, requestBuffer, bufferSize };
	if (request->clientToken != NULL) {
		_printKey(&state, first, "clientToken");
		_printStringValue(&state, request->clientToken);
		first = false;
	}
	if (request->executionNumber != 0) {
		_printKey(&state, first, "executionNumber");
		_printLongValue(&state, request->executionNumber);
		first = false;
	}
	if (request->includeJobDocument) {
		_printKey(&state, first, "includeJobDocument");
		_printBooleanValue(&state, request->includeJobDocument);
	}

	_printToBuffer(&state, "}");

	return state.totalSize;
}

int aws_iot_jobs_json_serialize_start_next_job_execution_request(
		char *requestBuffer, size_t bufferSize,
		const AwsIotStartNextPendingJobExecutionRequest *request)
{
	if (requestBuffer == NULL) bufferSize = 0;
	struct _SerializeState state = { 0, requestBuffer, bufferSize };
	if (request->statusDetails != NULL) {
		_printKey(&state, true, "statusDetails");
		_printToBuffer(&state, "%s", request->statusDetails);
	}
	if (request->clientToken != NULL) {
		if(request->statusDetails != NULL) {
			_printKey(&state, false, "clientToken");
		} else {
			_printKey(&state, true, "clientToken");
		}
		_printStringValue(&state, request->clientToken);
	}
	if (request->clientToken == NULL && request->statusDetails == NULL) {
		_printToBuffer(&state, "{");
	}
	_printToBuffer(&state, "}");
	return state.totalSize;
}

#ifdef __cplusplus
}
#endif
