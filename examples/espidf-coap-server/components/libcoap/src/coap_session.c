/* coap_session.c -- Session management for libcoap
 *
 * Copyright (C) 2017 Jean-Claue Michelou <jcm@spinetix.com>
 * Copyright (C) 2022 Jon Shallow <supjps-libcoap@jpshallow.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * This file is part of the CoAP library libcoap. Please see
 * README for terms of use.
 */

/**
 * @file coap_session.c
 * @brief Session handling functions
 */

#include "coap3/coap_internal.h"

#ifndef COAP_SESSION_C_
#define COAP_SESSION_C_

#include <stdio.h>

#ifdef COAP_EPOLL_SUPPORT
#include <sys/epoll.h>
#include <sys/timerfd.h>
#endif /* COAP_EPOLL_SUPPORT */
#include <errno.h>

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#else /* ! HAVE_INTTYPES_H */
#define PRIu32 "u"
#endif /* ! HAVE_INTTYPES_H */

void
coap_session_set_ack_timeout(coap_session_t *session, coap_fixed_point_t value) {
  if (value.integer_part > 0 && value.fractional_part < 1000) {
    session->ack_timeout = value;
    coap_log(LOG_DEBUG, "***%s: session ack_timeout set to %u.%03u\n",
           coap_session_str(session), session->ack_timeout.integer_part,
           session->ack_timeout.fractional_part);
  }
}

void
coap_session_set_ack_random_factor(coap_session_t *session,
                                   coap_fixed_point_t value) {
  if (value.integer_part > 0 && value.fractional_part < 1000) {
    session->ack_random_factor = value;
    coap_log(LOG_DEBUG, "***%s: session ack_random_factor set to %u.%03u\n",
           coap_session_str(session), session->ack_random_factor.integer_part,
           session->ack_random_factor.fractional_part);
  }
}

void
coap_session_set_max_retransmit(coap_session_t *session, uint16_t value) {
  if (value > 0) {
    session->max_retransmit = value;
    coap_log(LOG_DEBUG, "***%s: session max_retransmit set to %u\n",
           coap_session_str(session), session->max_retransmit);
  }
}

void
coap_session_set_nstart(coap_session_t *session, uint16_t value) {
  if (value > 0) {
    session->nstart = value;
    coap_log(LOG_DEBUG, "***%s: session nstart set to %u\n",
           coap_session_str(session), session->nstart);
  }
}

void
coap_session_set_default_leisure(coap_session_t *session,
                                 coap_fixed_point_t value) {
  if (value.integer_part > 0 && value.fractional_part < 1000) {
    session->default_leisure = value;
    coap_log(LOG_DEBUG, "***%s: session default_leisure set to %u.%03u\n",
           coap_session_str(session), session->default_leisure.integer_part,
           session->default_leisure.fractional_part);
  }
}

void
coap_session_set_probing_rate(coap_session_t *session, uint32_t value) {
  if (value > 0) {
    session->probing_rate = value;
    coap_log(LOG_DEBUG, "***%s: session probing_rate set to %" PRIu32 "\n",
           coap_session_str(session), session->probing_rate);
  }
}

coap_fixed_point_t
coap_session_get_ack_timeout(const coap_session_t *session) {
  return session->ack_timeout;
}

coap_fixed_point_t
coap_session_get_ack_random_factor(const coap_session_t *session) {
  return session->ack_random_factor;
}

uint16_t
coap_session_get_max_retransmit(const coap_session_t *session) {
  return session->max_retransmit;
}

uint16_t
coap_session_get_nstart(const coap_session_t *session) {
  return session->nstart;
}

coap_fixed_point_t
coap_session_get_default_leisure(const coap_session_t *session) {
  return session->default_leisure;
}

uint32_t
coap_session_get_probing_rate(const coap_session_t *session) {
  return session->probing_rate;
}

coap_session_t *
coap_session_reference(coap_session_t *session) {
  ++session->ref;
  return session;
}

void
coap_session_release(coap_session_t *session) {
  if (session) {
#ifndef __COVERITY__
    assert(session->ref > 0);
    if (session->ref > 0)
      --session->ref;
    if (session->ref == 0 && session->type == COAP_SESSION_TYPE_CLIENT)
      coap_session_free(session);
#else /* __COVERITY__ */
    /* Coverity scan is fooled by the reference counter leading to
     * false positives for USE_AFTER_FREE. */
    --session->ref;
    __coverity_negative_sink__(session->ref);
    /* Indicate that resources are released properly. */
    if (session->ref == 0 && session->type == COAP_SESSION_TYPE_CLIENT) {
      __coverity_free__(session);
    }
#endif /* __COVERITY__ */
  }
}

void
coap_session_set_app_data(coap_session_t *session, void *app_data) {
  assert(session);
  session->app = app_data;
}

void *
coap_session_get_app_data(const coap_session_t *session) {
  assert(session);
  return session->app;
}

static coap_session_t *
coap_make_session(coap_proto_t proto, coap_session_type_t type,
                  const coap_addr_hash_t *addr_hash,
                  const coap_address_t *local_addr,
                  const coap_address_t *remote_addr, int ifindex,
                  coap_context_t *context, coap_endpoint_t *endpoint) {
  coap_session_t *session = (coap_session_t*)coap_malloc_type(COAP_SESSION,
                                                     sizeof(coap_session_t));
#if ! COAP_SERVER_SUPPORT
  (void)endpoint;
#endif /* ! COAP_SERVER_SUPPORT */
  if (!session)
    return NULL;
  memset(session, 0, sizeof(*session));
  session->proto = proto;
  session->type = type;
  if (addr_hash)
    memcpy(&session->addr_hash, addr_hash, sizeof(session->addr_hash));
  else
    memset(&session->addr_hash, 0, sizeof(session->addr_hash));
  if (local_addr)
    coap_address_copy(&session->addr_info.local, local_addr);
  else
    coap_address_init(&session->addr_info.local);
  if (remote_addr)
    coap_address_copy(&session->addr_info.remote, remote_addr);
  else
    coap_address_init(&session->addr_info.remote);
  session->ifindex = ifindex;
  session->context = context;
#if COAP_SERVER_SUPPORT
  session->endpoint = endpoint;
  if (endpoint)
    session->mtu = endpoint->default_mtu;
  else
#endif /* COAP_SERVER_SUPPORT */
    session->mtu = COAP_DEFAULT_MTU;
  session->block_mode = context->block_mode;
  if (proto == COAP_PROTO_DTLS) {
    session->tls_overhead = 29;
    if (session->tls_overhead >= session->mtu) {
      session->tls_overhead = session->mtu;
      coap_log(LOG_ERR, "DTLS overhead exceeds MTU\n");
    }
  }
  session->ack_timeout = COAP_DEFAULT_ACK_TIMEOUT;
  session->ack_random_factor = COAP_DEFAULT_ACK_RANDOM_FACTOR;
  session->max_retransmit = COAP_DEFAULT_MAX_RETRANSMIT;
  session->nstart = COAP_DEFAULT_NSTART;
  session->default_leisure = COAP_DEFAULT_DEFAULT_LEISURE;
  session->probing_rate = COAP_DEFAULT_PROBING_RATE;
  session->dtls_event = -1;
  session->last_ping_mid = COAP_INVALID_MID;
  session->last_ack_mid = COAP_INVALID_MID;
  session->last_con_mid = COAP_INVALID_MID;

  /* Randomly initialize */
  coap_prng((unsigned char *)&session->tx_mid, sizeof(session->tx_mid));
  coap_prng((unsigned char *)&session->tx_rtag, sizeof(session->tx_rtag));

  return session;
}

