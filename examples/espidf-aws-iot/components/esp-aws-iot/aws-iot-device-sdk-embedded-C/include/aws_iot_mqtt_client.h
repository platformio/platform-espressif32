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

// Based on Eclipse Paho.
/*******************************************************************************
 * Copyright (c) 2014 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *    Xiang Rong - 442039 Add makefile to Embedded C client
 *******************************************************************************/

/**
 * @file aws_iot_mqtt_client.h
 * @brief Client definition for MQTT
 */

#ifndef AWS_IOT_SDK_SRC_IOT_MQTT_CLIENT_H
#define AWS_IOT_SDK_SRC_IOT_MQTT_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Library Header files */
#include "stdio.h"
#include "stdbool.h"
#include "stdint.h"
#include "stddef.h"

/* AWS Specific header files */
#include "aws_iot_error.h"
#include "aws_iot_config.h"

/* Platform specific implementation header files */
#include "network_interface.h"
#include "timer_interface.h"

#ifdef _ENABLE_THREAD_SUPPORT_
#include "threads_interface.h"
#endif

/** Greatest packet identifier, per MQTT spec */
#define MAX_PACKET_ID 65535

typedef struct _Client AWS_IoT_Client;

/**
 * @brief Quality of Service Type
 *
 * Defining a QoS type.
 * @note QoS 2 is \b NOT supported by the AWS IoT Service at the time of this SDK release.
 *
 */
typedef enum QoS {
	QOS0 = 0,
	QOS1 = 1
} QoS;

/**
 * @brief Publish Message Parameters Type
 *
 * Defines a type for MQTT Publish messages. Used for both incoming and out going messages
 *
 */
typedef struct {
	QoS qos;		///< Message Quality of Service
	uint8_t isRetained;	///< Retained messages are \b NOT supported by the AWS IoT Service at the time of this SDK release.
	uint8_t isDup;		///< Is this message a duplicate QoS > 0 message?  Handled automatically by the MQTT client.
	uint16_t id;		///< Message sequence identifier.  Handled automatically by the MQTT client.
	void *payload;		///< Pointer to MQTT message payload (bytes).
	size_t payloadLen;	///< Length of MQTT payload.
} IoT_Publish_Message_Params;

/**
 * @brief MQTT Version Type
 *
 * Defining an MQTT version type. Only 3.1.1 is supported at this time
 *
 */
typedef enum {
	MQTT_3_1_1 = 4    ///< MQTT 3.1.1 (protocol message byte = 4)
} MQTT_Ver_t;

/**
 * @brief Last Will and Testament Definition
 *
 * Defining a type for the MQTT "Last Will and Testament" (LWT) parameters.
 * @note Retained messages are \b NOT supported by the AWS IoT Service at the time of this SDK release.
 *
 */
typedef struct {
	char struct_id[4];		///< The eyecatcher for this structure.  must be MQTW
	char *pTopicName;		///< The LWT topic to which the LWT message will be published
	uint16_t topicNameLen;		///< The length of the LWT topic, 16 bit unsinged integer
	char *pMessage;			///< Message to be delivered as LWT
	uint16_t msgLen;		///< The length of the Message, 16 bit unsinged integer
	bool isRetained;		///< NOT supported. The retained flag for the LWT message (see MQTTAsync_message.retained)
	QoS qos;			///< QoS of LWT message
} IoT_MQTT_Will_Options;
/** Default initializer for will */
extern const IoT_MQTT_Will_Options iotMqttWillOptionsDefault;

/** Default initializer for will */
#define IoT_MQTT_Will_Options_Initializer { {'M', 'Q', 'T', 'W'}, NULL, 0, NULL, 0, false, QOS0 }

/**
 * @brief MQTT Connection Parameters
 *
 * Defining a type for MQTT connection parameters.  Passed into client when establishing a connection.
 *
 */
