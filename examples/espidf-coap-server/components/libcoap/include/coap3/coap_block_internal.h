/*
 * coap_block_internal.h -- Structures, Enums & Functions that are not
 * exposed to application programming
 *
 * Copyright (C) 2010-2022 Olaf Bergmann <bergmann@tzi.org>
 * Copyright (C) 2021-2022 Jon Shallow <supjps-libcoap@jpshallow.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * This file is part of the CoAP library libcoap. Please see README for terms
 * of use.
 */

/**
 * @file coap_block_internal.h
 * @brief CoAP block internal information
 */

#ifndef COAP_BLOCK_INTERNAL_H_
#define COAP_BLOCK_INTERNAL_H_

#include "coap_internal.h"
#include "coap_pdu_internal.h"
#include "resource.h"

/**
 * @ingroup internal_api
 * @defgroup block_internal Block Transfer
 * Internal API for Block Transfer (RC7959)
 * @{
 */

typedef enum {
  COAP_RECURSE_OK,
  COAP_RECURSE_NO
} coap_recurse_t;

struct coap_lg_range {
  uint32_t begin;
  uint32_t end;
};

#define COAP_RBLOCK_CNT 4
/**
 * Structure to keep track of received blocks
 */
typedef struct coap_rblock_t {
  uint32_t used;
  uint32_t retry;
  struct coap_lg_range range[COAP_RBLOCK_CNT];
  coap_tick_t last_seen;
} coap_rblock_t;

/**
 * Structure to keep track of block1 specific information
 * (Requests)
 */
typedef struct coap_l_block1_t {
  coap_binary_t *app_token; /**< original PDU token */
  uint64_t state_token;  /**< state token */
  size_t bert_size;      /**< size of last BERT block */
  uint32_t count;        /**< the number of packets sent for payload */
} coap_l_block1_t;

/**
 * Structure to keep track of block2 specific information
 * (Responses)
 */
typedef struct coap_l_block2_t {
  coap_resource_t *resource; /**< associated resource */
  coap_string_t *query;  /**< Associated query for the resource */
  uint64_t etag;         /**< ETag value */
  coap_time_t maxage_expire; /**< When this entry expires */
  coap_pdu_code_t request_method; /**< Method used to request this data */
} coap_l_block2_t;

/**
 * Structure to hold large body (many blocks) transmission information
 */
struct coap_lg_xmit_t {
  struct coap_lg_xmit_t *next;
  uint8_t blk_size;      /**< large block transmission size */
  uint16_t option;       /**< large block transmisson CoAP option */
  int last_block;        /**< last acknowledged block number */
  const uint8_t *data;   /**< large data ptr */
  size_t length;         /**< large data length */
  size_t offset;         /**< large data next offset to transmit */
  union {
    coap_l_block1_t b1;
    coap_l_block2_t b2;
  } b;
  coap_pdu_t pdu;        /**< skeletal PDU */
  coap_tick_t last_payload; /**< Last time MAX_PAYLOAD was sent or 0 */
  coap_tick_t last_sent; /**< Last time any data sent */
  coap_tick_t last_all_sent; /**< Last time all data sent or 0 */
  coap_tick_t last_obs; /**< Last time used (Observe tracking) or 0 */
  coap_release_large_data_t release_func; /**< large data de-alloc function */
  void *app_ptr;         /**< applicaton provided ptr for de-alloc function */
};

#if COAP_CLIENT_SUPPORT
/**
 * Structure to hold large body (many blocks) client receive information
 */
struct coap_lg_crcv_t {
  struct coap_lg_crcv_t *next;
  uint8_t observe[3];    /**< Observe data (if observe_set) (only 24 bits) */
  uint8_t observe_length;/**< Length of observe data */
  uint8_t observe_set;   /**< Set if this is an observe receive PDU */
  uint8_t szx;           /**< size of individual blocks */
  uint8_t etag_set;      /**< Set if ETag is in receive PDU */
  uint8_t etag_length;   /**< ETag length */
  uint8_t etag[8];       /**< ETag for block checking */
  uint16_t content_format; /**< Content format for the set of blocks */
  uint8_t last_type;     /**< Last request type (CON/NON) */
  uint8_t initial;       /**< If set, has not been used yet */
  uint16_t block_option; /**< Block option in use */
  uint16_t retry_counter; /**< Retry counter (part of state token) */
  size_t total_len;      /**< Length as indicated by SIZE2 option */
  coap_binary_t *body_data; /**< Used for re-assembling entire body */
  coap_binary_t *app_token; /**< app requesting PDU token */
  coap_binary_t *obs_token; /**< Initial Observe response PDU token */
  uint64_t state_token; /**< state token */
  coap_pdu_t pdu;        /**< skeletal PDU */
  coap_rblock_t rec_blocks; /** < list of received blocks */
  coap_tick_t last_used; /**< Last time all data sent or 0 */
};
#endif /* COAP_CLIENT_SUPPORT */