void coap_session_mfree(coap_session_t *session) {
  coap_queue_t *q, *tmp;
  coap_lg_xmit_t *lq, *ltmp;

#if COAP_CLIENT_SUPPORT
  coap_lg_crcv_t *cq, *etmp;

  /* Need to do this before (D)TLS and socket is closed down */
  LL_FOREACH_SAFE(session->lg_crcv, cq, etmp) {
    if (cq->observe_set && session->no_observe_cancel == 0) {
      /* Need to close down observe */
      if (coap_cancel_observe(session, cq->app_token, COAP_MESSAGE_NON)) {
        /* Need to delete node we set up for NON */
        coap_queue_t *queue = session->context->sendqueue;

        while (queue) {
          if (queue->session == session) {
            coap_delete_node(queue);
            break;
          }
          queue = queue->next;
        }
      }
    }
    LL_DELETE(session->lg_crcv, cq);
    coap_block_delete_lg_crcv(session, cq);
  }
#endif /* COAP_CLIENT_SUPPORT */

  if (session->partial_pdu)
    coap_delete_pdu(session->partial_pdu);
  if (session->proto == COAP_PROTO_DTLS)
    coap_dtls_free_session(session);
#if !COAP_DISABLE_TCP
  else if (session->proto == COAP_PROTO_TLS)
    coap_tls_free_session(session);
#endif /* !COAP_DISABLE_TCP */
  if (session->sock.flags != COAP_SOCKET_EMPTY)
    coap_socket_close(&session->sock);
  if (session->psk_identity)
    coap_free(session->psk_identity);
  if (session->psk_key)
    coap_free(session->psk_key);
  if (session->psk_hint)
    coap_free(session->psk_hint);

#if COAP_SERVER_SUPPORT
  coap_cache_entry_t *cp, *ctmp;
  HASH_ITER(hh, session->context->cache, cp, ctmp) {
    /* cp->session is NULL if not session based */
    if (cp->session == session) {
      coap_delete_cache_entry(session->context, cp);
    }
  }
#endif /* COAP_SERVER_SUPPORT */
  LL_FOREACH_SAFE(session->delayqueue, q, tmp) {
    if (q->pdu->type==COAP_MESSAGE_CON && session->context && session->context->nack_handler)
      session->context->nack_handler(session, q->pdu, session->proto == COAP_PROTO_DTLS ? COAP_NACK_TLS_FAILED : COAP_NACK_NOT_DELIVERABLE, q->id);
    coap_delete_node(q);
  }
  LL_FOREACH_SAFE(session->lg_xmit, lq, ltmp) {
    LL_DELETE(session->lg_xmit, lq);
    coap_block_delete_lg_xmit(session, lq);
  }
#if COAP_SERVER_SUPPORT
  coap_lg_srcv_t *sq, *stmp;

  LL_FOREACH_SAFE(session->lg_srcv, sq, stmp) {
    LL_DELETE(session->lg_srcv, sq);
    coap_block_delete_lg_srcv(session, sq);
  }
#endif /* COAP_SERVER_SUPPORT */
}

void coap_session_free(coap_session_t *session) {
  if (!session)
    return;
  assert(session->ref == 0);
  if (session->ref)
    return;
  coap_session_mfree(session);
#if COAP_SERVER_SUPPORT
  if (session->endpoint) {
    if (session->endpoint->sessions)
      SESSIONS_DELETE(session->endpoint->sessions, session);
  } else
#endif /* COAP_SERVER_SUPPORT */
#if COAP_CLIENT_SUPPORT
  if (session->context) {
    if (session->context->sessions)
      SESSIONS_DELETE(session->context->sessions, session);
  }
#endif /* COAP_CLIENT_SUPPORT */
  coap_delete_bin_const(session->last_token);
  coap_log(LOG_DEBUG, "***%s: session %p: closed\n", coap_session_str(session),
           (void *)session);

  coap_free_type(COAP_SESSION, session);
}

static size_t
coap_session_max_pdu_size_internal(const coap_session_t *session,
                                   size_t max_with_header) {
#if COAP_DISABLE_TCP
  return max_with_header > 4 ? max_with_header - 4 : 0;
#else /* !COAP_DISABLE_TCP */
  if (COAP_PROTO_NOT_RELIABLE(session->proto))
    return max_with_header > 4 ? max_with_header - 4 : 0;
  /* we must assume there is no token to be on the safe side */
  if (max_with_header <= 2)
    return 0;
  else if (max_with_header <= COAP_MAX_MESSAGE_SIZE_TCP0 + 2)
    return max_with_header - 2;
  else if (max_with_header <= COAP_MAX_MESSAGE_SIZE_TCP8 + 3)
    return max_with_header - 3;
  else if (max_with_header <= COAP_MAX_MESSAGE_SIZE_TCP16 + 4)
    return max_with_header - 4;
  else
    return max_with_header - 6;
#endif /* !COAP_DISABLE_TCP */
}

size_t
coap_session_max_pdu_rcv_size(const coap_session_t *session) {
  if (session->csm_rcv_mtu)
    return coap_session_max_pdu_size_internal(session,
                                              (size_t)(session->csm_rcv_mtu));

  return coap_session_max_pdu_size_internal(session,
                              (size_t)(session->mtu - session->tls_overhead));
}

size_t
coap_session_max_pdu_size(const coap_session_t *session) {
  size_t max_with_header;

#if COAP_CLIENT_SUPPORT
  /*
   * Delay if session->doing_first is set.
   * E.g. Reliable and CSM not in yet for checking block support
   */
  coap_session_t *session_rw;

  /*
   * Need to do this to not get a compiler warning about const parameters
   * but need to maintain source code backward compatibility
   */
  memcpy(&session_rw, &session, sizeof(session_rw));
  if (coap_client_delay_first(session_rw) == 0) {
    coap_log(LOG_DEBUG, "coap_client_delay_first: timeout\n");
    /* Have to go with the defaults */
  }
#endif /* COAP_CLIENT_SUPPORT */

  max_with_header = (size_t)(session->mtu - session->tls_overhead);

  return coap_session_max_pdu_size_internal(session, max_with_header);
}

void coap_session_set_mtu(coap_session_t *session, unsigned mtu) {
#if defined(WITH_CONTIKI) || defined(WITH_LWIP)
  if (mtu > COAP_MAX_MESSAGE_SIZE_TCP16 + 4)
    mtu = COAP_MAX_MESSAGE_SIZE_TCP16 + 4;
#endif
  if (mtu < 64)
    mtu = 64;
  session->mtu = mtu;
  if (session->tls_overhead >= session->mtu) {
    session->tls_overhead = session->mtu;
    coap_log(LOG_ERR, "DTLS overhead exceeds MTU\n");
  }
}

ssize_t coap_session_send(coap_session_t *session, const uint8_t *data, size_t datalen) {
  ssize_t bytes_written;

  coap_socket_t *sock = &session->sock;
#if COAP_SERVER_SUPPORT
  if (sock->flags == COAP_SOCKET_EMPTY) {
    assert(session->endpoint != NULL);
    sock = &session->endpoint->sock;
  }
#endif /* COAP_SERVER_SUPPORT */

  bytes_written = coap_socket_send(sock, session, data, datalen);
  if (bytes_written == (ssize_t)datalen) {
    coap_ticks(&session->last_rx_tx);
    coap_log(LOG_DEBUG, "*  %s: sent %zd bytes\n",
             coap_session_str(session), datalen);
  } else {
    coap_log(LOG_DEBUG, "*  %s: failed to send %zd bytes\n",
             coap_session_str(session), datalen);
  }
  return bytes_written;
}