typedef struct {
	char struct_id[4];			///< The eyecatcher for this structure.  must be MQTC
	MQTT_Ver_t MQTTVersion;			///< Desired MQTT version used during connection
	const char *pClientID;                	///< Pointer to a string defining the MQTT client ID (this needs to be unique \b per \b device across your AWS account)
	uint16_t clientIDLen;			///< Client Id Length. 16 bit unsigned integer
	uint16_t keepAliveIntervalInSec;	///< MQTT keep alive interval in seconds.  Defines inactivity time allowed before determining the connection has been lost.
	bool isCleanSession;			///< MQTT clean session.  True = this session is to be treated as clean.  Previous server state is cleared and no stated is retained from this connection.
	bool isWillMsgPresent;			///< Is there a LWT associated with this connection?
	IoT_MQTT_Will_Options will;		///< MQTT LWT parameters.
	char *pUsername;			///< Not used in the AWS IoT Service, will need to be cstring if used
	uint16_t usernameLen;			///< Username Length. 16 bit unsigned integer
	char *pPassword;			///< Not used in the AWS IoT Service, will need to be cstring if used
	uint16_t passwordLen;			///< Password Length. 16 bit unsigned integer
} IoT_Client_Connect_Params;
/** Default initializer for connect */
extern const IoT_Client_Connect_Params iotClientConnectParamsDefault;

/** Default initializer for connect */
#define IoT_Client_Connect_Params_initializer { {'M', 'Q', 'T', 'C'}, MQTT_3_1_1, NULL, 0, 60, true, false, \
        IoT_MQTT_Will_Options_Initializer, NULL, 0, NULL, 0 }

/**
 * @brief Disconnect Callback Handler Type
 *
 * Defining a TYPE for definition of disconnect callback function pointers.
 *
 */
typedef void (*iot_disconnect_handler)(AWS_IoT_Client *, void *);

/**
 * @brief MQTT Initialization Parameters
 *
 * Defining a type for MQTT initialization parameters.
 * Passed into client when to initialize the client
 *
 */
typedef struct {
	bool enableAutoReconnect;			///< Set to true to enable auto reconnect
	char *pHostURL;					///< Pointer to a string defining the endpoint for the MQTT service
	uint16_t port;					///< MQTT service listening port
	const char *pRootCALocation;				///< Pointer to a string defining the Root CA file (full file, not path)
	const char *pDeviceCertLocation;			///< Pointer to a string defining the device identity certificate file (full file, not path)
	const char *pDevicePrivateKeyLocation;        	///< Pointer to a string defining the device private key file (full file, not path)
	uint32_t mqttPacketTimeout_ms;			///< Timeout for reading a complete MQTT packet. In milliseconds
	uint32_t mqttCommandTimeout_ms;			///< Timeout for MQTT blocking calls. In milliseconds
	uint32_t tlsHandshakeTimeout_ms;		///< TLS handshake timeout.  In milliseconds
	bool isSSLHostnameVerify;			///< Client should perform server certificate hostname validation
	iot_disconnect_handler disconnectHandler;	///< Callback to be invoked upon connection loss
	void *disconnectHandlerData;			///< Data to pass as argument when disconnect handler is called
#ifdef _ENABLE_THREAD_SUPPORT_
	bool isBlockOnThreadLockEnabled;		///< Timeout for Thread blocking calls. Set to 0 to block until lock is obtained. In milliseconds
#endif
} IoT_Client_Init_Params;
/** Default initializer for client */
extern const IoT_Client_Init_Params iotClientInitParamsDefault;

/** Default initializer for client */
#ifdef _ENABLE_THREAD_SUPPORT_
#define IoT_Client_Init_Params_initializer { true, NULL, 0, NULL, NULL, NULL, 2000, 20000, 5000, true, NULL, NULL, false }
#else
#define IoT_Client_Init_Params_initializer { true, NULL, 0, NULL, NULL, NULL, 2000, 20000, 5000, true, NULL, NULL }
#endif

/**
 * @brief MQTT Client State Type
 *
 * Defining a type for MQTT Client State
 *
 */
