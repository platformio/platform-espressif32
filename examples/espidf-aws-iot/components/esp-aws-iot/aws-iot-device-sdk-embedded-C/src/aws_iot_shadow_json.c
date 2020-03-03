/*
 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
 * @file aws_iot_shadow_json.c
 * @brief Shadow client JSON parsing API definitions
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "aws_iot_shadow_json.h"

#include <string.h>
#include <stdbool.h>

#include "aws_iot_json_utils.h"
#include "aws_iot_log.h"
#include "aws_iot_shadow_key.h"
#include "aws_iot_config.h"

extern char mqttClientID[MAX_SIZE_OF_UNIQUE_CLIENT_ID_BYTES];
#define AWS_IOT_SHADOW_CLIENT_TOKEN_KEY "{\"clientToken\":\""
static uint32_t clientTokenNum = 0;

//helper functions
static IoT_Error_t convertDataToString(char *pStringBuffer, size_t maxSizoStringBuffer, JsonPrimitiveType type,
									   void *pData);

void resetClientTokenSequenceNum(void) {
	clientTokenNum = 0;
}

static IoT_Error_t emptyJsonWithClientToken(char *pBuffer, size_t bufferSize) {

    IoT_Error_t rc = SUCCESS;
    size_t dataLenInBuffer = 0;

	if(pBuffer != NULL)
    {
        dataLenInBuffer = (size_t)snprintf(pBuffer, bufferSize, AWS_IOT_SHADOW_CLIENT_TOKEN_KEY);
    }else
	{
	    IOT_ERROR("NULL buffer in emptyJsonWithClientToken\n");
        rc = FAILURE;
	}

	if(rc == SUCCESS)
	{
	    if ( dataLenInBuffer < bufferSize )
	    {
	        dataLenInBuffer += (size_t)snprintf(pBuffer + dataLenInBuffer, bufferSize - dataLenInBuffer, "%s-%d", mqttClientID, ( int )clientTokenNum++);
	    }
	    else
	    {
	        rc = FAILURE;
	        IOT_ERROR("Supplied buffer too small to create JSON file\n");
	    }
    }

	if(rc == SUCCESS)
	{
	    if ( dataLenInBuffer < bufferSize )
	    {
	        dataLenInBuffer += (size_t)snprintf( pBuffer + dataLenInBuffer, bufferSize - dataLenInBuffer, "\"}" );
	        if ( dataLenInBuffer > bufferSize )
	        {
	            rc = FAILURE;
	            IOT_ERROR( "Supplied buffer too small to create JSON file\n" );
	        }
	    }
	    else
	    {
	        rc = FAILURE;
	        IOT_ERROR( "Supplied buffer too small to create JSON file\n" );
	    }
    }

    FUNC_EXIT_RC(rc);
}

IoT_Error_t aws_iot_shadow_internal_get_request_json(char *pBuffer, size_t bufferSize) {
	return emptyJsonWithClientToken( pBuffer, bufferSize);
}

IoT_Error_t aws_iot_shadow_internal_delete_request_json(char *pBuffer, size_t bufferSize ) {
	return emptyJsonWithClientToken( pBuffer, bufferSize);
}

static inline IoT_Error_t checkReturnValueOfSnPrintf(int32_t snPrintfReturn, size_t maxSizeOfJsonDocument) {
	if(snPrintfReturn < 0) {
		return SHADOW_JSON_ERROR;
	} else if((size_t) snPrintfReturn >= maxSizeOfJsonDocument) {
		return SHADOW_JSON_BUFFER_TRUNCATED;
	}
	return SUCCESS;
}

IoT_Error_t aws_iot_shadow_init_json_document(char *pJsonDocument, size_t maxSizeOfJsonDocument) {

	IoT_Error_t ret_val = SUCCESS;
	int32_t snPrintfReturn = 0;

	if(pJsonDocument == NULL) {
		return NULL_VALUE_ERROR;
	}
	snPrintfReturn = snprintf(pJsonDocument, maxSizeOfJsonDocument, "{\"state\":{");

	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, maxSizeOfJsonDocument);

	return ret_val;

}

IoT_Error_t aws_iot_shadow_add_desired(char *pJsonDocument, size_t maxSizeOfJsonDocument, uint8_t count, ...) {
	IoT_Error_t ret_val = SUCCESS;
	size_t tempSize = 0;
	int8_t i;
	size_t remSizeOfJsonBuffer = maxSizeOfJsonDocument;
	int32_t snPrintfReturn = 0;
	va_list pArgs;
	jsonStruct_t *pTemporary = NULL;
	va_start(pArgs, count);

	if(pJsonDocument == NULL) {
		va_end(pArgs);
		return NULL_VALUE_ERROR;
	}

	tempSize = maxSizeOfJsonDocument - strlen(pJsonDocument);
	if(tempSize <= 1) {
		va_end(pArgs);
		return SHADOW_JSON_ERROR;
	}
	remSizeOfJsonBuffer = tempSize;

	snPrintfReturn = snprintf(pJsonDocument + strlen(pJsonDocument), remSizeOfJsonBuffer, "\"desired\":{");
	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);

	if(ret_val != SUCCESS) {
		va_end(pArgs);
		return ret_val;
	}

	for(i = 0; i < count; i++) {
		tempSize = maxSizeOfJsonDocument - strlen(pJsonDocument);
		if(tempSize <= 1) {
			va_end(pArgs);
			return SHADOW_JSON_ERROR;
		}
		remSizeOfJsonBuffer = tempSize;
		pTemporary = va_arg (pArgs, jsonStruct_t *);
		if(pTemporary != NULL) {
			snPrintfReturn = snprintf(pJsonDocument + strlen(pJsonDocument), remSizeOfJsonBuffer, "\"%s\":",
									  pTemporary->pKey);
			ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);
			if(ret_val != SUCCESS) {
				va_end(pArgs);
				return ret_val;
			}
			if(pTemporary->pKey != NULL && pTemporary->pData != NULL) {
				ret_val = convertDataToString(pJsonDocument + strlen(pJsonDocument), remSizeOfJsonBuffer,
											  pTemporary->type, pTemporary->pData);
			} else {
				va_end(pArgs);
				return NULL_VALUE_ERROR;
			}
			if(ret_val != SUCCESS) {
				va_end(pArgs);
				return ret_val;
			}
		} else {
			va_end(pArgs);
			return NULL_VALUE_ERROR;
		}
	}

	va_end(pArgs);
	snPrintfReturn = snprintf(pJsonDocument + strlen(pJsonDocument) - 1, remSizeOfJsonBuffer, "},");
	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);
	return ret_val;
}

IoT_Error_t aws_iot_shadow_add_reported(char *pJsonDocument, size_t maxSizeOfJsonDocument, uint8_t count, ...) {
	IoT_Error_t ret_val = SUCCESS;

	int8_t i;
	size_t remSizeOfJsonBuffer = maxSizeOfJsonDocument;
	int32_t snPrintfReturn = 0;
	size_t tempSize = 0;
	jsonStruct_t *pTemporary;
	va_list pArgs;
	va_start(pArgs, count);

	if(pJsonDocument == NULL) {
		va_end(pArgs);
		return NULL_VALUE_ERROR;
	}


	tempSize = maxSizeOfJsonDocument - strlen(pJsonDocument);
	if(tempSize <= 1) {
		va_end(pArgs);
		return SHADOW_JSON_ERROR;
	}
	remSizeOfJsonBuffer = tempSize;

	snPrintfReturn = snprintf(pJsonDocument + strlen(pJsonDocument), remSizeOfJsonBuffer, "\"reported\":{");
	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);

	if(ret_val != SUCCESS) {
		va_end(pArgs);
		return ret_val;
	}

	for(i = 0; i < count; i++) {
		tempSize = maxSizeOfJsonDocument - strlen(pJsonDocument);
		if(tempSize <= 1) {
			va_end(pArgs);
			return SHADOW_JSON_ERROR;
		}
		remSizeOfJsonBuffer = tempSize;

		pTemporary = va_arg (pArgs, jsonStruct_t *);
		if(pTemporary != NULL) {
			snPrintfReturn = snprintf(pJsonDocument + strlen(pJsonDocument), remSizeOfJsonBuffer, "\"%s\":",
									  pTemporary->pKey);
			ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);
			if(ret_val != SUCCESS) {
				va_end(pArgs);
				return ret_val;
			}
			if(pTemporary->pKey != NULL && pTemporary->pData != NULL) {
				ret_val = convertDataToString(pJsonDocument + strlen(pJsonDocument), remSizeOfJsonBuffer,
											  pTemporary->type, pTemporary->pData);
			} else {
				va_end(pArgs);
				return NULL_VALUE_ERROR;
			}
			if(ret_val != SUCCESS) {
				va_end(pArgs);
				return ret_val;
			}
		} else {
			va_end(pArgs);
			return NULL_VALUE_ERROR;
		}
	}

	va_end(pArgs);
	snPrintfReturn = snprintf(pJsonDocument + strlen(pJsonDocument) - 1, remSizeOfJsonBuffer, "},");
	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);
	return ret_val;
}


int32_t FillWithClientTokenSize(char *pBufferToBeUpdatedWithClientToken, size_t maxSizeOfJsonDocument) {
	int32_t snPrintfReturn;
	snPrintfReturn = snprintf(pBufferToBeUpdatedWithClientToken, maxSizeOfJsonDocument, "%s-%d", mqttClientID,
				  (int) clientTokenNum++);

	return snPrintfReturn;
}

IoT_Error_t aws_iot_fill_with_client_token(char *pBufferToBeUpdatedWithClientToken, size_t maxSizeOfJsonDocument) {

	int32_t snPrintfRet = 0;
	snPrintfRet = FillWithClientTokenSize(pBufferToBeUpdatedWithClientToken, maxSizeOfJsonDocument);
	return checkReturnValueOfSnPrintf(snPrintfRet, maxSizeOfJsonDocument);

}

IoT_Error_t aws_iot_finalize_json_document(char *pJsonDocument, size_t maxSizeOfJsonDocument) {
	size_t remSizeOfJsonBuffer = maxSizeOfJsonDocument;
	int32_t snPrintfReturn = 0;
	size_t tempSize = 0;
	IoT_Error_t ret_val = SUCCESS;

	if(pJsonDocument == NULL) {
		return NULL_VALUE_ERROR;
	}

	tempSize = maxSizeOfJsonDocument - strlen(pJsonDocument);
	if(tempSize <= 1) {
		return SHADOW_JSON_ERROR;
	}
	remSizeOfJsonBuffer = tempSize;

	// strlen(ShadowTxBuffer) - 1 is to ensure we remove the last ,(comma) that was added
	snPrintfReturn = snprintf(pJsonDocument + strlen(pJsonDocument) - 1, remSizeOfJsonBuffer, "}, \"%s\":\"",
							  SHADOW_CLIENT_TOKEN_STRING);
	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);

	if(ret_val != SUCCESS) {
		return ret_val;
	}
	// refactor this XXX repeated code
	tempSize = maxSizeOfJsonDocument - strlen(pJsonDocument);
	if(tempSize <= 1) {
		return SHADOW_JSON_ERROR;
	}
	remSizeOfJsonBuffer = tempSize;


	snPrintfReturn = FillWithClientTokenSize(pJsonDocument + strlen(pJsonDocument), remSizeOfJsonBuffer);
	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);

	if(ret_val != SUCCESS) {
		return ret_val;
	}
	tempSize = maxSizeOfJsonDocument - strlen(pJsonDocument);
	if(tempSize <= 1) {
		return SHADOW_JSON_ERROR;
	}
	remSizeOfJsonBuffer = tempSize;


	snPrintfReturn = snprintf(pJsonDocument + strlen(pJsonDocument), remSizeOfJsonBuffer, "\"}");
	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, remSizeOfJsonBuffer);

	return ret_val;
}

static IoT_Error_t convertDataToString(char *pStringBuffer, size_t maxSizoStringBuffer, JsonPrimitiveType type,
									   void *pData) {
	int32_t snPrintfReturn = 0;
	IoT_Error_t ret_val = SUCCESS;

	if(maxSizoStringBuffer == 0) {
		return SHADOW_JSON_ERROR;
	}

	if(type == SHADOW_JSON_INT32) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%i,", *(int32_t *) (pData));
	} else if(type == SHADOW_JSON_INT16) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%hi,", *(int16_t *) (pData));
	} else if(type == SHADOW_JSON_INT8) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%hhi,", *(int8_t *) (pData));
	} else if(type == SHADOW_JSON_UINT32) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%u,", *(uint32_t *) (pData));
	} else if(type == SHADOW_JSON_UINT16) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%hu,", *(uint16_t *) (pData));
	} else if(type == SHADOW_JSON_UINT8) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%hhu,", *(uint8_t *) (pData));
	} else if(type == SHADOW_JSON_DOUBLE) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%f,", *(double *) (pData));
	} else if(type == SHADOW_JSON_FLOAT) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%f,", *(float *) (pData));
	} else if(type == SHADOW_JSON_BOOL) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%s,", *(bool *) (pData) ? "true" : "false");
	} else if(type == SHADOW_JSON_STRING) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "\"%s\",", (char *) (pData));
	} else if(type == SHADOW_JSON_OBJECT) {
		snPrintfReturn = snprintf(pStringBuffer, maxSizoStringBuffer, "%s,", (char *) (pData));
	}


	ret_val = checkReturnValueOfSnPrintf(snPrintfReturn, maxSizoStringBuffer);

	return ret_val;
}

static jsmn_parser shadowJsonParser;
static jsmntok_t jsonTokenStruct[MAX_JSON_TOKEN_EXPECTED];

bool isJsonValidAndParse(const char *pJsonDocument, size_t jsonSize, void *pJsonHandler, int32_t *pTokenCount) {
	int32_t tokenCount;

	IOT_UNUSED(pJsonHandler);

	jsmn_init(&shadowJsonParser);

	tokenCount = jsmn_parse(&shadowJsonParser, pJsonDocument, jsonSize, jsonTokenStruct,
							sizeof(jsonTokenStruct) / sizeof(jsonTokenStruct[0]));

	if(tokenCount < 0) {
		IOT_WARN("Failed to parse JSON: %d\n", tokenCount);
		return false;
	}

	/* Assume the top-level element is an object */
	if(tokenCount < 1 || jsonTokenStruct[0].type != JSMN_OBJECT) {
		IOT_WARN("Top Level is not an object\n");
		return false;
	}

	*pTokenCount = tokenCount;

	return true;
}	