ssize_t coap_session_write(coap_session_t *session, const uint8_t *data, size_t datalen) {
  ssize_t bytes_written = coap_socket_write(&session->sock, data, datalen);
  if (bytes_written > 0) {
    coap_ticks(&session->last_rx_tx);
    coap_log(LOG_DEBUG, "*  %s: sent %zd bytes\n",
             coap_session_str(session), bytes_written);
  } else if (bytes_written < 0) {
    coap_log(LOG_DEBUG,  "*   %s: failed to send %zd bytes\n",
             coap_session_str(session), datalen );
  }
  return bytes_written;
}

ssize_t
coap_session_delay_pdu(coap_session_t *session, coap_pdu_t *pdu,
                       coap_queue_t *node)
{
  if ( node ) {
    coap_queue_t *removed = NULL;
    coap_remove_from_queue(&session->context->sendqueue, session, node->id, &removed);
    assert(removed == node);
    coap_session_release(node->session);
    node->session = NULL;
    node->t = 0;
  } else {
    if (COAP_PROTO_NOT_RELIABLE(session->proto)) {
      coap_queue_t *q = NULL;
      /* Check same mid is not getting re-used in violation of RFC7252 */
      LL_FOREACH(session->delayqueue, q) {
        if (q->id == pdu->mid) {
          coap_log(LOG_ERR, "**  %s: mid=0x%x: already in-use - dropped\n",
                   coap_session_str(session), pdu->mid);
          return COAP_INVALID_MID;
        }
      }
    }
    node = coap_new_node();
    if (node == NULL)
      return COAP_INVALID_MID;
    node->id = pdu->mid;
    node->pdu = pdu;
    if (pdu->type == COAP_MESSAGE_CON && COAP_PROTO_NOT_RELIABLE(session->proto)) {
      uint8_t r;
      coap_prng(&r, sizeof(r));
      /* add timeout in range [ACK_TIMEOUT...ACK_TIMEOUT * ACK_RANDOM_FACTOR] */
      node->timeout = coap_calc_timeout(session, r);
    }
  }
  LL_APPEND(session->delayqueue, node);
  coap_log(LOG_DEBUG, "** %s: mid=0x%x: delayed\n",
           coap_session_str(session), node->id);
  return COAP_PDU_DELAYED;
}

#if !COAP_DISABLE_TCP
void coap_session_send_csm(coap_session_t *session) {
  coap_pdu_t *pdu;
  uint8_t buf[4];
  assert(COAP_PROTO_RELIABLE(session->proto));
  coap_log(LOG_DEBUG, "***%s: sending CSM\n", coap_session_str(session));
  session->state = COAP_SESSION_STATE_CSM;
  session->partial_write = 0;
  if (session->mtu == 0)
    session->mtu = COAP_DEFAULT_MTU;  /* base value */
  pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_SIGNALING_CODE_CSM, 0, 20);
  if ( pdu == NULL
    || coap_add_option_internal(pdu, COAP_SIGNALING_OPTION_MAX_MESSAGE_SIZE,
         coap_encode_var_safe(buf, sizeof(buf),
                              session->context->csm_max_message_size), buf) == 0
    || coap_add_option_internal(pdu, COAP_SIGNALING_OPTION_BLOCK_WISE_TRANSFER,
         coap_encode_var_safe(buf, sizeof(buf),
                                0), buf) == 0
    || coap_pdu_encode_header(pdu, session->proto) == 0
  ) {
    coap_session_disconnected(session, COAP_NACK_NOT_DELIVERABLE);
  } else {
    ssize_t bytes_written = coap_session_send_pdu(session, pdu);
    if (bytes_written != (ssize_t)pdu->used_size + pdu->hdr_size) {
      coap_session_disconnected(session, COAP_NACK_NOT_DELIVERABLE);
    } else {
      session->csm_rcv_mtu = session->context->csm_max_message_size;
      if (session->csm_rcv_mtu > COAP_BERT_BASE)
        session->csm_bert_loc_support = 1;
      else
        session->csm_bert_loc_support = 0;
    }
  }
  if (pdu)
    coap_delete_pdu(pdu);
}
#endif /* !COAP_DISABLE_TCP */

coap_mid_t coap_session_send_ping(coap_session_t *session) {
  coap_pdu_t *ping = NULL;
  if (session->state != COAP_SESSION_STATE_ESTABLISHED)
    return COAP_INVALID_MID;
  if (COAP_PROTO_NOT_RELIABLE(session->proto)) {
    uint16_t mid = coap_new_message_id (session);
    ping = coap_pdu_init(COAP_MESSAGE_CON, 0, mid, 0);
  }
#if !COAP_DISABLE_TCP
  else {
    ping = coap_pdu_init(COAP_MESSAGE_CON, COAP_SIGNALING_CODE_PING, 0, 1);
  }
#endif /* !COAP_DISABLE_TCP */
  if (!ping)
    return COAP_INVALID_MID;
  return coap_send_internal(session, ping);
}

void coap_session_connected(coap_session_t *session) {
  if (session->state != COAP_SESSION_STATE_ESTABLISHED) {
    coap_log(LOG_DEBUG, "***%s: session connected\n",
             coap_session_str(session));
    if (session->state == COAP_SESSION_STATE_CSM)
      coap_handle_event(session->context, COAP_EVENT_SESSION_CONNECTED, session);
    if (session->doing_first)
      session->doing_first = 0;
  }

  session->state = COAP_SESSION_STATE_ESTABLISHED;
  session->partial_write = 0;

  if ( session->proto==COAP_PROTO_DTLS) {
    session->tls_overhead = coap_dtls_get_overhead(session);
    if (session->tls_overhead >= session->mtu) {
      session->tls_overhead = session->mtu;
      coap_log(LOG_ERR, "DTLS overhead exceeds MTU\n");
    }
  }

  while (session->delayqueue && session->state == COAP_SESSION_STATE_ESTABLISHED) {
    ssize_t bytes_written;
    coap_queue_t *q = session->delayqueue;
    if (q->pdu->type == COAP_MESSAGE_CON && COAP_PROTO_NOT_RELIABLE(session->proto)) {
      if (session->con_active >= COAP_NSTART(session))
        break;
      session->con_active++;
    }
    /* Take entry off the queue */
    session->delayqueue = q->next;
    q->next = NULL;

    coap_log(LOG_DEBUG, "** %s: mid=0x%x: transmitted after delay\n",
             coap_session_str(session), (int)q->pdu->mid);
    bytes_written = coap_session_send_pdu(session, q->pdu);
    if (q->pdu->type == COAP_MESSAGE_CON && COAP_PROTO_NOT_RELIABLE(session->proto)) {
      if (coap_wait_ack(session->context, session, q) >= 0)
        q = NULL;
    }
    if (COAP_PROTO_NOT_RELIABLE(session->proto)) {
      if (q)
        coap_delete_node(q);
      if (bytes_written < 0)
        break;
    } else {
      if (bytes_written <= 0 || (size_t)bytes_written < q->pdu->used_size + q->pdu->hdr_size) {
        q->next = session->delayqueue;
        session->delayqueue = q;
        if (bytes_written > 0)
          session->partial_write = (size_t)bytes_written;
        break;
      } else {
        coap_delete_node(q);
      }
    }
  }
}