typedef enum _ClientState {
	CLIENT_STATE_INVALID = 0,
	CLIENT_STATE_INITIALIZED = 1,
	CLIENT_STATE_CONNECTING = 2,
	CLIENT_STATE_CONNECTED_IDLE = 3,
	CLIENT_STATE_CONNECTED_YIELD_IN_PROGRESS = 4,
	CLIENT_STATE_CONNECTED_PUBLISH_IN_PROGRESS = 5,
	CLIENT_STATE_CONNECTED_SUBSCRIBE_IN_PROGRESS = 6,
	CLIENT_STATE_CONNECTED_UNSUBSCRIBE_IN_PROGRESS = 7,
	CLIENT_STATE_CONNECTED_RESUBSCRIBE_IN_PROGRESS = 8,
	CLIENT_STATE_CONNECTED_WAIT_FOR_CB_RETURN = 9,
	CLIENT_STATE_DISCONNECTING = 10,
	CLIENT_STATE_DISCONNECTED_ERROR = 11,
	CLIENT_STATE_DISCONNECTED_MANUALLY = 12,
	CLIENT_STATE_PENDING_RECONNECT = 13
} ClientState;

/**
 * @brief Application Callback Handler Type
 *
 * Defining a TYPE for definition of application callback function pointers.
 * Used to send incoming data to the application
 *
 */
typedef void (*pApplicationHandler_t)(AWS_IoT_Client *pClient, char *pTopicName, uint16_t topicNameLen,
									  IoT_Publish_Message_Params *pParams, void *pClientData);

/**
 * @brief MQTT Message Handler
 *
 * Defining a type for MQTT Message Handlers.
 * Used to pass incoming data back to the application
 *
 */
typedef struct _MessageHandlers {
	const char *topicName; ///< Topic name of subscription
	uint16_t topicNameLen; ///< Length of topic name
	char resubscribed; ///< Whether this handler was successfully resubscribed in the reconnect workflow
	QoS qos; ///< QoS of subscription
	pApplicationHandler_t pApplicationHandler; ///< Application function to invoke
	void *pApplicationHandlerData; ///< Context to pass to application handler
} MessageHandlers;   /* Message handlers are indexed by subscription topic */

/**
 * @brief MQTT Client Status
 *
 * Defining a type for MQTT Client Status
 * Contains information about the state of the MQTT Client
 *
 */
typedef struct _ClientStatus {
	ClientState clientState; ///< The current state of the client's state machine
	bool isPingOutstanding; ///< Whether this client is waiting for a ping response
	bool isAutoReconnectEnabled; ///< Whether auto-reconnect is enabled for this client
} ClientStatus;

/**
 * @brief MQTT Client Data
 *
 * Defining a type for MQTT Client Data
 * Contains data used by the MQTT Client
 *
 */
typedef struct _ClientData {
	uint16_t nextPacketId; ///< Packet ID to use for the next generated packet

	uint32_t packetTimeoutMs; ///< Timeout for reading incoming packets from the network
	uint32_t commandTimeoutMs; ///< Timeout for processing outgoing MQTT packets
	uint16_t keepAliveInterval; ///< Maximum interval between control packets
	uint32_t currentReconnectWaitInterval; ///< Current backoff period for reconnect
	uint32_t counterNetworkDisconnected; ///< How many times this client detected a disconnection

	/* The below values are initialized with the
	 * lengths of the TX/RX buffers and never modified
	 * afterwards */
	size_t writeBufSize; ///< Size of this client's outgoing data buffer
	size_t readBufSize; ///< Size of this client's incoming data buffer
	size_t readBufIndex; ///< Current offset into the incoming data buffer
	unsigned char writeBuf[AWS_IOT_MQTT_TX_BUF_LEN]; ///< Buffer for outgoing data
	unsigned char readBuf[AWS_IOT_MQTT_RX_BUF_LEN]; ///< Buffer for incoming data

#ifdef _ENABLE_THREAD_SUPPORT_
	bool isBlockOnThreadLockEnabled; ///< Whether to use nonblocking or blocking mutex APIs
	IoT_Mutex_t state_change_mutex; ///< Mutex protecting the client's state machine
	IoT_Mutex_t tls_read_mutex; ///< Mutex protecting incoming data
	IoT_Mutex_t tls_write_mutex; ///< Mutex protecting outgoing data
#endif

	IoT_Client_Connect_Params options; ///< Options passed when the client was initialized

	MessageHandlers messageHandlers[AWS_IOT_MQTT_NUM_SUBSCRIBE_HANDLERS]; ///< Callbacks for incoming messages
	iot_disconnect_handler disconnectHandler; ///< Callback when a disconnection is detected
	void *disconnectHandlerData; ///< Context for disconnect handler
} ClientData;