static IoT_Error_t UpdateValueIfNoObject(const char *pJsonString, jsonStruct_t *pDataStruct, jsmntok_t token) {
	IoT_Error_t ret_val = SHADOW_JSON_ERROR;
	if(pDataStruct->type == SHADOW_JSON_BOOL && pDataStruct->dataLength >= sizeof(bool)) {
		ret_val = parseBooleanValue((bool *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_INT32 && pDataStruct->dataLength >= sizeof(int32_t)) {
		ret_val = parseInteger32Value((int32_t *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_INT16 && pDataStruct->dataLength >= sizeof(int16_t)) {
		ret_val = parseInteger16Value((int16_t *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_INT8 && pDataStruct->dataLength >= sizeof(int8_t)) {
		ret_val = parseInteger8Value((int8_t *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_UINT32 && pDataStruct->dataLength >= sizeof(uint32_t)) {
		ret_val = parseUnsignedInteger32Value((uint32_t *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_UINT16 && pDataStruct->dataLength >= sizeof(uint16_t)) {
		ret_val = parseUnsignedInteger16Value((uint16_t *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_UINT8 && pDataStruct->dataLength >= sizeof(uint8_t)) {
		ret_val = parseUnsignedInteger8Value((uint8_t *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_FLOAT && pDataStruct->dataLength >= sizeof(float)) {
		ret_val = parseFloatValue((float *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_DOUBLE && pDataStruct->dataLength >= sizeof(double)) {
		ret_val = parseDoubleValue((double *) pDataStruct->pData, pJsonString, &token);
	} else if(pDataStruct->type == SHADOW_JSON_STRING) {
		ret_val = parseStringValue((char *) pDataStruct->pData, pDataStruct->dataLength, pJsonString, &token);
        }

	return ret_val;
}

bool isJsonKeyMatchingAndUpdateValue(const char *pJsonDocument, void *pJsonHandler, int32_t tokenCount,
									 jsonStruct_t *pDataStruct, uint32_t *pDataLength, int32_t *pDataPosition) {
	int32_t i;
	uint32_t dataLength;
	jsmntok_t dataToken;

	IOT_UNUSED(pJsonHandler);

	for(i = 1; i < tokenCount; i++) {
		if(jsoneq(pJsonDocument, &(jsonTokenStruct[i]), pDataStruct->pKey) == 0) {
			dataToken = jsonTokenStruct[i + 1];
			dataLength = (uint32_t) (dataToken.end - dataToken.start);
			UpdateValueIfNoObject(pJsonDocument, pDataStruct, dataToken);
			*pDataPosition = dataToken.start;
			*pDataLength = dataLength;
			return true;
		} else if(jsoneq(pJsonDocument, &(jsonTokenStruct[i]), "metadata") == 0) {
			return false;
		}
	}
	return false;
}

bool isReceivedJsonValid(const char *pJsonDocument, size_t jsonSize ) {
	int32_t tokenCount;

	jsmn_init(&shadowJsonParser);

	tokenCount = jsmn_parse(&shadowJsonParser, pJsonDocument, jsonSize, jsonTokenStruct,
							sizeof(jsonTokenStruct) / sizeof(jsonTokenStruct[0]));

	if(tokenCount < 0) {
		IOT_WARN("Failed to parse JSON: %d\n", tokenCount);
		return false;
	}

	/* Assume the top-level element is an object */
	if(tokenCount < 1 || jsonTokenStruct[0].type != JSMN_OBJECT) {
		return false;
	}

	return true;
}

bool extractClientToken(const char *pJsonDocument, size_t jsonSize, char *pExtractedClientToken, size_t clientTokenSize) {
	int32_t tokenCount, i;
	size_t length;
	jsmntok_t ClientJsonToken;
	jsmn_init(&shadowJsonParser);

	tokenCount = jsmn_parse(&shadowJsonParser, pJsonDocument, jsonSize, jsonTokenStruct,
							sizeof(jsonTokenStruct) / sizeof(jsonTokenStruct[0]));

	if(tokenCount < 0) {
		IOT_WARN("Failed to parse JSON: %d\n", tokenCount);
		return false;
	}

	/* Assume the top-level element is an object */
	if(tokenCount < 1 || jsonTokenStruct[0].type != JSMN_OBJECT) {
		return false;
	}

	for(i = 1; i < tokenCount; i++) {
		if(jsoneq(pJsonDocument, &jsonTokenStruct[i], SHADOW_CLIENT_TOKEN_STRING) == 0) {
			ClientJsonToken = jsonTokenStruct[i + 1];
			length = (uint8_t) (ClientJsonToken.end - ClientJsonToken.start);
            if (clientTokenSize >= length + 1)
            {
                strncpy( pExtractedClientToken, pJsonDocument + ClientJsonToken.start, length);
                pExtractedClientToken[length] = '\0';
                return true;
            }else{
                IOT_WARN( "Token size %zu too small for string %zu \n", clientTokenSize, length);
                return false; 
            }
		}
	}

	return false;
}

bool extractVersionNumber(const char *pJsonDocument, void *pJsonHandler, int32_t tokenCount, uint32_t *pVersionNumber) {
	int32_t i;
	IoT_Error_t ret_val = SUCCESS;

	IOT_UNUSED(pJsonHandler);

	for(i = 1; i < tokenCount; i++) {
		if(jsoneq(pJsonDocument, &(jsonTokenStruct[i]), SHADOW_VERSION_STRING) == 0) {
			ret_val = parseUnsignedInteger32Value(pVersionNumber, pJsonDocument, &jsonTokenStruct[i + 1]);
			if(ret_val == SUCCESS) {
				return true;
			}
		}
	}
	return false;
}

#ifdef __cplusplus
}
#endif