void coap_session_disconnected(coap_session_t *session, coap_nack_reason_t reason) {
#if !COAP_DISABLE_TCP
  coap_session_state_t state = session->state;
#endif /* !COAP_DISABLE_TCP */

  coap_log(LOG_DEBUG, "***%s: session disconnected (reason %d)\n",
           coap_session_str(session), reason);
#if COAP_SERVER_SUPPORT
  coap_delete_observers( session->context, session );
#endif /* COAP_SERVER_SUPPORT */

  if ( session->tls) {
    if (session->proto == COAP_PROTO_DTLS)
      coap_dtls_free_session(session);
#if !COAP_DISABLE_TCP
    else if (session->proto == COAP_PROTO_TLS)
      coap_tls_free_session(session);
#endif /* !COAP_DISABLE_TCP */
    session->tls = NULL;
  }

  if (session->proto == COAP_PROTO_UDP)
    session->state = COAP_SESSION_STATE_ESTABLISHED;
  else
    session->state = COAP_SESSION_STATE_NONE;

  session->con_active = 0;

  if (session->partial_pdu) {
    coap_delete_pdu(session->partial_pdu);
    session->partial_pdu = NULL;
  }
  session->partial_read = 0;

  while (session->delayqueue) {
    coap_queue_t *q = session->delayqueue;
    session->delayqueue = q->next;
    q->next = NULL;
    coap_log(LOG_DEBUG, "** %s: mid=0x%x: not transmitted after disconnect\n",
             coap_session_str(session), q->id);
    if (q->pdu->type==COAP_MESSAGE_CON
      && COAP_PROTO_NOT_RELIABLE(session->proto)
      && reason == COAP_NACK_ICMP_ISSUE)
    {
      /* Make sure that we try a re-transmit later on ICMP error */
      if (coap_wait_ack(session->context, session, q) >= 0) {
        if (session->context->nack_handler) {
          session->context->nack_handler(session, q->pdu, reason, q->id);
        }
        q = NULL;
      }
    }
    if (q && q->pdu->type == COAP_MESSAGE_CON
      && session->context->nack_handler)
    {
      session->context->nack_handler(session, q->pdu, reason, q->id);
    }
    if (q)
      coap_delete_node(q);
  }
  if (reason != COAP_NACK_ICMP_ISSUE) {
    coap_cancel_session_messages(session->context, session, reason);
  }
  else if (session->context->nack_handler) {
    coap_queue_t *q = session->context->sendqueue;
    while (q) {
      if (q->session == session) {
        session->context->nack_handler(session, q->pdu, reason, q->id);
      }
      q = q->next;
    }
  }

#if !COAP_DISABLE_TCP
  if (COAP_PROTO_RELIABLE(session->proto)) {
    if (session->sock.flags != COAP_SOCKET_EMPTY) {
      coap_socket_close(&session->sock);
      coap_handle_event(session->context,
        state == COAP_SESSION_STATE_CONNECTING ?
        COAP_EVENT_TCP_FAILED : COAP_EVENT_TCP_CLOSED, session);
    }
    if (state != COAP_SESSION_STATE_NONE) {
      coap_handle_event(session->context,
        state == COAP_SESSION_STATE_ESTABLISHED ?
        COAP_EVENT_SESSION_CLOSED : COAP_EVENT_SESSION_FAILED, session);
    }
    if (session->doing_first)
      session->doing_first = 0;
  }
#endif /* !COAP_DISABLE_TCP */
}

#if COAP_SERVER_SUPPORT
static void
coap_make_addr_hash(coap_addr_hash_t *addr_hash, coap_proto_t proto,
                    const coap_addr_tuple_t *addr_info) {
  memset(addr_hash, 0, sizeof(coap_addr_hash_t));
  coap_address_copy(&addr_hash->remote, &addr_info->remote);
  addr_hash->lport = coap_address_get_port(&addr_info->local);
  addr_hash->proto = proto;
}

coap_session_t *
coap_endpoint_get_session(coap_endpoint_t *endpoint,
  const coap_packet_t *packet, coap_tick_t now) {
  coap_session_t *session;
  coap_session_t *rtmp;
  unsigned int num_idle = 0;
  unsigned int num_hs = 0;
  coap_session_t *oldest = NULL;
  coap_session_t *oldest_hs = NULL;
  coap_addr_hash_t addr_hash;

  coap_make_addr_hash(&addr_hash, endpoint->proto, &packet->addr_info);
  SESSIONS_FIND(endpoint->sessions, addr_hash, session);
  if (session) {
    /* Maybe mcast or unicast IP address which is not in the hash */
    coap_address_copy(&session->addr_info.local, &packet->addr_info.local);
    session->ifindex = packet->ifindex;
    session->last_rx_tx = now;
    return session;
  }

  SESSIONS_ITER(endpoint->sessions, session, rtmp) {
    if (session->ref == 0 && session->delayqueue == NULL) {
      if (session->type == COAP_SESSION_TYPE_SERVER) {
        ++num_idle;
        if (oldest==NULL || session->last_rx_tx < oldest->last_rx_tx)
          oldest = session;

        if (session->state == COAP_SESSION_STATE_HANDSHAKE) {
          ++num_hs;
          /* See if this is a partial (D)TLS session set up
             which needs to be cleared down to prevent DOS */
          if ((session->last_rx_tx + COAP_PARTIAL_SESSION_TIMEOUT_TICKS) < now) {
            if (oldest_hs == NULL ||
                session->last_rx_tx < oldest_hs->last_rx_tx)
              oldest_hs = session;
          }
        }
      }
      else if (session->type == COAP_SESSION_TYPE_HELLO) {
        ++num_hs;
        /* See if this is a partial (D)TLS session set up for Client Hello
           which needs to be cleared down to prevent DOS */
        if ((session->last_rx_tx + COAP_PARTIAL_SESSION_TIMEOUT_TICKS) < now) {
          if (oldest_hs == NULL ||
              session->last_rx_tx < oldest_hs->last_rx_tx)
            oldest_hs = session;
        }
      }
    }
  }

  if (endpoint->context->max_idle_sessions > 0 &&
      num_idle >= endpoint->context->max_idle_sessions) {
    coap_handle_event(oldest->context, COAP_EVENT_SERVER_SESSION_DEL, oldest);
    coap_session_free(oldest);
  }
  else if (oldest_hs) {
    coap_log(LOG_WARNING, "***%s: Incomplete session timed out\n",
             coap_session_str(oldest_hs));
    coap_handle_event(oldest_hs->context, COAP_EVENT_SERVER_SESSION_DEL, oldest_hs);
    coap_session_free(oldest_hs);
  }

  if (num_hs > (endpoint->context->max_handshake_sessions ?
              endpoint->context->max_handshake_sessions :
              COAP_DEFAULT_MAX_HANDSHAKE_SESSIONS)) {
    /* Maxed out on number of sessions in (D)TLS negotiation state */
    coap_log(LOG_DEBUG,
             "Oustanding sessions in COAP_SESSION_STATE_HANDSHAKE too "
             "large.  New request ignored\n");
    return NULL;
  }

  if (endpoint->proto == COAP_PROTO_DTLS) {
    /*
     * Need to check that this actually is a Client Hello before wasting
     * time allocating and then freeing off session.
     */

    /*
     * Generic header structure of the DTLS record layer.
     * typedef struct __attribute__((__packed__)) {
     *   uint8_t content_type;           content type of the included message
     *   uint16_t version;               Protocol version
     *   uint16_t epoch;                 counter for cipher state changes
     *   uint8_t sequence_number[6];     sequence number
     *   uint16_t length;                length of the following fragment
     *   uint8_t handshake;              If content_type == DTLS_CT_HANDSHAKE
     * } dtls_record_handshake_t;
     */
#define OFF_CONTENT_TYPE      0  /* offset of content_type in dtls_record_handshake_t */
#define DTLS_CT_ALERT        21  /* Content Type Alert */
#define DTLS_CT_HANDSHAKE    22  /* Content Type Handshake */
#define OFF_HANDSHAKE_TYPE   13  /* offset of handshake in dtls_record_handshake_t */
#define DTLS_HT_CLIENT_HELLO  1  /* Client Hello handshake type */

#ifdef WITH_LWIP
    const uint8_t *payload = (const uint8_t*)packet->pbuf->payload;
    size_t length = packet->pbuf->len;
#else /* ! WITH_LWIP */
    const uint8_t *payload = (const uint8_t*)packet->payload;
    size_t length = packet->length;
#endif /* ! WITH_LWIP */
    if (length < (OFF_HANDSHAKE_TYPE + 1)) {
      coap_log(LOG_DEBUG,
         "coap_dtls_hello: ContentType %d Short Packet (%zu < %d) dropped\n",
         payload[OFF_CONTENT_TYPE], length,
         OFF_HANDSHAKE_TYPE + 1);
      return NULL;
    }
    if (payload[OFF_CONTENT_TYPE] != DTLS_CT_HANDSHAKE ||
        payload[OFF_HANDSHAKE_TYPE] != DTLS_HT_CLIENT_HELLO) {
      /* only log if not a late alert */
      if (payload[OFF_CONTENT_TYPE] != DTLS_CT_ALERT)
        coap_log(LOG_DEBUG,
         "coap_dtls_hello: ContentType %d Handshake %d dropped\n",
         payload[OFF_CONTENT_TYPE], payload[OFF_HANDSHAKE_TYPE]);
      return NULL;
    }
  }

  session = coap_make_session(endpoint->proto, COAP_SESSION_TYPE_SERVER,
                              &addr_hash, &packet->addr_info.local,
                              &packet->addr_info.remote,
                              packet->ifindex, endpoint->context, endpoint);
  if (session) {
    session->last_rx_tx = now;
    if (endpoint->proto == COAP_PROTO_UDP)
      session->state = COAP_SESSION_STATE_ESTABLISHED;
    else if (endpoint->proto == COAP_PROTO_DTLS) {
      session->type = COAP_SESSION_TYPE_HELLO;
    }
    SESSIONS_ADD(endpoint->sessions, session);
    coap_log(LOG_DEBUG, "***%s: session %p: new incoming session\n",
             coap_session_str(session), (void *)session);
    coap_handle_event(session->context, COAP_EVENT_SERVER_SESSION_NEW, session);
  }
  return session;
}

