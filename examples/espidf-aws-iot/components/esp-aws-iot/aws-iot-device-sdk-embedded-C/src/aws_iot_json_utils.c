/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/**
 * @file aws_json_utils.c
 * @brief Utilities for manipulating JSON
 *
 * json_utils provides JSON parsing utilities for use with the IoT SDK.
 * Underlying JSON parsing relies on the Jasmine JSON parser.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "aws_iot_json_utils.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "aws_iot_log.h"

int8_t jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if(tok->type == JSMN_STRING) {
		if((int) strlen(s) == tok->end - tok->start) {
			if(strncmp(json + tok->start, s, (size_t) (tok->end - tok->start)) == 0) {
				return 0;
			}
		}
	}
	return -1;
}

IoT_Error_t parseUnsignedInteger32Value(uint32_t *i, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not an integer");
		return JSON_PARSE_ERROR;
	}

	if(('-' == (char) (jsonString[token->start])) || (1 != sscanf(jsonString + token->start, "%u", i))) {
		IOT_WARN("Token was not an unsigned integer.");
		return JSON_PARSE_ERROR;
	}

	return SUCCESS;
}

IoT_Error_t parseUnsignedInteger16Value(uint16_t *i, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not an integer");
		return JSON_PARSE_ERROR;
	}

	if(('-' == (char) (jsonString[token->start])) || (1 != sscanf(jsonString + token->start, "%hu", i))) {
		IOT_WARN("Token was not an unsigned integer.");
		return JSON_PARSE_ERROR;
	}

	return SUCCESS;
}

IoT_Error_t parseUnsignedInteger8Value(uint8_t *i, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not an integer");
		return JSON_PARSE_ERROR;
	}

	if(('-' == (char) (jsonString[token->start])) || (1 != sscanf(jsonString + token->start, "%hhu", i))) {
		IOT_WARN("Token was not an unsigned integer.");
		return JSON_PARSE_ERROR;
	}

	return SUCCESS;
}

IoT_Error_t parseInteger32Value(int32_t *i, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not an integer");
		return JSON_PARSE_ERROR;
	}

	if(1 != sscanf(jsonString + token->start, "%i", i)) {
		IOT_WARN("Token was not an integer.");
		return JSON_PARSE_ERROR;
	}

	return SUCCESS;
}

IoT_Error_t parseInteger16Value(int16_t *i, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not an integer");
		return JSON_PARSE_ERROR;
	}

	if(1 != sscanf(jsonString + token->start, "%hi", i)) {
		IOT_WARN("Token was not an integer.");
		return JSON_PARSE_ERROR;
	}

	return SUCCESS;
}

IoT_Error_t parseInteger8Value(int8_t *i, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not an integer");
		return JSON_PARSE_ERROR;
	}

	if(1 != sscanf(jsonString + token->start, "%hhi", i)) {
		IOT_WARN("Token was not an integer.");
		return JSON_PARSE_ERROR;
	}

	return SUCCESS;
}

IoT_Error_t parseFloatValue(float *f, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not a float.");
		return JSON_PARSE_ERROR;
	}

	if(1 != sscanf(jsonString + token->start, "%f", f)) {
		IOT_WARN("Token was not a float.");
		return JSON_PARSE_ERROR;
	}

	return SUCCESS;
}

IoT_Error_t parseDoubleValue(double *d, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not a double.");
		return JSON_PARSE_ERROR;
	}

	if(1 != sscanf(jsonString + token->start, "%lf", d)) {
		IOT_WARN("Token was not a double.");
		return JSON_PARSE_ERROR;
	}

	return SUCCESS;
}

IoT_Error_t parseBooleanValue(bool *b, const char *jsonString, jsmntok_t *token) {
	if(token->type != JSMN_PRIMITIVE) {
		IOT_WARN("Token was not a primitive.");
		return JSON_PARSE_ERROR;
	}
	if(strncmp(jsonString + token->start, "true", 4) == 0) {
		*b = true;
	} else if(strncmp(jsonString + token->start, "false", 5) == 0) {
		*b = false;
	} else {
		IOT_WARN("Token was not a bool.");
		return JSON_PARSE_ERROR;
	}
	return SUCCESS;
}

IoT_Error_t parseStringValue(char *buf, size_t bufLen, const char *jsonString, jsmntok_t *token) {
	/* This length does not include a null-terminator. */
	size_t stringLength = (size_t)(token->end - token->start);

	if(token->type != JSMN_STRING) {
		IOT_WARN("Token was not a string.");
		return JSON_PARSE_ERROR;
	}

	if (stringLength+1 > bufLen) {
		IOT_WARN("Buffer too small to hold string value.");
		return SHADOW_JSON_ERROR;
	}

	strncpy(buf, jsonString + token->start, stringLength);
	buf[stringLength] = '\0';

	return SUCCESS;
}

jsmntok_t *findToken(const char *key, const char *jsonString, jsmntok_t *token) {
	jsmntok_t *result = token;
	int i;

	if(token->type != JSMN_OBJECT) {
		IOT_WARN("Token was not an object.");
		return NULL;
	}

	if(token->size == 0) {
		return NULL;
	}

	result = token + 1;

	for (i = 0; i < token->size; i++) {
		if (0 == jsoneq(jsonString, result, key)) {
			return result + 1;
		}

		int propertyEnd = (result + 1)->end;
		result += 2;
		while (result->start < propertyEnd)
			result++;
	}

	return NULL;
}

#ifdef __cplusplus
}
#endif