#if COAP_SERVER_SUPPORT
/**
 * Structure to hold large body (many blocks) server receive information
 */
struct coap_lg_srcv_t {
  struct coap_lg_srcv_t *next;
  uint8_t observe[3];    /**< Observe data (if set) (only 24 bits) */
  uint8_t observe_length;/**< Length of observe data */
  uint8_t observe_set;   /**< Set if this is an observe receive PDU */
  uint8_t rtag_set;      /**< Set if RTag is in receive PDU */
  uint8_t rtag_length;   /**< RTag length */
  uint8_t rtag[8];       /**< RTag for block checking */
  uint16_t content_format; /**< Content format for the set of blocks */
  uint8_t last_type;     /**< Last request type (CON/NON) */
  uint8_t szx;           /**< size of individual blocks */
  size_t total_len;      /**< Length as indicated by SIZE1 option */
  coap_binary_t *body_data; /**< Used for re-assembling entire body */
  size_t amount_so_far;  /**< Amount of data seen so far */
  coap_resource_t *resource; /**< associated resource */
  coap_str_const_t *uri_path; /** set to uri_path if unknown resource */
  coap_rblock_t rec_blocks; /** < list of received blocks */
  uint8_t last_token[8]; /**< last used token */
  size_t last_token_length; /**< length of token */
  coap_mid_t last_mid;   /**< Last received mid for this set of packets */
  coap_tick_t last_used; /**< Last time data sent or 0 */
  uint16_t block_option; /**< Block option in use */
};
#endif /* COAP_SERVER_SUPPORT */

#if COAP_CLIENT_SUPPORT
coap_lg_crcv_t * coap_block_new_lg_crcv(coap_session_t *session,
                                        coap_pdu_t *pdu);

void coap_block_delete_lg_crcv(coap_session_t *session,
                               coap_lg_crcv_t *lg_crcv);

int coap_block_check_lg_crcv_timeouts(coap_session_t *session,
                                      coap_tick_t now,
                                      coap_tick_t *tim_rem);
#endif /* COAP_CLIENT_SUPPORT */

#if COAP_SERVER_SUPPORT
void coap_block_delete_lg_srcv(coap_session_t *session,
                               coap_lg_srcv_t *lg_srcv);

int coap_block_check_lg_srcv_timeouts(coap_session_t *session,
                                      coap_tick_t now,
                                      coap_tick_t *tim_rem);

int coap_handle_request_send_block(coap_session_t *session,
                                   coap_pdu_t *pdu,
                                   coap_pdu_t *response,
                                   coap_resource_t *resource,
                                   coap_string_t *query);

int coap_handle_request_put_block(coap_context_t *context,
                                  coap_session_t *session,
                                  coap_pdu_t *pdu,
                                  coap_pdu_t *response,
                                  coap_resource_t *resource,
                                  coap_string_t *uri_path,
                                  coap_opt_t *observe,
                                  coap_string_t *query,
                                  coap_method_handler_t h,
                                  int *added_block);
#endif /* COAP_SERVER_SUPPORT */

#if COAP_CLIENT_SUPPORT
int coap_handle_response_send_block(coap_session_t *session, coap_pdu_t *sent,
                                    coap_pdu_t *rcvd);

int coap_handle_response_get_block(coap_context_t *context,
                                   coap_session_t *session,
                                   coap_pdu_t *sent,
                                   coap_pdu_t *rcvd,
                                   coap_recurse_t recursive);
#endif /* COAP_CLIENT_SUPPORT */

void coap_block_delete_lg_xmit(coap_session_t *session,
                               coap_lg_xmit_t *lg_xmit);

int coap_block_check_lg_xmit_timeouts(coap_session_t *session,
                                      coap_tick_t now,
                                      coap_tick_t *tim_rem);

/**
 * The function checks that the code in a newly formed lg_xmit created by
 * coap_add_data_large_response() is updated.
 *
 * @param session  The session
 * @param response The response PDU to to check
 * @param resource The requested resource
 * @param query    The requested query
 * @param request_method The requested method
 */
void coap_check_code_lg_xmit(coap_session_t *session, coap_pdu_t *response,
                             coap_resource_t *resource, coap_string_t *query,
                             coap_pdu_code_t request_method);

/** @} */

#endif /* COAP_BLOCK_INTERNAL_H_ */