coap_session_t *
coap_session_new_dtls_session(coap_session_t *session,
  coap_tick_t now) {
  if (session) {
    session->last_rx_tx = now;
    session->type = COAP_SESSION_TYPE_SERVER;
    session->tls = coap_dtls_new_server_session(session);
    if (session->tls) {
      session->state = COAP_SESSION_STATE_HANDSHAKE;
    } else {
      coap_session_free(session);
      session = NULL;
    }
  }
  return session;
}
#endif /* COAP_SERVER_SUPPORT */

#ifdef COAP_EPOLL_SUPPORT
static void
coap_epoll_ctl_add(coap_socket_t *sock,
                   uint32_t events,
                   const char *func
) {
  int ret;
  struct epoll_event event;
  coap_context_t *context;

  if (sock == NULL)
    return;

#if COAP_SERVER_SUPPORT
  context = sock->session ? sock->session->context :
                            sock->endpoint ? sock->endpoint->context : NULL;
#else /* ! COAP_SERVER_SUPPORT */
  context = sock->session ? sock->session->context : NULL;
#endif /* ! COAP_SERVER_SUPPORT */
  if (context == NULL)
    return;

  /* Needed if running 32bit as ptr is only 32bit */
  memset(&event, 0, sizeof(event));
  event.events = events;
  event.data.ptr = sock;

  ret = epoll_ctl(context->epfd, EPOLL_CTL_ADD, sock->fd, &event);
  if (ret == -1) {
     coap_log(LOG_ERR,
              "%s: epoll_ctl ADD failed: %s (%d)\n",
              func,
              coap_socket_strerror(), errno);
  }
}
#endif /* COAP_EPOLL_SUPPORT */

#if COAP_CLIENT_SUPPORT
static coap_session_t *
coap_session_create_client(
  coap_context_t *ctx,
  const coap_address_t *local_if,
  const coap_address_t *server,
  coap_proto_t proto
) {
  coap_session_t *session = NULL;

  assert(server);

  switch(proto) {
  case COAP_PROTO_UDP:
    break;
  case COAP_PROTO_DTLS:
    if (!coap_dtls_is_supported()) {
      coap_log(LOG_CRIT, "coap_new_client_session*: DTLS not supported\n");
      return NULL;
    }
    break;
  case COAP_PROTO_TCP:
    if (!coap_tcp_is_supported()) {
      coap_log(LOG_CRIT, "coap_new_client_session*: TCP not supported\n");
      return NULL;
    }
    break;
  case COAP_PROTO_TLS:
    if (!coap_tls_is_supported()) {
      coap_log(LOG_CRIT, "coap_new_client_session*: TLS not supported\n");
      return NULL;
    }
    break;
  case COAP_PROTO_NONE:
  default:
    assert(0);
    break;
  }
  session = coap_make_session(proto, COAP_SESSION_TYPE_CLIENT, NULL,
    local_if, server, 0, ctx, NULL);
  if (!session)
    goto error;

  coap_session_reference(session);

  if (proto == COAP_PROTO_UDP || proto == COAP_PROTO_DTLS) {
    coap_session_t *s, *rtmp;
    if (!coap_socket_connect_udp(&session->sock, local_if, server,
      proto == COAP_PROTO_DTLS ? COAPS_DEFAULT_PORT : COAP_DEFAULT_PORT,
      &session->addr_info.local, &session->addr_info.remote)) {
      goto error;
    }
    /* Check that this is not a duplicate 4-tuple */
    SESSIONS_ITER_SAFE(ctx->sessions, s, rtmp) {
      if ((s->proto == COAP_PROTO_UDP || s->proto == COAP_PROTO_DTLS) &&
          coap_address_equals(&session->addr_info.local,
                              &s->addr_info.local) &&
          coap_address_equals(&session->addr_info.remote,
                              &s->addr_info.remote)) {
        coap_log(LOG_WARNING, "***%s: session %p: duplicate - already exists\n",
                 coap_session_str(session), (void *)session);
        goto error;
      }
    }
#if !COAP_DISABLE_TCP
  } else if (proto == COAP_PROTO_TCP || proto == COAP_PROTO_TLS) {
    if (!coap_socket_connect_tcp1(&session->sock, local_if, server,
      proto == COAP_PROTO_TLS ? COAPS_DEFAULT_PORT : COAP_DEFAULT_PORT,
      &session->addr_info.local, &session->addr_info.remote)) {
      goto error;
    }
#endif /* !COAP_DISABLE_TCP */
  }

#ifdef COAP_EPOLL_SUPPORT
  session->sock.session = session;
  coap_epoll_ctl_add(&session->sock,
                     EPOLLIN |
                      ((session->sock.flags & COAP_SOCKET_WANT_CONNECT) ?
                       EPOLLOUT : 0),
                   __func__);
#endif /* COAP_EPOLL_SUPPORT */

  session->sock.flags |= COAP_SOCKET_NOT_EMPTY | COAP_SOCKET_WANT_READ;
  if (local_if)
    session->sock.flags |= COAP_SOCKET_BOUND;
#if COAP_SERVER_SUPPORT
  if (ctx->proxy_uri_resource)
    session->proxy_session = 1;
#endif /* COAP_SERVER_SUPPORT */
  SESSIONS_ADD(ctx->sessions, session);
  return session;

error:
  /*
   * Need to add in the session as coap_session_release()
   * will call SESSIONS_DELETE in coap_session_free().
   */
  if (session)
    SESSIONS_ADD(ctx->sessions, session);
  coap_session_release(session);
  return NULL;
}

