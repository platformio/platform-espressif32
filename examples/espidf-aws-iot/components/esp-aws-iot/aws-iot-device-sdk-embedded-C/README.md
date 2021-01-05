**We have released version 4.0.0 beta 1 of this SDK on the [v4_beta](https://github.com/aws/aws-iot-device-sdk-embedded-C/tree/v4_beta) branch and encourage everyone to give it a try.**

Version 4 is a new design, and therefore **NOT** backwards compatible with version 3.0.1. We will continue to fix bugs in v3.0.1 even after v4.0.0 is released, but we may not add new features to v3.0.1.

Please be aware that v4 beta may have bugs and performance issues. Additionally, there are currently missing features compared to v3.0.1. See the [README](https://github.com/aws/aws-iot-device-sdk-embedded-C/blob/v4_beta/README.md/) on the v4_beta branch for more information.

## Branches

### Master branch
The [master](https://github.com/aws/aws-iot-device-sdk-embedded-C/tree/master) branch will now contain bug fixes/features that have been minimally tested to ensure nothing major is broken. The current version on the master branch is v3.0.1. Eventually, we will move v4.0.0 to the master branch and move v3.0.1 to a legacy branch.

### Release branch
The [release](https://github.com/aws/aws-iot-device-sdk-embedded-C/tree/release) branch will contain new releases for the SDK that have been tested thoroughly on all supported platforms. Please ensure that you are tracking the release branch for all production work. The current version on the release branch is v3.0.1. Eventually, we will move v4.0.0 to the release branch and move v3.0.1 to a legacy branch.

### v4_beta branch
The [v4_beta](https://github.com/aws/aws-iot-device-sdk-embedded-C/tree/v4_beta) branch will contain new features and a new design that inherits from both the AWS IoT Device SDK Embedded C and the libraries provided with Amazon FreeRTOS. This is version 4.0.0 of the SDK. Please be aware that v4 beta may have bugs and performance issues. Eventually, we will move v4.0.0 to the master/release branches and delete v4 beta branch.

### Development branch

The [development](https://github.com/aws/aws-iot-device-sdk-embedded-C/tree/development) currently hosts development of the next iteration of the AWS IoT Embedded C SDK version 4. It is currently a work in progress and should not be used to create any products.  We will update this README when that status changes.

## Overview

The AWS IoT device SDK for embedded C is a collection of C source files which can be used in embedded applications to securely connect to the [AWS IoT platform](http://docs.aws.amazon.com/iot/latest/developerguide/what-is-aws-iot.html). It includes transport clients **MQTT**, **TLS** implementations and examples for their use. It also supports AWS IoT specific features such as **Thing Shadow**. It is distributed in source form and intended to be built into customer firmware along with application code, other libraries and RTOS. For additional information about porting the Device SDK for embedded C onto additional platforms please refer to the [PortingGuide](https://github.com/aws/aws-iot-device-sdk-embedded-c/blob/master/PortingGuide.md/).

## Features
The Device SDK simplifies access to the Pub/Sub functionality of the AWS IoT broker via MQTT and provide APIs to interact with Thing Shadows. The SDK has been tested to work with the AWS IoT platform to ensure best interoperability of a device with the AWS IoT platform.

### MQTT Connection
The Device SDK provides functionality to create and maintain a mutually authenticated TLS connection over which it runs MQTT. This connection is used for any further publish operations and allow for subscribing to MQTT topics which will call a configurable callback function when these topics are received.

### Thing Shadow
The Device SDK implements the specific protocol for Thing Shadows to retrieve, update and delete Thing Shadows adhering to the protocol that is implemented to ensure correct versioning and support for client tokens. It abstracts the necessary MQTT topic subscriptions by automatically subscribing to and unsubscribing from the reserved topics as needed for each API call. Inbound state change requests are automatically signalled via a configurable callback.

### Jobs
The Device SDK implements features to facilitate use of the AWS Jobs service. The Jobs service can be used for device management tasks such as updating program files, rotating device certificates, or running other maintenance tasks such are restoring device settings or restarting devices.

## Design Goals of this SDK
The embedded C SDK was specifically designed for resource constrained devices (running on micro-controllers and RTOS).

Primary aspects are:
 * Flexibility in picking and choosing functionality (reduce memory footprint)
 * Static memory only (no malloc’s)
 * Configurable resource usage(JSON tokens, MQTT subscription handlers, etc…)
 * Can be ported to a different RTOS, uses wrappers for OS specific functions

For more information on the Architecture of the SDK refer [here](http://aws-iot-device-sdk-embedded-c-docs.s3-website-us-east-1.amazonaws.com/index.html)

## Collection of Metrics
Beginning with Release v2.2.0 of the SDK, AWS collects usage metrics indicating which language and version of the SDK is being used. This allows us to prioritize our resources towards addressing issues faster in SDKs that see the most and is an important data point. However, we do understand that not all customers would want to report this data by default. In that case, the sending of usage metrics can be easily disabled by the user by setting the `DISABLE_METRICS` flag to true in the
`aws_iot_config.h` for each application.

## How to get started ?
Ensure you understand the AWS IoT platform and create the necessary certificates and policies. For more information on the AWS IoT platform please visit the [AWS IoT developer guide](http://docs.aws.amazon.com/iot/latest/developerguide/iot-security-identity.html).

In order to quickly get started with the AWS IoT platform, we have ported the SDK for POSIX type Operating Systems like Ubuntu, OS X and RHEL. The SDK is configured for the mbedTLS library and can be built out of the box with *GCC* using *make utility*. You'll need to download mbedTLS from the official ARMmbed repository. We recommend that you pick the latest version of 2.16 LTS release in order to have up-to-date security fixes.

## Installation
This section explains the individual steps to retrieve the necessary files and be able to build your first application using the AWS IoT device SDK for embedded C.

Steps:

 * Create a directory to hold your application e.g. (/home/*user*/aws_iot/my_app)
 * Change directory to this new directory
 * Download the SDK to device and place in the newly created directory
 * Expand the tarball (tar -xf <tarball.tar>).  This will create the below directories:
    * `certs` - TLS certificates directory
    * `docs` - SDK API and file documentation. This folder is not present on GitHub. You can access the documentation [here](http://aws-iot-device-sdk-embedded-c-docs.s3-website-us-east-1.amazonaws.com/index.html)
    * `external_libs` - The mbedTLS and jsmn source files
    * `include` - The AWS IoT SDK header files
    * `platform` - Platform specific files for timer, TLS and threading layers
    * `samples` - The sample applications
    * `src` - The AWS IoT SDK source files
    * `tests` - Contains tests for verifying that the SDK is functioning as expected. More information can be found [here](https://github.com/aws/aws-iot-device-sdk-embedded-c/blob/master/tests/README.md/)
 * View further information on how to use the SDK in the README file for samples that can be found [here](https://github.com/aws/aws-iot-device-sdk-embedded-c/blob/master/samples/README.md/)

Also, for a guided example on getting started with the Thing Shadow, visit the AWS IoT Console's [Interactive Guide](https://console.aws.amazon.com/iot).

## Porting to different platforms
As Embedded devices run on different real-time operating systems and TCP/IP stacks, it is one of the important design goals for the Device SDK to keep it portable. Please refer to the [porting guide](https://github.com/aws/aws-iot-device-sdk-embedded-C/blob/master/PortingGuide.md/) to get more information on how to make this SDK run on your devices (i.e. microcontrollers).

## Migrating from 1.x to 2.x
The 2.x branch makes several changes to the SDK. This section provides information on what changes will be required in the client application for migrating from v1.x to 2.x.

 * The first change is in the folder structure. Client applications using the SDK now need to keep only the certs, external_libs, include, src and platform folder in their application. The folder descriptions can be found above
 * All the SDK headers are in the `include` folder. These need to be added to the makefile as include directories
 * The source files are in the `src` folder. These need to be added to the makefile as one of the source directories
 * Similar to 1.x, the platform folder contains the platform specific headers and source files. These need to be added to the makefile as well
 * The `platform/threading` folder only needs to be added if multi-threading is required, and the `_ENABLE_THREAD_SUPPORT_` macro is defined in config
 * The list below provides a mapping for migrating from the major APIs used in 1.x to the new APIs:

    | Description | 1.x | 2.x |
    | :---------- | :-- | :-- |
    | Initializing the client | ```void aws_iot_mqtt_init(MQTTClient_t *pClient);``` | ```IoT_Error_t aws_iot_mqtt_init(AWS_IoT_Client *pClient, IoT_Client_Init_Params *pInitParams);``` |
    | Connect | ```IoT_Error_t aws_iot_mqtt_connect(MQTTConnectParams *pParams);``` | ```IoT_Error_t aws_iot_mqtt_connect(AWS_IoT_Client *pClient, IoT_Client_Connect_Params *pConnectParams);``` |
    | Subscribe | ```IoT_Error_t aws_iot_mqtt_subscribe(MQTTSubscribeParams *pParams);``` | ```IoT_Error_t aws_iot_mqtt_subscribe(AWS_IoT_Client *pClient, const char *pTopicName, uint16_t topicNameLen, QoS qos, pApplicationHandler_t pApplicationHandler, void *pApplicationHandlerData);``` |
    | Unsubscribe | ```IoT_Error_t aws_iot_mqtt_unsubscribe(char *pTopic);``` | ```IoT_Error_t aws_iot_mqtt_unsubscribe(AWS_IoT_Client *pClient, const char *pTopicFilter, uint16_t topicFilterLen);``` |
    | Yield | ```IoT_Error_t aws_iot_mqtt_yield(int timeout);``` | ```IoT_Error_t aws_iot_mqtt_yield(AWS_IoT_Client *pClient, uint32_t timeout_ms);``` |
    | Publish | ```IoT_Error_t aws_iot_mqtt_publish(MQTTPublishParams *pParams);``` | ```IoT_Error_t aws_iot_mqtt_publish(AWS_IoT_Client *pClient, const char *pTopicName, uint16_t topicNameLen, IoT_Publish_Message_Params *pParams);``` |
    | Disconnect | ```IoT_Error_t aws_iot_mqtt_disconnect(void);``` | ```IoT_Error_t aws_iot_mqtt_disconnect(AWS_IoT_Client *pClient);``` |

You can find more information on how to use the new APIs in the README file for samples that can be found [here](https://github.com/aws/aws-iot-device-sdk-embedded-c/blob/master/samples/README.md/)

## Migrating from 2.x to 3.x
AWS IoT Device SDK for Embedded C v3.0.0 fixes two bugs (see #152 and #155) that create a potential buffer overflows. This version is not backward compatible with previous versions, so users will need to recompile their applications with the new version.

Users of AWS IoT Device Shadows or Json utility functions such as `extractClientToken`, `emptyJsonWithClientToken`, `isJsonValidAndParse` and `isReceivedJsonValid` are encouraged to upgrade to version v3.0.0. For users who cannot upgrade, review all parts of your solution where user input can be sent to the device, and ensure sufficient authorization of these operations is enforced.

Details of the required changes to public functions and data structures are shown below:

### Changes in the `jsonStruct` data structure:
The member `dataLength` has been added to struct `jsonStruct`, which is declared in [include/aws_iot_shadow_json_data.h](include/aws_iot_shadow_json_data.h#L60).

```c
struct jsonStruct {
    const char *         pKey;
    void *               pData;
    size_t               dataLength;
    JsonPrimitiveType    type;
    JsonStructCallback_t cb;
};
```

The size of the buffer `pData` must now be specified by `dataLength`. **Failure to do so may result in undefined behavior**. Below are examples of the code changes required to use the new jsonStruct.

With a primitive data type, such as `int32_t`:

```c
…
jsonStruct_t exampleJsonStruct;
int32_t value = 0L;

/* Set the members of exampleJsonStruct. */
exampleJsonStruct.pKey = “exampleKey”;
exampleJsonStruct.pData = &value;
exampleJsonStruct.type = SHADOW_JSON_INT32;
exampleJsonStruct.cb = exampleCallback;

/* Register a delta callback using example JsonStruct. */
aws_iot_shadow_register_delta(&mqttClient, &exampleJsonStruct);
…
```

Version 3.0.0 will require the following code:

```c
…
jsonStruct_t exampleJsonStruct;
int32_t value = 0L;

/* Set the members of exampleJsonStruct. */
exampleJsonStruct.pKey = “exampleKey”;
exampleJsonStruct.pData = &value;
exampleJsonStruct.dataLength = sizeof(int32_t); /* sizeof(value) also OK.*/
exampleJsonStruct.type = SHADOW_JSON_INT32;
exampleJsonStruct.cb = exampleCallback;

/* Register a delta callback using example JsonStruct. */
aws_iot_shadow_register_delta(&mqttClient, &exampleJsonStruct);
…
```

With a string, versions up to v2.3.0 would require the following code:

```c
…
jsonStruct_t exampleJsonStruct;
char stringBuffer[SIZE_OF_BUFFER];
/* Set the members of exampleJsonStruct. */
exampleJsonStruct.pKey = “exampleKey”;
exampleJsonStruct.pData = stringBuffer;
exampleJsonStruct.type = SHADOW_JSON_STRING;
exampleJsonStruct.cb = exampleCallback;
/* Register a delta callback using example JsonStruct. */
aws_iot_shadow_register_delta(&mqttClient, &exampleJsonStruct);
…
```

Version 3.0.0 will require the following code:

```c
…
jsonStruct_t exampleJsonStruct;
char stringBuffer[SIZE_OF_BUFFER];
/* Set the members of exampleJsonStruct. */
exampleJsonStruct.pKey = “exampleKey”;
exampleJsonStruct.pData = stringBuffer;
exampleJsonStruct.dataLength = SIZE_OF_BUFFER;
exampleJsonStruct.type = SHADOW_JSON_STRING;
exampleJsonStruct.cb = exampleCallback;
/* Register a delta callback using example JsonStruct. */
aws_iot_shadow_register_delta(&mqttClient, &exampleJsonStruct);
…
```

### Changes in parseStringValue:
The function `parseStringValue`, declared in [include/aws_iot_json_utils.h](include/aws_iot_json_utils.h#L179) and implemented in [src/aws_iot_json_utils.c](src/aws_iot_json_utils.c#L184), now requires the inclusion of a buffer length. Its new function signature is:

```c
IoT_Error_t parseStringValue(char *buf, size_t bufLen, const char *jsonString, jsmntok_t *token);
```

Below is an example of code changes required to use the new parseStringValue.

With up to version v2.3.0:

```c
…
char* jsonString = “…”;
jsmntok_t jsmnTokens[NUMBER_OF_JSMN_TOKENS];
char stringBuffer[SIZE_OF_BUFFER];
parseStringValue(stringBuffer, jsonString, jsmnTokens);
…
```

Version 3.0.0 will require the following code:

```c
…
char* jsonString = “…”;
jsmntok_t jsmnTokens[NUMBER_OF_JSMN_TOKENS];
char stringBuffer[SIZE_OF_BUFFER];
parseStringValue(stringBuffer, SIZE_OF_BUFFER, jsonString, jsmnTokens);
…
```

### Changes to functions intended for internal usage:
Version 3.0.0 changes the signature of four functions intended for internal usage. The new signatures explicitly carry the information for the size of the buffer or JSON document passed as a parameter to the functions. Users of the SDK may need to change their code and recompile to ingest the changes. We report the old and new signatures below.

#### Old signatures:

```c
bool extractClientToken(const char *pJsonDocument, char *pExtractedClientToken);

static void emptyJsonWithClientToken(char *pBuffer);

bool isJsonValidAndParse(const char *pJsonDocument, void *pJsonHandler, int32_t *pTokenCount);

bool isReceivedJsonValid(const char *pJsonDocument);
```

#### New signatures:

```c
bool extractClientToken(const char *pJsonDocument, size_t jsonSize, char *pExtractedClientToken, size_t clientTokenSize);

static void emptyJsonWithClientToken(char *pBuffer, size_t bufferSize);

bool isJsonValidAndParse(const char *pJsonDocument, size_t jsonSize, void *pJsonHandler, int32_t *pTokenCount);

bool isReceivedJsonValid(const char *pJsonDocument, size_t jsonSize);
```

## Resources
[API Documentation](http://aws-iot-device-sdk-embedded-c-docs.s3-website-us-east-1.amazonaws.com/index.html)

[MQTT 3.1.1 Spec](http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/csprd02/mqtt-v3.1.1-csprd02.html)

## Support
If you have any technical questions about AWS IoT Device SDK, use the [AWS IoT forum](https://forums.aws.amazon.com/forum.jspa?forumID=210).
For any other questions on AWS IoT, contact [AWS Support](https://aws.amazon.com/contact-us/).

## Sample APIs
Connecting to the AWS IoT MQTT platform

```
AWS_IoT_Client client;
rc = aws_iot_mqtt_init(&client, &iotInitParams);
rc = aws_iot_mqtt_connect(&client, &iotConnectParams);
```


Subscribe to a topic

```
AWS_IoT_Client client;
rc = aws_iot_mqtt_subscribe(&client, "sdkTest/sub", 11, QOS0, iot_subscribe_callback_handler, NULL);
```


Update Thing Shadow from a device

```
rc = aws_iot_shadow_update(&mqttClient, AWS_IOT_MY_THING_NAME, pJsonDocumentBuffer, ShadowUpdateStatusCallback,
                            pCallbackContext, TIMEOUT_4SEC, persistenSubscription);
```