/**
 * @brief MQTT Client
 *
 * Defining a type for MQTT Client
 *
 */
struct _Client {
	Timer pingReqTimer;		///< Timer to keep track of when to send next PINGREQ
	Timer pingRespTimer;	///< Timer to ensure that PINGRESP is received timely
	Timer reconnectDelayTimer; ///< Timer for backoff on reconnect

	ClientStatus clientStatus; ///< Client state information
	ClientData clientData; ///< Client context
	Network networkStack; ///< Table of network function pointers
};

/**
 * @functionpage{aws_iot_mqtt_get_next_packet_id,mqtt,get_next_packet_id}
 * @functionpage{aws_iot_mqtt_set_connect_params,mqtt,set_connect_params}
 * @functionpage{aws_iot_mqtt_is_client_connected,mqtt,is_client_connected}
 * @functionpage{aws_iot_mqtt_get_client_state,mqtt,get_client_state}
 * @functionpage{aws_iot_is_autoreconnect_enabled,mqtt,is_autoreconnect_enabled}
 * @functionpage{aws_iot_mqtt_set_disconnect_handler,mqtt,set_disconnect_handler}
 * @functionpage{aws_iot_mqtt_autoreconnect_set_status,mqtt,autoreconnect_set_status}
 * @functionpage{aws_iot_mqtt_get_network_disconnected_count,mqtt,get_network_disconnected_count}
 * @functionpage{aws_iot_mqtt_reset_network_disconnected_count,mqtt,reset_network_disconnected_count}
 */

/**
 * @brief Retrieve and increment the next packet identifier for an MQTT client context.
 *
 * This function generates a two-byte packet identifier for an outgoing MQTT packet and
 * modifies the internal state of the MQTT client context so that the next call generates
 * a different packet identifier. Per the MQTT specification, MQTT packet identifiers are
 * nonzero, two-byte integers that identify certain MQTT packets. MQTT packet identifiers
 * must be unique at any given time: no two concurrent packets may use the same identifier,
 * but packet identifiers from previously processed packets may be reused.
 *
 * @param[in] pClient MQTT client context
 *
 * @return A two-byte MQTT packet identifier that will be unique for the given MQTT client
 * context.
 *
 * @warning This function is not thread safe. Do not call it concurrently from different
 * threads.
 */
/* @[declare_mqtt_get_next_packet_id] */
uint16_t aws_iot_mqtt_get_next_packet_id(AWS_IoT_Client *pClient);
/* @[declare_mqtt_get_next_packet_id] */

/**
 * @brief Reset the connection parameters of an initialized MQTT client context.
 *
 * This function replaces the current connection parameters of an MQTT client
 * context with a new set of parameters. Its primary use is to modify the connection
 * parameters for the next reconnect attempt if the existing parameters are no longer
 * valid. Therefore, it should be called just before a reconnect attempt, i.e. just
 * before @ref mqtt_function_attempt_reconnect or @ref mqtt_function_yield.
 *
 * The new connection parameters take effect at the next connection attempt.
 *
 * @param[in] pClient MQTT client context
 * @param[in] pNewConnectParams The new connection parameters
 *
 * @return Returns NULL_VALUE_ERROR if provided a bad parameter; otherwise, always
 * returns SUCCESS.
 *
 * @warning Do not call this function if a connection attempt is in progress. Connection
 * attempts happen in the context of @ref mqtt_function_connect, @ref mqtt_function_attempt_reconnect,
 * or @ref mqtt_function_yield.
 */
/* @[declare_mqtt_set_connect_params] */
IoT_Error_t aws_iot_mqtt_set_connect_params(AWS_IoT_Client *pClient, const IoT_Client_Connect_Params *pNewConnectParams);
/* @[declare_mqtt_set_connect_params] */

/**
 * @brief Determine if the MQTT client context currently connected to a server.
 *
 * This function checks the internal state of the MQTT client context to determine
 * if it is currently connected to the server.
 *
 * @param[in] pClient MQTT client context
 *
 * @return true if connected; false otherwise.
 *
 * @warning Application code should not rely on this function's return value.
 * The returned value only represents the internal state of the client and
 * does not check the network connection status.
 */