static coap_session_t *
coap_session_connect(coap_session_t *session) {
  if (session->proto == COAP_PROTO_UDP) {
    session->state = COAP_SESSION_STATE_ESTABLISHED;
  } else if (session->proto == COAP_PROTO_DTLS) {
    session->tls = coap_dtls_new_client_session(session);
    if (session->tls) {
      session->state = COAP_SESSION_STATE_HANDSHAKE;
    } else {
      /* Need to free session object. As a new session may not yet
       * have been referenced, we call coap_session_reference() first
       * before trying to release the object.
       */
      coap_session_reference(session);
      coap_session_release(session);
      return NULL;
    }
#if !COAP_DISABLE_TCP
  } else {
    if (session->proto == COAP_PROTO_TCP || session->proto == COAP_PROTO_TLS) {
      if (session->sock.flags & COAP_SOCKET_WANT_CONNECT) {
        session->state = COAP_SESSION_STATE_CONNECTING;
        if (session->state != COAP_SESSION_STATE_ESTABLISHED &&
               session->state != COAP_SESSION_STATE_NONE &&
               COAP_PROTO_RELIABLE(session->proto) &&
               session->type == COAP_SESSION_TYPE_CLIENT) {
          session->doing_first = 1;
        }
      } else if (session->proto == COAP_PROTO_TLS) {
        int connected = 0;
        session->tls = coap_tls_new_client_session(session, &connected);
        if (session->tls) {
          session->state = COAP_SESSION_STATE_HANDSHAKE;
          if (connected)
            coap_session_send_csm(session);
        } else {
          /* Need to free session object. As a new session may not yet
           * have been referenced, we call coap_session_reference()
           * first before trying to release the object.
           */
          coap_session_reference(session);
          coap_session_release(session);
          return NULL;
        }
      } else {
        coap_session_send_csm(session);
      }
    }
#endif /* !COAP_DISABLE_TCP */
  }
  coap_ticks(&session->last_rx_tx);
  return session;
}
#endif /* COAP_CLIENT_SUPPORT */

#if COAP_SERVER_SUPPORT
static coap_session_t *
coap_session_accept(coap_session_t *session) {
#if !COAP_DISABLE_TCP
  if (session->proto == COAP_PROTO_TCP || session->proto == COAP_PROTO_TLS)
    coap_handle_event(session->context, COAP_EVENT_TCP_CONNECTED, session);
  if (session->proto == COAP_PROTO_TCP) {
    coap_session_send_csm(session);
  } else if (session->proto == COAP_PROTO_TLS) {
    int connected = 0;
    session->tls = coap_tls_new_server_session(session, &connected);
    if (session->tls) {
      session->state = COAP_SESSION_STATE_HANDSHAKE;
      if (connected) {
        coap_handle_event(session->context, COAP_EVENT_DTLS_CONNECTED, session);
        coap_session_send_csm(session);
      }
    } else {
      /* Need to free session object. As a new session may not yet
       * have been referenced, we call coap_session_reference() first
       * before trying to release the object.
       */
      coap_session_reference(session);
      coap_session_release(session);
      session = NULL;
    }
  }
#endif /* COAP_DISABLE_TCP */
  return session;
}
#endif /* COAP_SERVER_SUPPORT */

#if COAP_CLIENT_SUPPORT
coap_session_t *coap_new_client_session(
  coap_context_t *ctx,
  const coap_address_t *local_if,
  const coap_address_t *server,
  coap_proto_t proto
) {
  coap_session_t *session = coap_session_create_client(ctx, local_if, server,
                                                       proto);
  if (session) {
    coap_log(LOG_DEBUG, "***%s: session %p: created outgoing session\n",
             coap_session_str(session), (void *)session);
    session = coap_session_connect(session);
  }
  return session;
}

coap_session_t *coap_new_client_session_psk(
  coap_context_t *ctx,
  const coap_address_t *local_if,
  const coap_address_t *server,
  coap_proto_t proto,
  const char *identity,
  const uint8_t *key,
  unsigned key_len
) {
  coap_dtls_cpsk_t setup_data;

  memset (&setup_data, 0, sizeof(setup_data));
  setup_data.version = COAP_DTLS_CPSK_SETUP_VERSION;

  if (identity) {
    setup_data.psk_info.identity.s = (const uint8_t *)identity;
    setup_data.psk_info.identity.length = strlen(identity);
  }

  if (key && key_len > 0) {
    setup_data.psk_info.key.s = key;
    setup_data.psk_info.key.length = key_len;
  }

  return coap_new_client_session_psk2(ctx, local_if, server,
                                      proto, &setup_data);
}

coap_session_t *coap_new_client_session_psk2(
  coap_context_t *ctx,
  const coap_address_t *local_if,
  const coap_address_t *server,
  coap_proto_t proto,
  coap_dtls_cpsk_t *setup_data
) {
  coap_session_t *session = coap_session_create_client(ctx, local_if,
                                                       server, proto);

  if (!session)
    return NULL;

  session->cpsk_setup_data = *setup_data;
  if (setup_data->psk_info.identity.s) {
    session->psk_identity =
                      coap_new_bin_const(setup_data->psk_info.identity.s,
                                         setup_data->psk_info.identity.length);
    if (!session->psk_identity) {
      coap_log(LOG_WARNING, "Cannot store session Identity (PSK)\n");
      coap_session_release(session);
      return NULL;
    }
  }
  else if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    coap_log(LOG_WARNING, "Identity (PSK) not defined\n");
    coap_session_release(session);
    return NULL;
  }

  if (setup_data->psk_info.key.s && setup_data->psk_info.key.length > 0) {
    session->psk_key = coap_new_bin_const(setup_data->psk_info.key.s,
                                          setup_data->psk_info.key.length);
    if (!session->psk_key) {
      coap_log(LOG_WARNING, "Cannot store session pre-shared key (PSK)\n");
      coap_session_release(session);
      return NULL;
    }
  }
  else if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    coap_log(LOG_WARNING, "Pre-shared key (PSK) not defined\n");
    coap_session_release(session);
    return NULL;
  }

  if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    if (!coap_dtls_context_set_cpsk(ctx, setup_data)) {
      coap_session_release(session);
      return NULL;
    }
  }
  coap_log(LOG_DEBUG, "***%s: new outgoing session\n",
           coap_session_str(session));
  return coap_session_connect(session);
}
#endif /* ! COAP_CLIENT_SUPPORT */

int
coap_session_refresh_psk_hint(coap_session_t *session,
  const coap_bin_const_t *psk_hint
) {
  /* We may be refreshing the hint with the same hint */
  coap_bin_const_t *old_psk_hint = session->psk_hint;

  if (psk_hint && psk_hint->s) {
    if (session->psk_hint) {
      if (coap_binary_equal(session->psk_hint, psk_hint))
        return 1;
    }
    session->psk_hint = coap_new_bin_const(psk_hint->s,
                                           psk_hint->length);
    if (!session->psk_hint) {
      coap_log(LOG_ERR, "No memory to store identity hint (PSK)\n");
      if (old_psk_hint)
        coap_delete_bin_const(old_psk_hint);
      return 0;
    }
  }
  else {
    session->psk_hint = NULL;
  }
  if (old_psk_hint)
    coap_delete_bin_const(old_psk_hint);

  return 1;
}

int
coap_session_refresh_psk_key(coap_session_t *session,
  const coap_bin_const_t *psk_key
) {
  /* We may be refreshing the key with the same key */
  coap_bin_const_t *old_psk_key = session->psk_key;

  if (psk_key && psk_key->s) {
    if (session->psk_key) {
      if (coap_binary_equal(session->psk_key, psk_key))
        return 1;
    }
    session->psk_key = coap_new_bin_const(psk_key->s, psk_key->length);
    if (!session->psk_key) {
      coap_log(LOG_ERR, "No memory to store pre-shared key (PSK)\n");
      if (old_psk_key)
        coap_delete_bin_const(old_psk_key);
      return 0;
    }
  }
  else {
    session->psk_key = NULL;
  }
  if (old_psk_key)
    coap_delete_bin_const(old_psk_key);

  return 1;
}

