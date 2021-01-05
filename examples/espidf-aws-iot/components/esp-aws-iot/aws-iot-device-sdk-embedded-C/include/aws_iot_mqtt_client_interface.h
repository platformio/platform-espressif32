/*
* Copyright 2015-2016 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
 * @file aws_iot_mqtt_client_interface.h
 * @brief Interface definition for MQTT client.
 */

#ifndef AWS_IOT_SDK_SRC_IOT_MQTT_INTERFACE_H
#define AWS_IOT_SDK_SRC_IOT_MQTT_INTERFACE_H

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
#include "aws_iot_mqtt_client.h"

/* Platform specific implementation header files */
#include "network_interface.h"
#include "timer_interface.h"

/**
 * @functionspage{mqtt,MQTT library}
 *
 * API functions
 * - @functionname{mqtt_function_init}
 * - @functionname{mqtt_function_free}
 * - @functionname{mqtt_function_connect}
 * - @functionname{mqtt_function_publish}
 * - @functionname{mqtt_function_subscribe}
 * - @functionname{mqtt_function_resubscribe}
 * - @functionname{mqtt_function_unsubscribe}
 * - @functionname{mqtt_function_disconnect}
 * - @functionname{mqtt_function_yield}
 * - @functionname{mqtt_function_attempt_reconnect}
 * - @functionname{mqtt_function_get_next_packet_id}
 * - @functionname{mqtt_function_set_connect_params}
 * - @functionname{mqtt_function_is_client_connected}
 * - @functionname{mqtt_function_get_client_state}
 * - @functionname{mqtt_function_is_autoreconnect_enabled}
 * - @functionname{mqtt_function_set_disconnect_handler}
 * - @functionname{mqtt_function_autoreconnect_set_status}
 * - @functionname{mqtt_function_get_network_disconnected_count}
 * - @functionname{mqtt_function_reset_network_disconnected_count}
 */

/**
 * @functionpage{aws_iot_mqtt_init,mqtt,init}
 * @functionpage{aws_iot_mqtt_free,mqtt,free}
 * @functionpage{aws_iot_mqtt_connect,mqtt,connect}
 * @functionpage{aws_iot_mqtt_publish,mqtt,publish}
 * @functionpage{aws_iot_mqtt_subscribe,mqtt,subscribe}
 * @functionpage{aws_iot_mqtt_resubscribe,mqtt,resubscribe}
 * @functionpage{aws_iot_mqtt_unsubscribe,mqtt,unsubscribe}
 * @functionpage{aws_iot_mqtt_disconnect,mqtt,disconnect}
 * @functionpage{aws_iot_mqtt_yield,mqtt,yield}
 * @functionpage{aws_iot_mqtt_attempt_reconnect,mqtt,attempt_reconnect}
 */

/**
 * @brief Initialize a new MQTT client context.
 *
 * This function should be called before any other MQTT function to initialize
 * a new MQTT client context. Once the client context is no longer needed,
 * @ref mqtt_function_free should be called.
 *
 * @param[in] pClient MQTT client context to initialize
 * @param[in] pInitParams The MQTT connection parameters
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 */
/* @[declare_mqtt_init] */
IoT_Error_t aws_iot_mqtt_init(AWS_IoT_Client *pClient, const IoT_Client_Init_Params *pInitParams);
/* @[declare_mqtt_init] */

/**
 * @brief Clean up an MQTT client context that is no longer needed.
 *
 * This function will free up resources used by an MQTT client context. It should
 * only be called when that context is no longer needed.
 *
 * @param[in] pClient MQTT client context that was previously initialized by
 * @ref mqtt_function_init
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 */
/* @[declare_mqtt_free] */
IoT_Error_t aws_iot_mqtt_free( AWS_IoT_Client *pClient );
/* @[declare_mqtt_free] */

/**
 * @brief Establish a connection with an MQTT server.
 *
 * This function should be called once and after @ref mqtt_function_init. It sends
 * the MQTT CONNECT packet to the server, which establishes an MQTT session. Once
 * the session is no longer needed, it can be closed with @ref mqtt_function_disconnect.
 *
 * @param[in] pClient MQTT client context
 * @param[in] pConnectParams MQTT connection parameters
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 */
/* @[declare_mqtt_connect] */
IoT_Error_t aws_iot_mqtt_connect(AWS_IoT_Client *pClient, const IoT_Client_Connect_Params *pConnectParams);
/* @[declare_mqtt_connect] */

/**
 * @brief Publish an MQTT message to a topic.
 *
 * This function sends an MQTT message to the server. The server will then
 * forward this message to any clients with subscriptions to topic filters
 * that match the message's topic.
 *
 * For a QoS 0 message, this function returns after the message is successfully
 * passed to the TLS layer. For a QoS 1 message, this function returns after the
 * receipt of the PUBACK for the transmitted message.
 *
 * @param pClient MQTT client context
 * @param pTopicName Topic name to publish to
 * @param topicNameLen Length of the topic name
 * @param pParams Publish message parameters
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 */
/* @[declare_mqtt_publish] */
IoT_Error_t aws_iot_mqtt_publish(AWS_IoT_Client *pClient, const char *pTopicName, uint16_t topicNameLen,
								 IoT_Publish_Message_Params *pParams);
/* @[declare_mqtt_publish] */