/* @[declare_mqtt_is_client_connected] */
bool aws_iot_mqtt_is_client_connected(AWS_IoT_Client *pClient);
/* @[declare_mqtt_is_client_connected] */

/**
 * @brief Get the current state of the MQTT client context.
 *
 * @param[in] pClient MQTT client context
 *
 * @return The state of the MQTT client context at the time of the function call.
 *
 * @note The client's state is internal and generally not useful to application code.
 * Applications should not make assumptions about the status of the client based on
 * its state.
 */
/* @[declare_mqtt_get_client_state] */
ClientState aws_iot_mqtt_get_client_state(AWS_IoT_Client *pClient);
/* @[declare_mqtt_get_client_state] */

/**
 * @brief Determine if auto-reconnect is enabled for an MQTT client context.
 *
 * @param[in] pClient MQTT client context
 *
 * @return true if auto-reconnect is enabled; false otherwise.
 */
/* @[declare_mqtt_is_autoreconnect_enabled] */
bool aws_iot_is_autoreconnect_enabled(AWS_IoT_Client *pClient);
/* @[declare_mqtt_is_autoreconnect_enabled] */

/**
 * @brief Reset the disconnect handler of an initialized MQTT client context.
 *
 * This function replaces the current disconnect handler of an MQTT client
 * context with a new disconnect handler.
 *
 * The new disconnect handler will be invoked when the next disconnect is detected.
 *
 * @param[in] pClient MQTT client context
 * @param[in] pDisconnectHandler New disconnect handler
 * @param[in] pDisconnectHandlerData Context to be passed to new disconnect handler
 *
 * @return Returns NULL_VALUE_ERROR if provided a bad parameter; otherwise, always
 * returns SUCCESS.
 *
 * @warning Do not call this function if @ref mqtt_function_yield is in progress.
 */
/* @[declare_mqtt_set_disconnect_handler] */
IoT_Error_t aws_iot_mqtt_set_disconnect_handler(AWS_IoT_Client *pClient, iot_disconnect_handler pDisconnectHandler,
												void *pDisconnectHandlerData);
/* @[declare_mqtt_set_disconnect_handler] */

/**
 * @brief Enable or disable auto-reconnect for an initialized MQTT client context.
 *
 * This function replaces the current auto-reconnect setting with the provided setting.
 *
 * @note This function should only be called after @ref mqtt_function_connect has been
 * called for the provided client.
 *
 * @param[in] pClient MQTT client context
 * @param[in] newStatus New setting for auto-reconnect
 *
 * @return Returns NULL_VALUE_ERROR if provided a bad parameter; otherwise, always
 * returns SUCCESS.
 *
  * @warning Do not call this function if a connection attempt is in progress. Connection
 * attempts happen in the context of @ref mqtt_function_connect, @ref mqtt_function_attempt_reconnect,
 * or @ref mqtt_function_yield.
 */
/* @[declare_mqtt_autoreconnect_set_status] */
IoT_Error_t aws_iot_mqtt_autoreconnect_set_status(AWS_IoT_Client *pClient, bool newStatus);
/* @[declare_mqtt_autoreconnect_set_status] */

/**
 * @brief Get the current number of disconnects detected by an MQTT client context.
 *
 * @param[in] pClient MQTT client context
 *
 * @return The number of disconnects detected since the client was created
 * (or since the last call to @ref mqtt_function_reset_network_disconnected_count).
 *
 * @warning Do not call this function if @ref mqtt_function_yield is in progress.
 */
/* @[declare_mqtt_get_network_disconnected_count] */
uint32_t aws_iot_mqtt_get_network_disconnected_count(AWS_IoT_Client *pClient);
/* @[declare_mqtt_get_network_disconnected_count] */

/**
 * @brief Reset the number of disconnects detected by an MQTT client context to zero.
 *
 * @param[in] pClient MQTT client context
 *
 * @warning Do not call this function if @ref mqtt_function_yield is in progress.
 */
/* @[declare_mqtt_reset_network_disconnected_count] */
void aws_iot_mqtt_reset_network_disconnected_count(AWS_IoT_Client *pClient);
/* @[declare_mqtt_reset_network_disconnected_count] */

#ifdef __cplusplus
}
#endif

#endif