int
coap_session_refresh_psk_identity(coap_session_t *session,
  const coap_bin_const_t *psk_identity
) {
  /* We may be refreshing the identity with the same identity */
  coap_bin_const_t *old_psk_identity = session->psk_identity;

  if (psk_identity && psk_identity->s) {
    if (session->psk_identity) {
      if (coap_binary_equal(session->psk_identity, psk_identity))
        return 1;
    }
    session->psk_identity = coap_new_bin_const(psk_identity->s,
                                               psk_identity->length);
    if (!session->psk_identity) {
      coap_log(LOG_ERR, "No memory to store pre-shared key identity (PSK)\n");
      if (old_psk_identity)
        coap_delete_bin_const(old_psk_identity);
      return 0;
    }
  }
  else {
    session->psk_identity = NULL;
  }
  if (old_psk_identity)
    coap_delete_bin_const(old_psk_identity);

  return 1;
}

#if COAP_SERVER_SUPPORT
const coap_bin_const_t *
coap_session_get_psk_hint(const coap_session_t *session) {
  if (session)
    return session->psk_hint;
  return NULL;
}
#endif /* COAP_SERVER_SUPPORT */

const coap_bin_const_t *
coap_session_get_psk_identity(const coap_session_t *session) {
  const coap_bin_const_t *psk_identity = NULL;
  if (session) {
    psk_identity = session->psk_identity;
    if (psk_identity == NULL) {
      psk_identity = &session->cpsk_setup_data.psk_info.identity;
    }
  }
  return psk_identity;
}

const coap_bin_const_t *
coap_session_get_psk_key(const coap_session_t *session) {
  if (session)
    return session->psk_key;
  return NULL;
}

#if COAP_CLIENT_SUPPORT
coap_session_t *coap_new_client_session_pki(
  coap_context_t *ctx,
  const coap_address_t *local_if,
  const coap_address_t *server,
  coap_proto_t proto,
  coap_dtls_pki_t* setup_data
) {
  coap_session_t *session;

  if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    if (!setup_data) {
      return NULL;
    } else {
      if (setup_data->version != COAP_DTLS_PKI_SETUP_VERSION) {
        coap_log(LOG_ERR,
                 "coap_new_client_session_pki: Wrong version of setup_data\n");
        return NULL;
      }
    }

  }
  session = coap_session_create_client(ctx, local_if, server, proto);

  if (!session) {
    return NULL;
  }

  if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    /* we know that setup_data is not NULL */
    if (!coap_dtls_context_set_pki(ctx, setup_data, COAP_DTLS_ROLE_CLIENT)) {
      coap_session_release(session);
      return NULL;
    }
  }
  coap_log(LOG_DEBUG, "***%s: new outgoing session\n",
           coap_session_str(session));
  return coap_session_connect(session);
}
#endif /* ! COAP_CLIENT_SUPPORT */

#if COAP_SERVER_SUPPORT
coap_session_t *coap_new_server_session(
  coap_context_t *ctx,
  coap_endpoint_t *ep
) {
  coap_session_t *session;
  session = coap_make_session( ep->proto, COAP_SESSION_TYPE_SERVER,
                               NULL, NULL, NULL, 0, ctx, ep );
  if (!session)
    goto error;

#if !COAP_DISABLE_TCP
  if (!coap_socket_accept_tcp(&ep->sock, &session->sock,
                              &session->addr_info.local,
                              &session->addr_info.remote))
    goto error;
  coap_make_addr_hash(&session->addr_hash, session->proto, &session->addr_info);

#endif /* !COAP_DISABLE_TCP */
  session->sock.flags |= COAP_SOCKET_NOT_EMPTY | COAP_SOCKET_CONNECTED
                       | COAP_SOCKET_WANT_READ;
#ifdef COAP_EPOLL_SUPPORT
  session->sock.session = session;
  coap_epoll_ctl_add(&session->sock,
                     EPOLLIN,
                   __func__);
#endif /* COAP_EPOLL_SUPPORT */
  SESSIONS_ADD(ep->sessions, session);
  if (session) {
    coap_log(LOG_DEBUG, "***%s: session %p: new incoming session\n",
             coap_session_str(session), (void *)session);
    /* Returned session may already have been released and is now NULL */
    session = coap_session_accept(session);
    if(session) {
      coap_handle_event(session->context, COAP_EVENT_SERVER_SESSION_NEW, session);
    }
  }
  return session;

error:
  /*
   * Need to add in the session as coap_session_release()
   * will call SESSIONS_DELETE in coap_session_free().
   */
  if (session) {
    SESSIONS_ADD(ep->sessions, session);
    coap_session_free(session);
  }
  return NULL;
}
#endif /* COAP_SERVER_SUPPORT */

void
coap_session_init_token(coap_session_t *session, size_t len,
                             const uint8_t *data) {
  session->tx_token = coap_decode_var_bytes8(data, len);
}

void coap_session_new_token(coap_session_t *session, size_t *len,
                                      uint8_t *data) {
  *len = coap_encode_var_safe8(data,
                               sizeof(session->tx_token), ++session->tx_token);
}

uint16_t
coap_new_message_id(coap_session_t *session) {
  return ++session->tx_mid;
}

const coap_address_t *
coap_session_get_addr_remote(const coap_session_t *session) {
  if (session)
    return &session->addr_info.remote;
  return NULL;
}

const coap_address_t *
coap_session_get_addr_local(const coap_session_t *session) {
  if (session)
    return &session->addr_info.local;
  return NULL;
}

coap_context_t *
coap_session_get_context(const coap_session_t *session) {
  if (session)
    return session->context;
  return NULL;
}

coap_proto_t
coap_session_get_proto(const coap_session_t *session) {
  if (session)
    return session->proto;
  return 0;
}

coap_session_type_t
coap_session_get_type(const coap_session_t *session) {
  if (session)
    return session->type;
  return 0;
}

#if COAP_CLIENT_SUPPORT
int
coap_session_set_type_client(coap_session_t *session) {
#if COAP_SERVER_SUPPORT
  if (session && session->type == COAP_SESSION_TYPE_SERVER) {
    coap_session_reference(session);
    session->type = COAP_SESSION_TYPE_CLIENT;
    return 1;
  }
#else /* ! COAP_SERVER_SUPPORT */
  (void)session;
#endif /* ! COAP_SERVER_SUPPORT */
  return 0;
}
#endif /* COAP_CLIENT_SUPPORT */

coap_session_state_t
coap_session_get_state(const coap_session_t *session) {
  if (session)
    return session->state;
  return 0;
}

int coap_session_get_ifindex(const coap_session_t *session) {
  if (session)
    return session->ifindex;
  return -1;
}

void *coap_session_get_tls(const coap_session_t *session,
                           coap_tls_library_t *tls_lib) {
  if (session)
    return coap_dtls_get_tls(session, tls_lib);
  return NULL;
}