/**
 * @brief Subscribe to an MQTT topic.
 *
 * This function sends an MQTT subscribe packet to the server. It registers
 * a subscription that will cause the provided callback function to be invoked
 * when the server sends a message on a matching topic to the client.
 *
 * @note Incoming messages are handled by @ref mqtt_function_yield. Therefore,
 * @ref mqtt_function_yield must always be called regularly if any subscriptions
 * are active.
 *
 * @param[in] pClient MQTT client context
 * @param[in] pTopicName Topic for subscription
 * @param[in] topicNameLen Length of topic
 * @param[in] qos Quality of service for subscription
 * @param[in] pApplicationHandler Callback function for incoming messages that arrive
 * on this subscription
 * @param[in] pApplicationHandlerData Data passed to the callback
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 *
 * @attention The `pTopicName` parameter is not copied. It must remain valid for the duration
 * of the subscription (until @ref mqtt_function_unsubscribe) is called.
 */
/* @[declare_mqtt_subscribe] */
IoT_Error_t aws_iot_mqtt_subscribe(AWS_IoT_Client *pClient, const char *pTopicName, uint16_t topicNameLen,
								   QoS qos, pApplicationHandler_t pApplicationHandler, void *pApplicationHandlerData);
/* @[declare_mqtt_subscribe] */

/**
 * @brief Resubscribe to topic filter subscriptions in a previous MQTT session.
 *
 * This function restores subscriptions that were previously present in an
 * MQTT session. Its primary use is to restore subscriptions after a session
 * is manually disconnected and reopened.
 *
 * @note This function does not need to be called after @ref mqtt_function_attempt_reconnect
 * or if auto-reconnect is enabled.
 *
 * @param[in] pClient MQTT client context
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 */
/* @[declare_mqtt_resubscribe] */
IoT_Error_t aws_iot_mqtt_resubscribe(AWS_IoT_Client *pClient);
/* @[declare_mqtt_resubscribe] */

/**
 * @brief Unsubscribe from an MQTT topic filter.
 *
 * This function removes an MQTT subscription previously set by @ref mqtt_function_subscribe.
 * It sends an MQTT UNSUBSCRIBE packet to the server and removes the topic's message
 * handler stored by the client.
 *
 * @param[in] pClient MQTT client context
 * @param[in] pTopicFilter Topic filter of the subscription to remove
 * @param[in] topicFilterLen Length of topic filter to remove
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 */
/* @[declare_mqtt_unsubscribe] */
IoT_Error_t aws_iot_mqtt_unsubscribe(AWS_IoT_Client *pClient, const char *pTopicFilter, uint16_t topicFilterLen);
/* @[declare_mqtt_unsubscribe] */

/**
 * @brief Disconnect an MQTT session.
 *
 * This function sends the MQTT DISCONNECT packet, which closes the MQTT session
 * between the client and server. After this function returns, the MQTT client
 * context should be either freed with @ref mqtt_function_free or reopened with
 * @ref mqtt_function_connect.
 *
 * @param[in] pClient MQTT client context
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 */
/* @[declare_mqtt_disconnect] */
IoT_Error_t aws_iot_mqtt_disconnect(AWS_IoT_Client *pClient);
/* @[declare_mqtt_disconnect] */

/**
 * @brief Provide time for the MQTT client to process events.
 *
 * This function processes the following events:
 * - Incoming messages from the server <br>
 * Whenever a client publishes a message on a topic, the server sends that
 * message to all the clients whose subscriptions match the message's
 * topic. The messages sent by the server are received by this function,
 * which in turn calls the corresponding message handler. This function
 * must be called at a rate faster than the incoming messages, as it is the
 * only way the client receives processing time to manage incoming messages.
 * - MQTT keep-alive (sending ping requests and processing ping responses) <br>
 * The MQTT keep-alive mechanism involves sending pings to the server if the connection
 * is idle. Therefore, in the absence of any other messages, <b>this function must be called
 * at least once every keep-alive period to send the ping request</b>.
 * - @ref mqtt_autoreconnect (if enabled) <br>
 * If the client detects a disconnect, the reconnection will be performed in this function.
 *
 * @param[in] pClient MQTT client context
 * @param[in] timeout_ms Amount of time to yield. This function will return to the caller
 * after AT LEAST this amount of thime has passed.
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 * @return If this call results a negative value, assume the MQTT connection has dropped.
 * @ref mqtt_function_is_client_connected can be called to confirm. If a reconnection is
 * needed, @ref mqtt_function_attempt_reconnect should be called.
 */
/* @[declare_mqtt_yield] */
IoT_Error_t aws_iot_mqtt_yield(AWS_IoT_Client *pClient, uint32_t timeout_ms);
/* @[declare_mqtt_yield] */

/**
 * @brief Attempt to reconnect with the MQTT server.
 *
 * This function makes a single reconnect attempt with the server. If the
 * reconnection is successful, subscriptions from the client's previous
 * session are restored as well.
 *
 * If this function fails, the client's state is set to `CLIENT_STATE_PENDING_RECONNECT`.
 *
 * @param[in] pClient MQTT client context
 *
 * @return `IoT_Error_t`: See `aws_iot_error.h`
 *
 * @note Generally, it is not necessary to call this function if @ref mqtt_autoreconnect
 * is enabled. This function may still be called to initiate a reconnect attempt when
 * auto-reconnect has exhausted all attempts.
 */
/* @[declare_mqtt_attempt_reconnect] */
IoT_Error_t aws_iot_mqtt_attempt_reconnect(AWS_IoT_Client *pClient);
/* @[declare_mqtt_attempt_reconnect] */

#ifdef __cplusplus
}
#endif

#endif