#ifndef WITH_LWIP
#if COAP_SERVER_SUPPORT
coap_endpoint_t *
coap_new_endpoint(coap_context_t *context, const coap_address_t *listen_addr, coap_proto_t proto) {
  coap_endpoint_t *ep = NULL;

  assert(context);
  assert(listen_addr);
  assert(proto != COAP_PROTO_NONE);

  if (proto == COAP_PROTO_DTLS && !coap_dtls_is_supported()) {
    coap_log(LOG_CRIT, "coap_new_endpoint: DTLS not supported\n");
    goto error;
  }

  if (proto == COAP_PROTO_TLS && !coap_tls_is_supported()) {
    coap_log(LOG_CRIT, "coap_new_endpoint: TLS not supported\n");
    goto error;
  }

  if (proto == COAP_PROTO_TCP && !coap_tcp_is_supported()) {
    coap_log(LOG_CRIT, "coap_new_endpoint: TCP not supported\n");
    goto error;
  }

  if (proto == COAP_PROTO_DTLS || proto == COAP_PROTO_TLS) {
    if (!coap_dtls_context_check_keys_enabled(context)) {
      coap_log(LOG_INFO,
               "coap_new_endpoint: one of coap_context_set_psk() or "
               "coap_context_set_pki() not called\n");
      goto error;
    }
  }

  ep = coap_malloc_endpoint();
  if (!ep) {
    coap_log(LOG_WARNING, "coap_new_endpoint: malloc");
    goto error;
  }

  memset(ep, 0, sizeof(coap_endpoint_t));
  ep->context = context;
  ep->proto = proto;

  if (proto==COAP_PROTO_UDP || proto==COAP_PROTO_DTLS) {
    if (!coap_socket_bind_udp(&ep->sock, listen_addr, &ep->bind_addr))
      goto error;
    ep->sock.flags |= COAP_SOCKET_WANT_READ;
#if !COAP_DISABLE_TCP
  } else if (proto==COAP_PROTO_TCP || proto==COAP_PROTO_TLS) {
    if (!coap_socket_bind_tcp(&ep->sock, listen_addr, &ep->bind_addr))
      goto error;
    ep->sock.flags |= COAP_SOCKET_WANT_ACCEPT;
#endif /* !COAP_DISABLE_TCP */
  } else {
    coap_log(LOG_CRIT, "coap_new_endpoint: protocol not supported\n");
    goto error;
  }

  if (LOG_DEBUG <= coap_get_log_level()) {
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 40
#endif
    unsigned char addr_str[INET6_ADDRSTRLEN + 8];

    if (coap_print_addr(&ep->bind_addr, addr_str, INET6_ADDRSTRLEN + 8)) {
      coap_log(LOG_DEBUG, "created %s endpoint %s\n",
          ep->proto == COAP_PROTO_TLS ? "TLS "
        : ep->proto == COAP_PROTO_TCP ? "TCP "
        : ep->proto == COAP_PROTO_DTLS ? "DTLS" : "UDP ",
        addr_str);
    }
  }

  ep->sock.flags |= COAP_SOCKET_NOT_EMPTY | COAP_SOCKET_BOUND;

  ep->default_mtu = COAP_DEFAULT_MTU;

#ifdef COAP_EPOLL_SUPPORT
  ep->sock.endpoint = ep;
  coap_epoll_ctl_add(&ep->sock,
                     EPOLLIN,
                   __func__);
#endif /* COAP_EPOLL_SUPPORT */

  LL_PREPEND(context->endpoint, ep);
  return ep;

error:
  coap_free_endpoint(ep);
  return NULL;
}

void coap_endpoint_set_default_mtu(coap_endpoint_t *ep, unsigned mtu) {
  ep->default_mtu = (uint16_t)mtu;
}

void
coap_free_endpoint(coap_endpoint_t *ep) {
  if (ep) {
    coap_session_t *session, *rtmp;

    SESSIONS_ITER_SAFE(ep->sessions, session, rtmp) {
      assert(session->ref == 0);
      if (session->ref == 0) {
        coap_session_free(session);
      }
    }
    if (ep->sock.flags != COAP_SOCKET_EMPTY) {
      /*
       * ep->sock.endpoint is set in coap_new_endpoint().
       * ep->sock.session is never set.
       *
       * session->sock.session is set for both clients and servers (when a
       * new session is accepted), but does not affect the endpoint.
       *
       * So, it is safe to call coap_socket_close() after all the sessions
       * have been freed above as we are only working with the endpoint sock.
       */
#ifdef COAP_EPOLL_SUPPORT
       assert(ep->sock.session == NULL);
#endif /* COAP_EPOLL_SUPPORT */
      coap_socket_close(&ep->sock);
    }

    if (ep->context && ep->context->endpoint) {
      LL_DELETE(ep->context->endpoint, ep);
    }
    coap_mfree_endpoint(ep);
  }
}
#endif /* COAP_SERVER_SUPPORT */
#endif /* WITH_LWIP */

coap_session_t *
coap_session_get_by_peer(const coap_context_t *ctx,
  const coap_address_t *remote_addr,
  int ifindex) {
  coap_session_t *s, *rtmp;
#if COAP_CLIENT_SUPPORT
  SESSIONS_ITER(ctx->sessions, s, rtmp) {
    if (s->ifindex == ifindex && coap_address_equals(&s->addr_info.remote,
                                                     remote_addr))
      return s;
  }
#endif /* COAP_CLIENT_SUPPORT */
#if COAP_SERVER_SUPPORT
  coap_endpoint_t *ep;

  LL_FOREACH(ctx->endpoint, ep) {
    SESSIONS_ITER(ep->sessions, s, rtmp) {
      if (s->ifindex == ifindex && coap_address_equals(&s->addr_info.remote,
                                                       remote_addr))
        return s;
    }
  }
#endif /* COAP_SERVER_SUPPORT */
  return NULL;
}

const char *coap_session_str(const coap_session_t *session) {
  static char szSession[2 * (INET6_ADDRSTRLEN + 8) + 24];
  char *p = szSession, *end = szSession + sizeof(szSession);
  if (coap_print_addr(&session->addr_info.local,
                      (unsigned char*)p, end - p) > 0)
    p += strlen(p);
  if (p + 6 < end) {
    strcpy(p, " <-> ");
    p += 5;
  }
  if (p + 1 < end) {
    if (coap_print_addr(&session->addr_info.remote,
                        (unsigned char*)p, end - p) > 0)
      p += strlen(p);
  }
  if (session->ifindex > 0 && p + 1 < end)
    p += snprintf(p, end - p, " (if%d)", session->ifindex);
  if (p + 6 < end) {
    if (session->proto == COAP_PROTO_UDP) {
      strcpy(p, " UDP ");
      p += 4;
    } else if (session->proto == COAP_PROTO_DTLS) {
      strcpy(p, " DTLS");
      p += 5;
    } else if (session->proto == COAP_PROTO_TCP) {
      strcpy(p, " TCP ");
      p += 4;
    } else if (session->proto == COAP_PROTO_TLS) {
      strcpy(p, " TLS ");
      p += 4;
    } else {
      strcpy(p, " NONE");
      p += 5;
    }
  }

  return szSession;
}

#if COAP_SERVER_SUPPORT
const char *coap_endpoint_str(const coap_endpoint_t *endpoint) {
  static char szEndpoint[128];
  char *p = szEndpoint, *end = szEndpoint + sizeof(szEndpoint);
  if (coap_print_addr(&endpoint->bind_addr, (unsigned char*)p, end - p) > 0)
    p += strlen(p);
  if (p + 6 < end) {
    if (endpoint->proto == COAP_PROTO_UDP) {
      strcpy(p, " UDP");
      p += 4;
    } else if (endpoint->proto == COAP_PROTO_DTLS) {
      strcpy(p, " DTLS");
      p += 5;
    } else {
      strcpy(p, " NONE");
      p += 5;
    }
  }

  return szEndpoint;
}
#endif /* COAP_SERVER_SUPPORT */
#ifdef COAP_CLIENT_SUPPORT
void
coap_session_set_no_observe_cancel(coap_session_t *session) {
  session->no_observe_cancel = 1;
}
#endif /* COAP_CLIENT_SUPPORT */
#endif  /* COAP_SESSION_C_ */
