/* net.c -- CoAP context inteface
 *
 * Copyright (C) 2010--2022 Olaf Bergmann <bergmann@tzi.org> and others
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * This file is part of the CoAP library libcoap. Please see
 * README for terms of use.
 */

/**
 * @file net.c
 * @brief CoAP context functions
 */

#include "coap3/coap_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#else
#ifdef HAVE_SYS_UNISTD_H
#include <sys/unistd.h>
#endif
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif
#ifdef COAP_EPOLL_SUPPORT
#include <sys/epoll.h>
#include <sys/timerfd.h>
#endif /* COAP_EPOLL_SUPPORT */
#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef WITH_LWIP
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <lwip/timeouts.h>
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 40
#endif

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

      /**
       * The number of bits for the fractional part of ACK_TIMEOUT and
       * ACK_RANDOM_FACTOR. Must be less or equal 8.
       */
#define FRAC_BITS 6

       /**
        * The maximum number of bits for fixed point integers that are used
        * for retransmission time calculation. Currently this must be @c 8.
        */
#define MAX_BITS 8

#if FRAC_BITS > 8
#error FRAC_BITS must be less or equal 8
#endif

        /** creates a Qx.frac from fval in coap_fixed_point_t */
#define Q(frac,fval) ((uint16_t)(((1 << (frac)) * fval.integer_part) + \
                      ((1 << (frac)) * fval.fractional_part + 500)/1000))

/** creates a Qx.FRAC_BITS from session's 'ack_random_factor' */
#define ACK_RANDOM_FACTOR                                        \
  Q(FRAC_BITS, session->ack_random_factor)

/** creates a Qx.FRAC_BITS from session's 'ack_timeout' */
#define ACK_TIMEOUT Q(FRAC_BITS, session->ack_timeout)

#if !defined(WITH_LWIP) && !defined(WITH_CONTIKI)

COAP_STATIC_INLINE coap_queue_t *
coap_malloc_node(void) {
  return (coap_queue_t *)coap_malloc_type(COAP_NODE, sizeof(coap_queue_t));
}

COAP_STATIC_INLINE void
coap_free_node(coap_queue_t *node) {
  coap_free_type(COAP_NODE, node);
}
#endif /* !defined(WITH_LWIP) && !defined(WITH_CONTIKI) */

#ifdef WITH_LWIP

#include <lwip/memp.h>

static void coap_retransmittimer_execute(void *arg);
static void coap_retransmittimer_restart(coap_context_t *ctx);

COAP_STATIC_INLINE coap_queue_t *
coap_malloc_node() {
  return (coap_queue_t *)memp_malloc(MEMP_COAP_NODE);
}

COAP_STATIC_INLINE void
coap_free_node(coap_queue_t *node) {
  memp_free(MEMP_COAP_NODE, node);
}

#endif /* WITH_LWIP */
#ifdef WITH_CONTIKI
# ifndef DEBUG
#  define DEBUG DEBUG_PRINT
# endif /* DEBUG */

#include "net/ip/uip-debug.h"

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF  ((struct uip_udp_hdr *)&uip_buf[UIP_LLIPH_LEN])

void coap_resources_init();

unsigned char initialized = 0;
coap_context_t the_coap_context;

PROCESS(coap_retransmit_process, "message retransmit process");

COAP_STATIC_INLINE coap_queue_t *
coap_malloc_node() {
  return (coap_queue_t *)coap_malloc_type(COAP_NODE, 0);
}

COAP_STATIC_INLINE void
coap_free_node(coap_queue_t *node) {
  coap_free_type(COAP_NODE, node);
}
#endif /* WITH_CONTIKI */

unsigned int
coap_adjust_basetime(coap_context_t *ctx, coap_tick_t now) {
  unsigned int result = 0;
  coap_tick_diff_t delta = now - ctx->sendqueue_basetime;

  if (ctx->sendqueue) {
    /* delta < 0 means that the new time stamp is before the old. */
    if (delta <= 0) {
      ctx->sendqueue->t -= delta;
    } else {
      /* This case is more complex: The time must be advanced forward,
       * thus possibly leading to timed out elements at the queue's
       * start. For every element that has timed out, its relative
       * time is set to zero and the result counter is increased. */

      coap_queue_t *q = ctx->sendqueue;
      coap_tick_t t = 0;
      while (q && (t + q->t < (coap_tick_t)delta)) {
        t += q->t;
        q->t = 0;
        result++;
        q = q->next;
      }

      /* finally adjust the first element that has not expired */
      if (q) {
        q->t = (coap_tick_t)delta - t;
      }
    }
  }

  /* adjust basetime */
  ctx->sendqueue_basetime += delta;

  return result;
}

int
coap_insert_node(coap_queue_t **queue, coap_queue_t *node) {
  coap_queue_t *p, *q;
  if (!queue || !node)
    return 0;

  /* set queue head if empty */
  if (!*queue) {
    *queue = node;
    return 1;
  }

  /* replace queue head if PDU's time is less than head's time */
  q = *queue;
  if (node->t < q->t) {
    node->next = q;
    *queue = node;
    q->t -= node->t;                /* make q->t relative to node->t */
    return 1;
  }

  /* search for right place to insert */
  do {
    node->t -= q->t;                /* make node-> relative to q->t */
    p = q;
    q = q->next;
  } while (q && q->t <= node->t);

  /* insert new item */
  if (q) {
    q->t -= node->t;                /* make q->t relative to node->t */
  }
  node->next = q;
  p->next = node;
  return 1;
}

int
coap_delete_node(coap_queue_t *node) {
  if (!node)
    return 0;

  coap_delete_pdu(node->pdu);
  if ( node->session ) {
    /*
     * Need to remove out of context->sendqueue as added in by coap_wait_ack()
     */
    if (node->session->context->sendqueue) {
      LL_DELETE(node->session->context->sendqueue, node);
    }
    coap_session_release(node->session);
  }
  coap_free_node(node);

  return 1;
}

void
coap_delete_all(coap_queue_t *queue) {
  if (!queue)
    return;

  coap_delete_all(queue->next);
  coap_delete_node(queue);
}

coap_queue_t *
coap_new_node(void) {
  coap_queue_t *node;
  node = coap_malloc_node();

  if (!node) {
    coap_log(LOG_WARNING, "coap_new_node: malloc failed\n");
    return NULL;
  }

  memset(node, 0, sizeof(*node));
  return node;
}

coap_queue_t *
coap_peek_next(coap_context_t *context) {
  if (!context || !context->sendqueue)
    return NULL;

  return context->sendqueue;
}

coap_queue_t *
coap_pop_next(coap_context_t *context) {
  coap_queue_t *next;

  if (!context || !context->sendqueue)
    return NULL;

  next = context->sendqueue;
  context->sendqueue = context->sendqueue->next;
  if (context->sendqueue) {
    context->sendqueue->t += next->t;
  }
  next->next = NULL;
  return next;
}

#if COAP_CLIENT_SUPPORT
const coap_bin_const_t *
coap_get_session_client_psk_key(const coap_session_t *session) {

  if (session->psk_key) {
    return session->psk_key;
  }
  if (session->cpsk_setup_data.psk_info.key.length)
    return &session->cpsk_setup_data.psk_info.key;

  /* Not defined in coap_new_client_session_psk2() */
  return NULL;
}
#endif /* COAP_CLIENT_SUPPORT */

const coap_bin_const_t *
coap_get_session_client_psk_identity(const coap_session_t *session) {

  if (session->psk_identity) {
    return session->psk_identity;
  }
  if (session->cpsk_setup_data.psk_info.identity.length)
    return &session->cpsk_setup_data.psk_info.identity;

  /* Not defined in coap_new_client_session_psk2() */
  return NULL;
}

#if COAP_SERVER_SUPPORT
const coap_bin_const_t *
coap_get_session_server_psk_key(const coap_session_t *session) {

  if (session->psk_key)
    return session->psk_key;

  if (session->context->spsk_setup_data.psk_info.key.length)
    return &session->context->spsk_setup_data.psk_info.key;

  /* Not defined in coap_context_set_psk2() */
  return NULL;
}

const coap_bin_const_t *
coap_get_session_server_psk_hint(const coap_session_t *session) {

  if (session->psk_hint)
    return session->psk_hint;

  if (session->context->spsk_setup_data.psk_info.hint.length)
    return &session->context->spsk_setup_data.psk_info.hint;

  /* Not defined in coap_context_set_psk2() */
  return NULL;
}

int coap_context_set_psk(coap_context_t *ctx,
                         const char *hint,
                         const uint8_t *key,
                         size_t key_len) {
  coap_dtls_spsk_t setup_data;

  memset (&setup_data, 0, sizeof(setup_data));
  if (hint) {
    setup_data.psk_info.hint.s = (const uint8_t *)hint;
    setup_data.psk_info.hint.length = strlen(hint);
  }

  if (key && key_len > 0) {
    setup_data.psk_info.key.s = key;
    setup_data.psk_info.key.length = key_len;
  }

  return coap_context_set_psk2(ctx, &setup_data);
}

int coap_context_set_psk2(coap_context_t *ctx, coap_dtls_spsk_t *setup_data) {
  if (!setup_data)
    return 0;

  ctx->spsk_setup_data = *setup_data;

  if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    return coap_dtls_context_set_spsk(ctx, setup_data);
  }
  return 0;
}

int coap_context_set_pki(coap_context_t *ctx,
                         const coap_dtls_pki_t* setup_data) {
  if (!setup_data)
    return 0;
  if (setup_data->version != COAP_DTLS_PKI_SETUP_VERSION) {
    coap_log(LOG_ERR, "coap_context_set_pki: Wrong version of setup_data\n");
    return 0;
  }
  if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    return coap_dtls_context_set_pki(ctx, setup_data, COAP_DTLS_ROLE_SERVER);
  }
  return 0;
}
#endif /* ! COAP_SERVER_SUPPORT */

int coap_context_set_pki_root_cas(coap_context_t *ctx,
                                  const char *ca_file,
                                  const char *ca_dir) {
  if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    return coap_dtls_context_set_pki_root_cas(ctx, ca_file, ca_dir);
  }
  return 0;
}

void coap_context_set_keepalive(coap_context_t *context, unsigned int seconds) {
  context->ping_timeout = seconds;
}

void
coap_context_set_max_idle_sessions(coap_context_t *context,
                                   unsigned int max_idle_sessions) {
  context->max_idle_sessions = max_idle_sessions;
}

unsigned int
coap_context_get_max_idle_sessions(const coap_context_t *context) {
  return context->max_idle_sessions;
}

void
coap_context_set_max_handshake_sessions(coap_context_t *context,
                                        unsigned int max_handshake_sessions) {
  context->max_handshake_sessions = max_handshake_sessions;
}

unsigned int
coap_context_get_max_handshake_sessions(const coap_context_t *context) {
  return context->max_handshake_sessions;
}

void
coap_context_set_csm_timeout(coap_context_t *context,
                             unsigned int csm_timeout) {
  context->csm_timeout = csm_timeout;
}

unsigned int
coap_context_get_csm_timeout(const coap_context_t *context) {
  return context->csm_timeout;
}

void
coap_context_set_csm_max_message_size(coap_context_t *context,
                                      uint32_t csm_max_message_size) {
  assert(csm_max_message_size >= 64);
  context->csm_max_message_size = csm_max_message_size;
}

uint32_t
coap_context_get_csm_max_message_size(const coap_context_t *context) {
  return context->csm_max_message_size;
}

void
coap_context_set_session_timeout(coap_context_t *context,
                                 unsigned int session_timeout) {
  context->session_timeout = session_timeout;
}

unsigned int
coap_context_get_session_timeout(const coap_context_t *context) {
  return context->session_timeout;
}

int coap_context_get_coap_fd(const coap_context_t *context) {
#ifdef COAP_EPOLL_SUPPORT
  return context->epfd;
#else /* ! COAP_EPOLL_SUPPORT */
  (void)context;
  return -1;
#endif /* ! COAP_EPOLL_SUPPORT */
}

coap_context_t *
coap_new_context(
  const coap_address_t *listen_addr) {
  coap_context_t *c;

#if ! COAP_SERVER_SUPPORT
  (void)listen_addr;
#endif /* COAP_SERVER_SUPPORT */

#ifdef WITH_CONTIKI
  if (initialized)
    return NULL;
#endif /* WITH_CONTIKI */

  coap_startup();

#ifndef WITH_CONTIKI
  c = coap_malloc_type(COAP_CONTEXT, sizeof(coap_context_t));
  if (!c) {
    coap_log(LOG_EMERG, "coap_init: malloc: failed\n");
    return NULL;
  }
#endif /* not WITH_CONTIKI */
#ifdef WITH_CONTIKI
  coap_resources_init();

  c = &the_coap_context;
  initialized = 1;
#endif /* WITH_CONTIKI */

  memset(c, 0, sizeof(coap_context_t));

#ifdef COAP_EPOLL_SUPPORT
  c->epfd = epoll_create1(0);
  if (c->epfd == -1) {
    coap_log(LOG_ERR, "coap_new_context: Unable to epoll_create: %s (%d)\n",
             coap_socket_strerror(),
             errno);
    goto onerror;
  }
  if (c->epfd != -1) {
    c->eptimerfd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);
    if (c->eptimerfd == -1) {
      coap_log(LOG_ERR, "coap_new_context: Unable to timerfd_create: %s (%d)\n",
               coap_socket_strerror(),
               errno);
      goto onerror;
    }
    else {
      int ret;
      struct epoll_event event;

      /* Needed if running 32bit as ptr is only 32bit */
      memset(&event, 0, sizeof(event));
      event.events = EPOLLIN;
      /* We special case this event by setting to NULL */
      event.data.ptr = NULL;

      ret = epoll_ctl(c->epfd, EPOLL_CTL_ADD, c->eptimerfd, &event);
      if (ret == -1) {
         coap_log(LOG_ERR,
                  "%s: epoll_ctl ADD failed: %s (%d)\n",
                  "coap_new_context",
                  coap_socket_strerror(), errno);
        goto onerror;
      }
    }
  }
#endif /* COAP_EPOLL_SUPPORT */

  if (coap_dtls_is_supported() || coap_tls_is_supported()) {
    c->dtls_context = coap_dtls_new_context(c);
    if (!c->dtls_context) {
      coap_log(LOG_EMERG, "coap_init: no DTLS context available\n");
      coap_free_context(c);
      return NULL;
    }
  }

  /* set default CSM values */
  c->csm_timeout = 30;
  c->csm_max_message_size = COAP_DEFAULT_MAX_PDU_RX_SIZE;

#if COAP_SERVER_SUPPORT
  if (listen_addr) {
    coap_endpoint_t *endpoint = coap_new_endpoint(c, listen_addr, COAP_PROTO_UDP);
    if (endpoint == NULL) {
      goto onerror;
    }
  }
#endif /* COAP_SERVER_SUPPORT */

#if !defined(WITH_LWIP)
  c->network_send = coap_network_send;
  c->network_read = coap_network_read;
#endif

#ifdef WITH_CONTIKI
  process_start(&coap_retransmit_process, (char *)c);

  PROCESS_CONTEXT_BEGIN(&coap_retransmit_process);
  etimer_set(&c->notify_timer, COAP_RESOURCE_CHECK_TIME * COAP_TICKS_PER_SECOND);
  /* the retransmit timer must be initialized to some large value */
  etimer_set(&the_coap_context.retransmit_timer, 0xFFFF);
  PROCESS_CONTEXT_END(&coap_retransmit_process);
#endif /* WITH_CONTIKI */

  return c;

#if defined(COAP_EPOLL_SUPPORT) || COAP_SERVER_SUPPORT
onerror:
  coap_free_type(COAP_CONTEXT, c);
  return NULL;
#endif /* COAP_EPOLL_SUPPORT || COAP_SERVER_SUPPORT */
}

void
coap_set_app_data(coap_context_t *ctx, void *app_data) {
  assert(ctx);
  ctx->app = app_data;
}

void *
coap_get_app_data(const coap_context_t *ctx) {
  assert(ctx);
  return ctx->app;
}

void
coap_free_context(coap_context_t *context) {
  if (!context)
    return;

#if COAP_SERVER_SUPPORT
  /* Removing a resource may cause a CON observe to be sent */
  coap_delete_all_resources(context);
#endif /* COAP_SERVER_SUPPORT */

  coap_delete_all(context->sendqueue);

#ifdef WITH_LWIP
  context->sendqueue = NULL;
  coap_retransmittimer_restart(context);
#endif

#ifndef WITHOUT_ASYNC
  coap_delete_all_async(context);
#endif /* WITHOUT_ASYNC */
#if COAP_SERVER_SUPPORT
  coap_cache_entry_t *cp, *ctmp;

  HASH_ITER(hh, context->cache, cp, ctmp) {
    coap_delete_cache_entry(context, cp);
  }
  if (context->cache_ignore_count) {
    coap_free(context->cache_ignore_options);
  }

  coap_endpoint_t *ep, *tmp;

  LL_FOREACH_SAFE(context->endpoint, ep, tmp) {
    coap_free_endpoint(ep);
  }
#endif /* COAP_SERVER_SUPPORT */

#if COAP_CLIENT_SUPPORT
  coap_session_t *sp, *rtmp;

  SESSIONS_ITER_SAFE(context->sessions, sp, rtmp) {
    coap_session_release(sp);
  }
#endif /* COAP_CLIENT_SUPPORT */

  if (context->dtls_context)
    coap_dtls_free_context(context->dtls_context);
#ifdef COAP_EPOLL_SUPPORT
  if (context->eptimerfd != -1) {
    int ret;
    struct epoll_event event;

    /* Kernels prior to 2.6.9 expect non NULL event parameter */
    ret = epoll_ctl(context->epfd, EPOLL_CTL_DEL, context->eptimerfd, &event);
    if (ret == -1) {
       coap_log(LOG_ERR,
                "%s: epoll_ctl DEL failed: %s (%d)\n",
                "coap_free_context",
                coap_socket_strerror(), errno);
    }
    close(context->eptimerfd);
    context->eptimerfd = -1;
  }
  if (context->epfd != -1) {
    close(context->epfd);
    context->epfd = -1;
  }
#endif /* COAP_EPOLL_SUPPORT */

#ifndef WITH_CONTIKI
  coap_free_type(COAP_CONTEXT, context);
#else /* WITH_CONTIKI */
  memset(&the_coap_context, 0, sizeof(coap_context_t));
  initialized = 0;
#endif /* WITH_CONTIKI */
}

int
coap_option_check_critical(coap_session_t *session,
  coap_pdu_t *pdu,
  coap_opt_filter_t *unknown) {

  coap_context_t *ctx = session->context;
  coap_opt_iterator_t opt_iter;
  int ok = 1;

  coap_option_iterator_init(pdu, &opt_iter, COAP_OPT_ALL);

  while (coap_option_next(&opt_iter)) {

    /* The following condition makes use of the fact that
     * coap_option_getb() returns -1 if type exceeds the bit-vector
     * filter. As the vector is supposed to be large enough to hold
     * the largest known option, we know that everything beyond is
     * bad.
     */
    if (opt_iter.number & 0x01) {
      /* first check the known built-in critical options */
      switch (opt_iter.number) {
      case COAP_OPTION_IF_MATCH:
      case COAP_OPTION_URI_HOST:
      case COAP_OPTION_IF_NONE_MATCH:
      case COAP_OPTION_URI_PORT:
      case COAP_OPTION_URI_PATH:
      case COAP_OPTION_URI_QUERY:
      case COAP_OPTION_ACCEPT:
      case COAP_OPTION_PROXY_URI:
      case COAP_OPTION_PROXY_SCHEME:
      case COAP_OPTION_BLOCK2:
      case COAP_OPTION_BLOCK1:
        break;
      default:
        if (coap_option_filter_get(&ctx->known_options, opt_iter.number) <= 0) {
#if COAP_SERVER_SUPPORT
          if ((opt_iter.number & 0x02) == 0) {
            coap_opt_iterator_t t_iter;

            /* Safe to forward  - check if proxy pdu */
            if (session->proxy_session)
              break;
            if (COAP_PDU_IS_REQUEST(pdu) && ctx->proxy_uri_resource &&
                (coap_check_option(pdu, COAP_OPTION_PROXY_URI, &t_iter) ||
                 coap_check_option(pdu, COAP_OPTION_PROXY_SCHEME, &t_iter))) {
              pdu->crit_opt = 1;
              break;
            }
          }
#endif /* COAP_SERVER_SUPPORT */
          coap_log(LOG_DEBUG, "unknown critical option %d\n", opt_iter.number);
          ok = 0;

          /* When opt_iter.number is beyond our known option range,
           * coap_option_filter_set() will return -1 and we are safe to leave
           * this loop. */
          if (coap_option_filter_set(unknown, opt_iter.number) == -1) {
            break;
          }
        }
      }
    }
  }

  return ok;
}

coap_mid_t
coap_send_ack(coap_session_t *session, const coap_pdu_t *request) {
  coap_pdu_t *response;
  coap_mid_t result = COAP_INVALID_MID;

  if (request && request->type == COAP_MESSAGE_CON &&
    COAP_PROTO_NOT_RELIABLE(session->proto)) {
    response = coap_pdu_init(COAP_MESSAGE_ACK, 0, request->mid, 0);
    if (response)
      result = coap_send_internal(session, response);
  }
  return result;
}

ssize_t
coap_session_send_pdu(coap_session_t *session, coap_pdu_t *pdu) {
  ssize_t bytes_written = -1;
  assert(pdu->hdr_size > 0);
  switch(session->proto) {
    case COAP_PROTO_UDP:
      bytes_written = coap_session_send(session, pdu->token - pdu->hdr_size,
                                        pdu->used_size + pdu->hdr_size);
      break;
    case COAP_PROTO_DTLS:
      bytes_written = coap_dtls_send(session, pdu->token - pdu->hdr_size,
                                     pdu->used_size + pdu->hdr_size);
      break;
    case COAP_PROTO_TCP:
#if !COAP_DISABLE_TCP
      bytes_written = coap_session_write(session, pdu->token - pdu->hdr_size,
                                         pdu->used_size + pdu->hdr_size);
#endif /* !COAP_DISABLE_TCP */
      break;
    case COAP_PROTO_TLS:
#if !COAP_DISABLE_TCP
      bytes_written = coap_tls_write(session, pdu->token - pdu->hdr_size,
                                     pdu->used_size + pdu->hdr_size);
#endif /* !COAP_DISABLE_TCP */
      break;
    case COAP_PROTO_NONE:
    default:
      break;
  }
  coap_show_pdu(LOG_DEBUG, pdu);
  return bytes_written;
}

static ssize_t
coap_send_pdu(coap_session_t *session, coap_pdu_t *pdu, coap_queue_t *node) {
  ssize_t bytes_written;

#ifdef WITH_LWIP

  coap_socket_t *sock = &session->sock;
  if (sock->flags == COAP_SOCKET_EMPTY) {
    assert(session->endpoint != NULL);
    sock = &session->endpoint->sock;
  }

  bytes_written = coap_socket_send_pdu(sock, session, pdu);
  if (bytes_written >= 0 && pdu->type == COAP_MESSAGE_CON &&
      COAP_PROTO_NOT_RELIABLE(session->proto))
    session->con_active++;

  if (LOG_DEBUG <= coap_get_log_level()) {
    coap_show_pdu(LOG_DEBUG, pdu);
  }
  coap_ticks(&session->last_rx_tx);

#else

  if (session->state == COAP_SESSION_STATE_NONE) {
#if ! COAP_CLIENT_SUPPORT
    return -1;
#else /* COAP_CLIENT_SUPPORT */
    if (session->proto == COAP_PROTO_DTLS && !session->tls) {
      session->tls = coap_dtls_new_client_session(session);
      if (session->tls) {
        session->state = COAP_SESSION_STATE_HANDSHAKE;
        return coap_session_delay_pdu(session, pdu, node);
      }
      coap_handle_event(session->context, COAP_EVENT_DTLS_ERROR, session);
      return -1;
#if !COAP_DISABLE_TCP
    } else if(COAP_PROTO_RELIABLE(session->proto)) {
      if (!coap_socket_connect_tcp1(
        &session->sock, &session->addr_info.local, &session->addr_info.remote,
        session->proto == COAP_PROTO_TLS ? COAPS_DEFAULT_PORT :
                                           COAP_DEFAULT_PORT,
        &session->addr_info.local, &session->addr_info.remote
      )) {
        coap_handle_event(session->context, COAP_EVENT_TCP_FAILED, session);
        return -1;
      }
      session->last_ping = 0;
      session->last_pong = 0;
      session->csm_tx = 0;
      coap_ticks( &session->last_rx_tx );
      if ((session->sock.flags & COAP_SOCKET_WANT_CONNECT) != 0) {
        session->state = COAP_SESSION_STATE_CONNECTING;
        return coap_session_delay_pdu(session, pdu, node);
      }
      coap_handle_event(session->context, COAP_EVENT_TCP_CONNECTED, session);
      if (session->proto == COAP_PROTO_TLS) {
        int connected = 0;
        session->state = COAP_SESSION_STATE_HANDSHAKE;
        session->tls = coap_tls_new_client_session(session, &connected);
        if (session->tls) {
          if (connected) {
            coap_handle_event(session->context, COAP_EVENT_DTLS_CONNECTED, session);
            coap_session_send_csm(session);
          }
          return coap_session_delay_pdu(session, pdu, node);
        }
        coap_handle_event(session->context, COAP_EVENT_DTLS_ERROR, session);
        coap_session_disconnected(session, COAP_NACK_TLS_FAILED);
        return -1;
      } else {
        coap_session_send_csm(session);
      }
#endif /* !COAP_DISABLE_TCP */
    } else {
      return -1;
    }
#endif /* COAP_CLIENT_SUPPORT */
  }

  if (pdu->type == COAP_MESSAGE_CON &&
      (session->sock.flags & COAP_SOCKET_NOT_EMPTY) &&
      (session->sock.flags & COAP_SOCKET_MULTICAST)) {
    /* Violates RFC72522 8.1 */
    coap_log(LOG_ERR, "Multicast requests cannot be Confirmable (RFC7252 8.1)\n");
    return -1;
  }

  if (session->state != COAP_SESSION_STATE_ESTABLISHED ||
      (pdu->type == COAP_MESSAGE_CON &&
       session->con_active >= COAP_NSTART(session))) {
    return coap_session_delay_pdu(session, pdu, node);
  }

  if ((session->sock.flags & COAP_SOCKET_NOT_EMPTY) &&
    (session->sock.flags & COAP_SOCKET_WANT_WRITE))
    return coap_session_delay_pdu(session, pdu, node);

  bytes_written = coap_session_send_pdu(session, pdu);
  if (bytes_written >= 0 && pdu->type == COAP_MESSAGE_CON &&
      COAP_PROTO_NOT_RELIABLE(session->proto))
    session->con_active++;

#endif /* WITH_LWIP */

  return bytes_written;
}

coap_mid_t
coap_send_error(coap_session_t *session,
  const coap_pdu_t *request,
  coap_pdu_code_t code,
  coap_opt_filter_t *opts) {
  coap_pdu_t *response;
  coap_mid_t result = COAP_INVALID_MID;

  assert(request);
  assert(session);

  response = coap_new_error_response(request, code, opts);
  if (response)
    result = coap_send_internal(session, response);

  return result;
}

coap_mid_t
coap_send_message_type(coap_session_t *session, const coap_pdu_t *request,
                       coap_pdu_type_t type) {
  coap_pdu_t *response;
  coap_mid_t result = COAP_INVALID_MID;

  if (request) {
    response = coap_pdu_init(type, 0, request->mid, 0);
    if (response)
      result = coap_send_internal(session, response);
  }
  return result;
}

/**
 * Calculates the initial timeout based on the session CoAP transmission
 * parameters 'ack_timeout', 'ack_random_factor', and COAP_TICKS_PER_SECOND.
 * The calculation requires 'ack_timeout' and 'ack_random_factor' to be in
 * Qx.FRAC_BITS fixed point notation, whereas the passed parameter @p r
 * is interpreted as the fractional part of a Q0.MAX_BITS random value.
 *
 * @param session session timeout is associated with
 * @param r  random value as fractional part of a Q0.MAX_BITS fixed point
 *           value
 * @return   COAP_TICKS_PER_SECOND * 'ack_timeout' *
 *           (1 + ('ack_random_factor' - 1) * r)
 */
unsigned int
coap_calc_timeout(coap_session_t *session, unsigned char r) {
  unsigned int result;

  /* The integer 1.0 as a Qx.FRAC_BITS */
#define FP1 Q(FRAC_BITS, ((coap_fixed_point_t){1,0}))

  /* rounds val up and right shifts by frac positions */
#define SHR_FP(val,frac) (((val) + (1 << ((frac) - 1))) >> (frac))

  /* Inner term: multiply ACK_RANDOM_FACTOR by Q0.MAX_BITS[r] and
   * make the result a rounded Qx.FRAC_BITS */
  result = SHR_FP((ACK_RANDOM_FACTOR - FP1) * r, MAX_BITS);

  /* Add 1 to the inner term and multiply with ACK_TIMEOUT, then
   * make the result a rounded Qx.FRAC_BITS */
  result = SHR_FP(((result + FP1) * ACK_TIMEOUT), FRAC_BITS);

  /* Multiply with COAP_TICKS_PER_SECOND to yield system ticks
   * (yields a Qx.FRAC_BITS) and shift to get an integer */
  return SHR_FP((COAP_TICKS_PER_SECOND * result), FRAC_BITS);

#undef FP1
#undef SHR_FP
}

coap_mid_t
coap_wait_ack(coap_context_t *context, coap_session_t *session,
              coap_queue_t *node) {
  coap_tick_t now;

  node->session = coap_session_reference(session);

  /* Set timer for pdu retransmission. If this is the first element in
  * the retransmission queue, the base time is set to the current
  * time and the retransmission time is node->timeout. If there is
  * already an entry in the sendqueue, we must check if this node is
  * to be retransmitted earlier. Therefore, node->timeout is first
  * normalized to the base time and then inserted into the queue with
  * an adjusted relative time.
  */
  coap_ticks(&now);
  if (context->sendqueue == NULL) {
    node->t = node->timeout << node->retransmit_cnt;
    context->sendqueue_basetime = now;
  } else {
    /* make node->t relative to context->sendqueue_basetime */
    node->t = (now - context->sendqueue_basetime) +
              (node->timeout << node->retransmit_cnt);
  }

  coap_insert_node(&context->sendqueue, node);

#ifdef WITH_LWIP
  if (node == context->sendqueue) /* don't bother with timer stuff if there are earlier retransmits */
    coap_retransmittimer_restart(context);
#endif

#ifdef WITH_CONTIKI
  {                            /* (re-)initialize retransmission timer */
    coap_queue_t *nextpdu;

    nextpdu = coap_peek_next(context);
    assert(nextpdu);                /* we have just inserted a node */

                                /* must set timer within the context of the retransmit process */
    PROCESS_CONTEXT_BEGIN(&coap_retransmit_process);
    etimer_set(&context->retransmit_timer, nextpdu->t);
    PROCESS_CONTEXT_END(&coap_retransmit_process);
  }
#endif /* WITH_CONTIKI */

  coap_log(LOG_DEBUG, "** %s: mid=0x%x: added to retransmit queue (%ums)\n",
    coap_session_str(node->session), node->id,
    (unsigned)(node->t * 1000 / COAP_TICKS_PER_SECOND));

#ifdef COAP_EPOLL_SUPPORT
  coap_update_epoll_timer(context, node->t);
#endif /* COAP_EPOLL_SUPPORT */

  return node->id;
}

COAP_STATIC_INLINE int
token_match(const uint8_t *a, size_t alen,
  const uint8_t *b, size_t blen) {
  return alen == blen && (alen == 0 || memcmp(a, b, alen) == 0);
}

int
coap_client_delay_first(coap_session_t *session)
{
  if (session->type == COAP_SESSION_TYPE_CLIENT && session->doing_first) {
    if (session->doing_first) {
      int timeout_ms = 5000;
#ifdef WITH_LWIP
#include <netif/tapif.h>
      struct netif *netif = ip_route(&session->sock.pcb->local_ip,
                                     &session->addr_info.remote.addr);
      if (!netif) {
        session->doing_first = 0;
        return 1;
      }
#endif /* WITH_LWIP */

      if (session->delay_recursive) {
        assert(0);
        return 1;
      } else {
        session->delay_recursive = 1;
      }
      /*
       * Need to wait for first request to get out and response back before
       * continuing.. Response handler has to clear doing_first if not an error.
       */
      coap_session_reference(session);
      while (session->doing_first != 0) {
#ifndef WITH_LWIP
        int result = coap_io_process(session->context, 1000);
#else /* WITH_LWIP */
        int result;
        coap_tick_t start;
        coap_tick_t end;

        coap_ticks(&start);
        result = tapif_select(netif);
#endif /* WITH_LWIP */

        if (result < 0) {
          session->doing_first = 0;
          session->delay_recursive = 0;
          coap_session_release(session);
          return 0;
        }
#ifdef WITH_LWIP
        sys_check_timeouts();
        coap_ticks(&end);
        result = (end - start) * 1000 / COAP_TICKS_PER_SECOND;
#endif /* WITH_LWIP */
        if (result <= timeout_ms) {
          timeout_ms -= result;
        } else {
          if (session->doing_first == 1) {
            /* Timeout failure of some sort with first request */
            coap_log(LOG_DEBUG, "** %s: timeout waiting for first response\n",
                     coap_session_str(session));
            session->doing_first = 0;
          }
        }
      }
      session->delay_recursive = 0;
      coap_session_release(session);
    }
  }
  return 1;
}

coap_mid_t
coap_send(coap_session_t *session, coap_pdu_t *pdu) {
  coap_mid_t mid = COAP_INVALID_MID;

  assert(pdu);

  if (session->type == COAP_SESSION_TYPE_CLIENT &&
      session->sock.flags == COAP_SOCKET_EMPTY) {
    coap_log(LOG_DEBUG, "coap_send: Socket closed\n");
    coap_delete_pdu(pdu);
    return COAP_INVALID_MID;
  }
#if COAP_CLIENT_SUPPORT
  coap_lg_crcv_t *lg_crcv = NULL;
  coap_opt_iterator_t opt_iter;
  coap_block_b_t block;
  int observe_action = -1;
  int have_block1 = 0;
  coap_opt_t *opt;

  if (!(session->block_mode & COAP_BLOCK_USE_LIBCOAP)) {
    return coap_send_internal(session, pdu);
  }

  if (COAP_PDU_IS_REQUEST(pdu)) {
    opt = coap_check_option(pdu, COAP_OPTION_OBSERVE, &opt_iter);

    if (opt) {
      observe_action = coap_decode_var_bytes(coap_opt_value(opt),
                                                     coap_opt_length(opt));
    }

    if (coap_get_block_b(session, pdu, COAP_OPTION_BLOCK1, &block) &&
        (block.m == 1 || block.bert == 1))
      have_block1 = 1;
    if (observe_action != COAP_OBSERVE_CANCEL) {
      /* Warn about re-use of tokens */
      coap_bin_const_t token = coap_pdu_get_token(pdu);

      if (session->last_token &&
          coap_binary_equal(&token, session->last_token)) {
        coap_log(LOG_DEBUG, "Token reused - see https://www.rfc-editor.org/rfc/rfc9175.html#section-4.2\n");
      }
      coap_delete_bin_const(session->last_token);
      session->last_token = coap_new_bin_const(token.s, token.length);
    }
  } else {
    memset(&block, 0, sizeof(block));
  }

  /*
   * If type is CON and protocol is not reliable, there is no need to set up
   * lg_crcv here as it can be built up based on sent PDU if there is a
   * Block2 in the response.  However, still need it for observe and block1.
   */
  if (observe_action != -1 || have_block1 ||
      ((pdu->type == COAP_MESSAGE_NON || COAP_PROTO_RELIABLE(session->proto)) &&
       COAP_PDU_IS_REQUEST(pdu) && pdu->code != COAP_REQUEST_CODE_DELETE)) {
    coap_lg_xmit_t *lg_xmit = NULL;

    if (!session->lg_xmit) {
      coap_log(LOG_DEBUG, "PDU presented by app\n");
      coap_show_pdu(LOG_DEBUG, pdu);
    }
    /* See if this token is already in use for large body responses */
    LL_FOREACH(session->lg_crcv, lg_crcv) {
      if (token_match(pdu->token, pdu->token_length,
                      lg_crcv->app_token->s, lg_crcv->app_token->length)) {

        if (observe_action == COAP_OBSERVE_CANCEL) {
          uint8_t buf[8];
          size_t len;

          /* Need to update token to server's version */
          len = coap_encode_var_safe8(buf, sizeof(lg_crcv->state_token),
                                      lg_crcv->state_token);
          coap_update_token(pdu, len, buf);
          lg_crcv->initial = 1;
          lg_crcv->observe_set = 0;
          /* de-reference lg_crcv as potentially linking in later */
          LL_DELETE(session->lg_crcv, lg_crcv);
          goto send_it;
        }

        /* Need to terminate and clean up previous response setup */
        LL_DELETE(session->lg_crcv, lg_crcv);
        coap_block_delete_lg_crcv(session, lg_crcv);
        break;
      }
    }

    if (have_block1 && session->lg_xmit) {
      LL_FOREACH(session->lg_xmit, lg_xmit) {
        if (COAP_PDU_IS_REQUEST(&lg_xmit->pdu) &&
            lg_xmit->b.b1.app_token &&
            token_match(pdu->token, pdu->token_length,
                        lg_xmit->b.b1.app_token->s,
                        lg_xmit->b.b1.app_token->length)) {
          break;
        }
      }
    }
    lg_crcv = coap_block_new_lg_crcv(session, pdu);
    if (lg_crcv == NULL) {
      coap_delete_pdu(pdu);
      return COAP_INVALID_MID;
    }
    if (lg_xmit) {
      /* Need to update the token as set up in the session->lg_xmit */
      lg_xmit->b.b1.state_token = lg_crcv->state_token;
    }
  }

send_it:
#endif /* COAP_CLIENT_SUPPORT */
  mid = coap_send_internal(session, pdu);
#if COAP_CLIENT_SUPPORT
  if (lg_crcv) {
    if (mid != COAP_INVALID_MID) {
      LL_PREPEND(session->lg_crcv, lg_crcv);
    }
    else {
      coap_block_delete_lg_crcv(session, lg_crcv);
    }
  }
#endif /* COAP_CLIENT_SUPPORT */
  return mid;
}

coap_mid_t
coap_send_internal(coap_session_t *session, coap_pdu_t *pdu) {
  uint8_t r;
  ssize_t bytes_written;
  coap_opt_iterator_t opt_iter;

  if (pdu->code == COAP_RESPONSE_CODE(508)) {
    /*
     * Need to prepend our IP identifier to the data as per
     * https://www.rfc-editor.org/rfc/rfc8768.html#section-4
     */
    char addr_str[INET6_ADDRSTRLEN + 8 + 1];
    coap_opt_t *opt;
    size_t hop_limit;

    addr_str[sizeof(addr_str)-1] = '\000';
    if (coap_print_addr(&session->addr_info.local, (uint8_t*)addr_str,
                            sizeof(addr_str) - 1)) {
      char *cp;
      size_t len;

      if (addr_str[0] == '[') {
        cp = strchr(addr_str, ']');
        if (cp) *cp = '\000';
        if (memcmp(&addr_str[1], "::ffff:", 7) == 0) {
          /* IPv4 embedded into IPv6 */
          cp = &addr_str[8];
        }
        else {
          cp = &addr_str[1];
        }
      }
      else {
        cp = strchr(addr_str, ':');
        if (cp) *cp = '\000';
        cp = addr_str;
      }
      len = strlen(cp);

      /* See if Hop Limit option is being used in return path */
      opt = coap_check_option(pdu, COAP_OPTION_HOP_LIMIT, &opt_iter);
      if (opt) {
        uint8_t buf[4];

        hop_limit =
          coap_decode_var_bytes (coap_opt_value (opt), coap_opt_length (opt));
        if (hop_limit == 1) {
          coap_log(LOG_WARNING, "Proxy loop detected '%s'\n",
                   (char*)pdu->data);
          coap_delete_pdu(pdu);
          return (coap_mid_t)COAP_DROPPED_RESPONSE;
        }
        else if (hop_limit < 1 || hop_limit > 255) {
          /* Something is bad - need to drop this pdu (TODO or delete option) */
          coap_log(LOG_WARNING, "Proxy return has bad hop limit count '%zu'\n",
                                 hop_limit);
          coap_delete_pdu(pdu);
          return (coap_mid_t)COAP_DROPPED_RESPONSE;
        }
        hop_limit--;
        coap_update_option(pdu, COAP_OPTION_HOP_LIMIT,
                           coap_encode_var_safe8(buf, sizeof(buf), hop_limit),
                           buf);
      }

      /* Need to check that we are not seeing this proxy in the return loop */
      if (pdu->data && opt == NULL) {
        if (pdu->used_size + 1 <= pdu->max_size) {
          char *a_match;
          size_t data_len = pdu->used_size - (pdu->data - pdu->token);
          pdu->data[data_len] = '\000';
          a_match = strstr((char*)pdu->data, cp);
          if (a_match && (a_match == (char*)pdu->data || a_match[-1] == ' ') &&
              ((size_t)(a_match - (char*)pdu->data + len) == data_len ||
               a_match[len] == ' ')) {
            coap_log(LOG_WARNING, "Proxy loop detected '%s'\n",
                     (char*)pdu->data);
            coap_delete_pdu(pdu);
            return (coap_mid_t)COAP_DROPPED_RESPONSE;
          }
        }
      }
      if (pdu->used_size + len + 1 <= pdu->max_size) {
        size_t old_size = pdu->used_size;
        if (coap_pdu_resize(pdu, pdu->used_size + len + 1)) {
          if (pdu->data == NULL) {
            /*
             * Set Hop Limit to max for return path.  If this libcoap is in
             * a proxy loop path, it will always decrement hop limit in code
             * above and hence timeout / drop the response as appropriate
             */
            hop_limit = 255;
            coap_insert_option(pdu, COAP_OPTION_HOP_LIMIT, 1,
                               (uint8_t *)&hop_limit);
            coap_add_data(pdu, len, (uint8_t*)cp);
          }
          else {
            /* prepend with space separator, leaving hop limit "as is" */
            memmove(pdu->data + len + 1, pdu->data,
                    old_size - (pdu->data - pdu->token));
            memcpy(pdu->data, cp, len);
            pdu->data[len] = ' ';
            pdu->used_size += len + 1;
          }
        }
      }
    }
  }

  if (session->echo) {
    if (!coap_insert_option(pdu, COAP_OPTION_ECHO, session->echo->length,
                            session->echo->s))
      goto error;
    coap_delete_bin_const(session->echo);
    session->echo = NULL;
  }

  if (!coap_pdu_encode_header(pdu, session->proto)) {
    goto error;
  }

#if !COAP_DISABLE_TCP
  if (COAP_PROTO_RELIABLE(session->proto) &&
      session->state == COAP_SESSION_STATE_ESTABLISHED) {
    if (!session->csm_block_supported) {
      /*
       * Need to check that this instance is not sending any block options as
       * the remote end via CSM has not informed us that there is support
       * https://tools.ietf.org/html/rfc8323#section-5.3.2
       * This includes potential BERT blocks.
       */
      if (coap_check_option(pdu, COAP_OPTION_BLOCK1, &opt_iter) != NULL) {
        coap_log(LOG_DEBUG,
                 "Remote end did not indicate CSM support for Block1 enabled\n");
      }
      if (coap_check_option(pdu, COAP_OPTION_BLOCK2, &opt_iter) != NULL) {
        coap_log(LOG_DEBUG,
                 "Remote end did not indicate CSM support for Block2 enabled\n");
      }
    }
    else if (!session->csm_bert_rem_support) {
      coap_opt_t *opt;

      opt = coap_check_option(pdu, COAP_OPTION_BLOCK1, &opt_iter);
      if (opt && COAP_OPT_BLOCK_SZX(opt) == 7) {
        coap_log(LOG_DEBUG,
               "Remote end did not indicate CSM support for BERT Block1\n");
      }
      opt = coap_check_option(pdu, COAP_OPTION_BLOCK2, &opt_iter);
      if (opt && COAP_OPT_BLOCK_SZX(opt) == 7) {
        coap_log(LOG_DEBUG,
               "Remote end did not indicate CSM support for BERT Block2\n");
      }
    }
  }
#endif /* !COAP_DISABLE_TCP */

  bytes_written = coap_send_pdu( session, pdu, NULL );

  if (bytes_written == COAP_PDU_DELAYED) {
    /* do not free pdu as it is stored with session for later use */
    return pdu->mid;
  }

  if (bytes_written < 0) {
    goto error;
  }

#if !COAP_DISABLE_TCP
  if (COAP_PROTO_RELIABLE(session->proto) &&
      (size_t)bytes_written < pdu->used_size + pdu->hdr_size) {
    if (coap_session_delay_pdu(session, pdu, NULL) == COAP_PDU_DELAYED) {
      session->partial_write = (size_t)bytes_written;
      /* do not free pdu as it is stored with session for later use */
      return pdu->mid;
    } else {
      goto error;
    }
  }
#endif /* !COAP_DISABLE_TCP */

  if (pdu->type != COAP_MESSAGE_CON
      || COAP_PROTO_RELIABLE(session->proto)) {
    coap_mid_t id = pdu->mid;
    coap_delete_pdu(pdu);
    return id;
  }

  coap_queue_t *node = coap_new_node();
  if (!node) {
    coap_log(LOG_DEBUG, "coap_wait_ack: insufficient memory\n");
    goto error;
  }

  node->id = pdu->mid;
  node->pdu = pdu;
  coap_prng(&r, sizeof(r));
  /* add timeout in range [ACK_TIMEOUT...ACK_TIMEOUT * ACK_RANDOM_FACTOR] */
  node->timeout = coap_calc_timeout(session, r);
  return coap_wait_ack(session->context, session, node);
 error:
  coap_delete_pdu(pdu);
  return COAP_INVALID_MID;
}

coap_mid_t
coap_retransmit(coap_context_t *context, coap_queue_t *node) {
  if (!context || !node)
    return COAP_INVALID_MID;

  /* re-initialize timeout when maximum number of retransmissions are not reached yet */
  if (node->retransmit_cnt < node->session->max_retransmit) {
    ssize_t bytes_written;
    coap_tick_t now;

    node->retransmit_cnt++;
    coap_ticks(&now);
    if (context->sendqueue == NULL) {
      node->t = node->timeout << node->retransmit_cnt;
      context->sendqueue_basetime = now;
    } else {
      /* make node->t relative to context->sendqueue_basetime */
      node->t = (now - context->sendqueue_basetime) + (node->timeout << node->retransmit_cnt);
    }
    coap_insert_node(&context->sendqueue, node);
#ifdef WITH_LWIP
    if (node == context->sendqueue) /* don't bother with timer stuff if there are earlier retransmits */
      coap_retransmittimer_restart(context);
#endif

    if (node->is_mcast) {
      coap_log(LOG_DEBUG, "** %s: mid=0x%x: mcast delayed transmission\n",
               coap_session_str(node->session), node->id);
    } else {
      coap_log(LOG_DEBUG, "** %s: mid=0x%x: retransmission #%d\n",
               coap_session_str(node->session), node->id, node->retransmit_cnt);
    }

    if (node->session->con_active)
      node->session->con_active--;
    bytes_written = coap_send_pdu(node->session, node->pdu, node);

    if (node->is_mcast) {
      coap_session_connected(node->session);
      coap_delete_node(node);
      return COAP_INVALID_MID;
    }
    if (bytes_written == COAP_PDU_DELAYED) {
      /* PDU was not retransmitted immediately because a new handshake is
         in progress. node was moved to the send queue of the session. */
      return node->id;
    }

    if (bytes_written < 0)
      return (int)bytes_written;

    return node->id;
  }

  /* no more retransmissions, remove node from system */

#ifndef WITH_CONTIKI
  coap_log(LOG_DEBUG, "** %s: mid=0x%x: give up after %d attempts\n",
           coap_session_str(node->session), node->id, node->retransmit_cnt);
#endif

#if COAP_SERVER_SUPPORT
  /* Check if subscriptions exist that should be canceled after
     COAP_OBS_MAX_FAIL */
  if (COAP_RESPONSE_CLASS(node->pdu->code) >= 2) {
    coap_binary_t token = { 0, NULL };

    token.length = node->pdu->token_length;
    token.s = node->pdu->token;

    coap_handle_failed_notify(context, node->session, &token);
  }
#endif /* COAP_SERVER_SUPPORT */
  if (node->session->con_active) {
    node->session->con_active--;
    if (node->session->state == COAP_SESSION_STATE_ESTABLISHED) {
      /*
       * As there may be another CON in a different queue entry on the same
       * session that needs to be immediately released,
       * coap_session_connected() is called.
       * However, there is the possibility coap_wait_ack() may be called for
       * this node (queue) and re-added to context->sendqueue.
       * coap_delete_node(node) called shortly will handle this and remove it.
       */
      coap_session_connected(node->session);
    }
 }

  /* And finally delete the node */
  if (node->pdu->type == COAP_MESSAGE_CON && context->nack_handler)
    context->nack_handler(node->session, node->pdu, COAP_NACK_TOO_MANY_RETRIES, node->id);
  coap_delete_node(node);
  return COAP_INVALID_MID;
}

#ifdef WITH_LWIP
/* WITH_LWIP, this is handled by coap_recv in a different way */
void
coap_io_do_io(coap_context_t *ctx, coap_tick_t now) {
  return;
}
#else /* WITH_LWIP */

static int
coap_handle_dgram_for_proto(coap_context_t *ctx, coap_session_t *session, coap_packet_t *packet) {
  uint8_t *data;
  size_t data_len;
  int result = -1;

  coap_packet_get_memmapped(packet, &data, &data_len);

  if (session->proto == COAP_PROTO_DTLS) {
#if COAP_SERVER_SUPPORT
    if (session->type == COAP_SESSION_TYPE_HELLO)
      result = coap_dtls_hello(session, data, data_len);
    else
#endif /* COAP_SERVER_SUPPORT */
    if (session->tls)
      result = coap_dtls_receive(session, data, data_len);
  } else if (session->proto == COAP_PROTO_UDP) {
    result = coap_handle_dgram(ctx, session, data, data_len);
  }
  return result;
}

#if COAP_CLIENT_SUPPORT
static void
coap_connect_session(coap_context_t *ctx,
                          coap_session_t *session,
                          coap_tick_t now) {
  (void)ctx;
#if COAP_DISABLE_TCP
  (void)session;
  (void)now;
#else /* !COAP_DISABLE_TCP */
  if (coap_socket_connect_tcp2(&session->sock, &session->addr_info.local,
                               &session->addr_info.remote)) {
    session->last_rx_tx = now;
    coap_handle_event(session->context, COAP_EVENT_TCP_CONNECTED, session);
    if (session->proto == COAP_PROTO_TCP) {
      coap_session_send_csm(session);
    } else if (session->proto == COAP_PROTO_TLS) {
      int connected = 0;
      session->state = COAP_SESSION_STATE_HANDSHAKE;
      session->tls = coap_tls_new_client_session(session, &connected);
      if (session->tls) {
        if (connected) {
          coap_handle_event(session->context, COAP_EVENT_DTLS_CONNECTED,
                            session);
          coap_session_send_csm(session);
        }
      } else {
        coap_handle_event(session->context, COAP_EVENT_DTLS_ERROR, session);
        coap_session_disconnected(session, COAP_NACK_TLS_FAILED);
      }
    }
  } else {
    coap_handle_event(session->context, COAP_EVENT_TCP_FAILED, session);
    coap_session_disconnected(session, COAP_NACK_NOT_DELIVERABLE);
  }
#endif /* !COAP_DISABLE_TCP */
}
#endif /* COAP_CLIENT_SUPPORT */

static void
coap_write_session(coap_context_t *ctx, coap_session_t *session, coap_tick_t now) {
  (void)ctx;
  assert(session->sock.flags & COAP_SOCKET_CONNECTED);

  while (session->delayqueue) {
    ssize_t bytes_written;
    coap_queue_t *q = session->delayqueue;
    coap_log(LOG_DEBUG, "** %s: mid=0x%x: transmitted after delay\n",
             coap_session_str(session), (int)q->pdu->mid);
    assert(session->partial_write < q->pdu->used_size + q->pdu->hdr_size);
    switch (session->proto) {
      case COAP_PROTO_TCP:
#if !COAP_DISABLE_TCP
        bytes_written = coap_session_write(
          session,
          q->pdu->token - q->pdu->hdr_size + session->partial_write,
          q->pdu->used_size + q->pdu->hdr_size - session->partial_write
        );
#endif /* !COAP_DISABLE_TCP */
        break;
      case COAP_PROTO_TLS:
#if !COAP_DISABLE_TCP
        bytes_written = coap_tls_write(
          session,
          q->pdu->token - q->pdu->hdr_size - session->partial_write,
          q->pdu->used_size + q->pdu->hdr_size - session->partial_write
        );
#endif /* !COAP_DISABLE_TCP */
        break;
      case COAP_PROTO_NONE:
      case COAP_PROTO_UDP:
      case COAP_PROTO_DTLS:
      default:
        bytes_written = -1;
        break;
    }
    if (bytes_written > 0)
      session->last_rx_tx = now;
    if (bytes_written <= 0 || (size_t)bytes_written < q->pdu->used_size + q->pdu->hdr_size - session->partial_write) {
      if (bytes_written > 0)
        session->partial_write += (size_t)bytes_written;
      break;
    }
    session->delayqueue = q->next;
    session->partial_write = 0;
    coap_delete_node(q);
  }
}

static void
coap_read_session(coap_context_t *ctx, coap_session_t *session, coap_tick_t now) {
#if COAP_CONSTRAINED_STACK
  static coap_mutex_t s_static_mutex = COAP_MUTEX_INITIALIZER;
  static coap_packet_t s_packet;
#else /* ! COAP_CONSTRAINED_STACK */
  coap_packet_t s_packet;
#endif /* ! COAP_CONSTRAINED_STACK */
  coap_packet_t *packet = &s_packet;

#if COAP_CONSTRAINED_STACK
  coap_mutex_lock(&s_static_mutex);
#endif /* COAP_CONSTRAINED_STACK */

  assert(session->sock.flags & (COAP_SOCKET_CONNECTED | COAP_SOCKET_MULTICAST));

  if (COAP_PROTO_NOT_RELIABLE(session->proto)) {
    ssize_t bytes_read;
    memcpy(&packet->addr_info, &session->addr_info, sizeof(packet->addr_info));
    bytes_read = ctx->network_read(&session->sock, packet);

    if (bytes_read < 0) {
      if (bytes_read == -2)
        /* Reset the session back to startup defaults */
        coap_session_disconnected(session, COAP_NACK_ICMP_ISSUE);
      else
        coap_log(LOG_WARNING, "*  %s: read error\n",
                 coap_session_str(session));
    } else if (bytes_read > 0) {
      session->last_rx_tx = now;
      memcpy(&session->addr_info, &packet->addr_info,
             sizeof(session->addr_info));
      coap_log(LOG_DEBUG, "*  %s: received %zd bytes\n",
               coap_session_str(session), bytes_read);
      coap_handle_dgram_for_proto(ctx, session, packet);
    }
#if !COAP_DISABLE_TCP
  } else {
    ssize_t bytes_read = 0;
    const uint8_t *p;
    int retry;
    /* adjust for LWIP */
    uint8_t *buf = packet->payload;
    size_t buf_len = sizeof(packet->payload);

    do {
      if (session->proto == COAP_PROTO_TCP)
        bytes_read = coap_socket_read(&session->sock, buf, buf_len);
      else if (session->proto == COAP_PROTO_TLS)
        bytes_read = coap_tls_read(session, buf, buf_len);
      if (bytes_read > 0) {
        coap_log(LOG_DEBUG, "*  %s: received %zd bytes\n",
                 coap_session_str(session), bytes_read);
        session->last_rx_tx = now;
      }
      p = buf;
      retry = bytes_read == (ssize_t)buf_len;
      while (bytes_read > 0) {
        if (session->partial_pdu) {
          size_t len = session->partial_pdu->used_size
                     + session->partial_pdu->hdr_size
                     - session->partial_read;
          size_t n = min(len, (size_t)bytes_read);
          memcpy(session->partial_pdu->token - session->partial_pdu->hdr_size
                 + session->partial_read, p, n);
          p += n;
          bytes_read -= n;
          if (n == len) {
            if (coap_pdu_parse_header(session->partial_pdu, session->proto)
              && coap_pdu_parse_opt(session->partial_pdu)) {
#if COAP_CONSTRAINED_STACK
              coap_mutex_unlock(&s_static_mutex);
#endif /* COAP_CONSTRAINED_STACK */
              coap_dispatch(ctx, session, session->partial_pdu);
#if COAP_CONSTRAINED_STACK
              coap_mutex_lock(&s_static_mutex);
#endif /* COAP_CONSTRAINED_STACK */
            }
            coap_delete_pdu(session->partial_pdu);
            session->partial_pdu = NULL;
            session->partial_read = 0;
          } else {
            session->partial_read += n;
          }
        } else if (session->partial_read > 0) {
          size_t hdr_size = coap_pdu_parse_header_size(session->proto,
            session->read_header);
          size_t len = hdr_size - session->partial_read;
          size_t n = min(len, (size_t)bytes_read);
          memcpy(session->read_header + session->partial_read, p, n);
          p += n;
          bytes_read -= n;
          if (n == len) {
            size_t size = coap_pdu_parse_size(session->proto, session->read_header,
              hdr_size);
            if (size > COAP_DEFAULT_MAX_PDU_RX_SIZE) {
              coap_log(LOG_WARNING,
                       "** %s: incoming PDU length too large (%zu > %lu)\n",
                       coap_session_str(session),
                       size, COAP_DEFAULT_MAX_PDU_RX_SIZE);
              bytes_read = -1;
              break;
            }
            /* Need max space incase PDU is updated with updated token etc. */
            session->partial_pdu = coap_pdu_init(0, 0, 0,
                                           coap_session_max_pdu_rcv_size(session));
            if (session->partial_pdu == NULL) {
              bytes_read = -1;
              break;
            }
            if (session->partial_pdu->alloc_size < size && !coap_pdu_resize(session->partial_pdu, size)) {
              bytes_read = -1;
              break;
            }
            session->partial_pdu->hdr_size = (uint8_t)hdr_size;
            session->partial_pdu->used_size = size;
            memcpy(session->partial_pdu->token - hdr_size, session->read_header, hdr_size);
            session->partial_read = hdr_size;
            if (size == 0) {
              if (coap_pdu_parse_header(session->partial_pdu, session->proto)) {
#if COAP_CONSTRAINED_STACK
                coap_mutex_unlock(&s_static_mutex);
#endif /* COAP_CONSTRAINED_STACK */
                coap_dispatch(ctx, session, session->partial_pdu);
#if COAP_CONSTRAINED_STACK
                coap_mutex_lock(&s_static_mutex);
#endif /* COAP_CONSTRAINED_STACK */
              }
              coap_delete_pdu(session->partial_pdu);
              session->partial_pdu = NULL;
              session->partial_read = 0;
            }
          } else {
            session->partial_read += bytes_read;
          }
        } else {
          session->read_header[0] = *p++;
          bytes_read -= 1;
          if (!coap_pdu_parse_header_size(session->proto,
                                          session->read_header)) {
            bytes_read = -1;
            break;
          }
          session->partial_read = 1;
        }
      }
    } while (bytes_read == 0 && retry);
    if (bytes_read < 0)
      coap_session_disconnected(session, COAP_NACK_NOT_DELIVERABLE);
#endif /* !COAP_DISABLE_TCP */
  }
#if COAP_CONSTRAINED_STACK
  coap_mutex_unlock(&s_static_mutex);
#endif /* COAP_CONSTRAINED_STACK */
}

#if COAP_SERVER_SUPPORT
static int
coap_read_endpoint(coap_context_t *ctx, coap_endpoint_t *endpoint, coap_tick_t now) {
  ssize_t bytes_read = -1;
  int result = -1;                /* the value to be returned */
#if COAP_CONSTRAINED_STACK
  static coap_mutex_t e_static_mutex = COAP_MUTEX_INITIALIZER;
  static coap_packet_t e_packet;
#else /* ! COAP_CONSTRAINED_STACK */
  coap_packet_t e_packet;
#endif /* ! COAP_CONSTRAINED_STACK */
  coap_packet_t *packet = &e_packet;

  assert(COAP_PROTO_NOT_RELIABLE(endpoint->proto));
  assert(endpoint->sock.flags & COAP_SOCKET_BOUND);

#if COAP_CONSTRAINED_STACK
  coap_mutex_lock(&e_static_mutex);
#endif /* COAP_CONSTRAINED_STACK */

  /* Need to do this as there may be holes in addr_info */
  memset(&packet->addr_info, 0, sizeof(packet->addr_info));
  coap_address_init(&packet->addr_info.remote);
  coap_address_copy(&packet->addr_info.local, &endpoint->bind_addr);
  bytes_read = ctx->network_read(&endpoint->sock, packet);

  if (bytes_read < 0) {
    coap_log(LOG_WARNING, "*  %s: read failed\n", coap_endpoint_str(endpoint));
  } else if (bytes_read > 0) {
    coap_session_t *session = coap_endpoint_get_session(endpoint, packet, now);
    if (session) {
      coap_log(LOG_DEBUG, "*  %s: received %zd bytes\n",
               coap_session_str(session), bytes_read);
      result = coap_handle_dgram_for_proto(ctx, session, packet);
      if (endpoint->proto == COAP_PROTO_DTLS && session->type == COAP_SESSION_TYPE_HELLO && result == 1)
        coap_session_new_dtls_session(session, now);
    }
  }
#if COAP_CONSTRAINED_STACK
  coap_mutex_unlock(&e_static_mutex);
#endif /* COAP_CONSTRAINED_STACK */
  return result;
}

static int
coap_write_endpoint(coap_context_t *ctx, coap_endpoint_t *endpoint, coap_tick_t now) {
  (void)ctx;
  (void)endpoint;
  (void)now;
  return 0;
}

static int
coap_accept_endpoint(coap_context_t *ctx, coap_endpoint_t *endpoint,
  coap_tick_t now) {
  coap_session_t *session = coap_new_server_session(ctx, endpoint);
  if (session)
    session->last_rx_tx = now;
  return session != NULL;
}
#endif /* COAP_SERVER_SUPPORT */

void
coap_io_do_io(coap_context_t *ctx, coap_tick_t now) {
#ifdef COAP_EPOLL_SUPPORT
  (void)ctx;
  (void)now;
   coap_log(LOG_EMERG,
            "coap_io_do_io() requires libcoap not compiled for using epoll\n");
#else /* ! COAP_EPOLL_SUPPORT */
  coap_session_t *s, *rtmp;

#if COAP_SERVER_SUPPORT
  coap_endpoint_t *ep, *tmp;
  LL_FOREACH_SAFE(ctx->endpoint, ep, tmp) {
    if ((ep->sock.flags & COAP_SOCKET_CAN_READ) != 0)
      coap_read_endpoint(ctx, ep, now);
    if ((ep->sock.flags & COAP_SOCKET_CAN_WRITE) != 0)
      coap_write_endpoint(ctx, ep, now);
    if ((ep->sock.flags & COAP_SOCKET_CAN_ACCEPT) != 0)
      coap_accept_endpoint(ctx, ep, now);
    SESSIONS_ITER_SAFE(ep->sessions, s, rtmp) {
      /* Make sure the session object is not deleted in one of the callbacks  */
      coap_session_reference(s);
      if ((s->sock.flags & COAP_SOCKET_CAN_READ) != 0) {
        coap_read_session(ctx, s, now);
      }
      if ((s->sock.flags & COAP_SOCKET_CAN_WRITE) != 0) {
        coap_write_session(ctx, s, now);
      }
      coap_session_release(s);
    }
  }
#endif /* COAP_SERVER_SUPPORT */

#if COAP_CLIENT_SUPPORT
  SESSIONS_ITER_SAFE(ctx->sessions, s, rtmp) {
    /* Make sure the session object is not deleted in one of the callbacks  */
    coap_session_reference(s);
    if ((s->sock.flags & COAP_SOCKET_CAN_CONNECT) != 0) {
      coap_connect_session(ctx, s, now);
    }
    if ((s->sock.flags & COAP_SOCKET_CAN_READ) != 0 && s->ref > 1) {
      coap_read_session(ctx, s, now);
    }
    if ((s->sock.flags & COAP_SOCKET_CAN_WRITE) != 0 && s->ref > 1) {
      coap_write_session(ctx, s, now);
    }
    coap_session_release( s );
  }
#endif /* COAP_CLIENT_SUPPORT */
#endif /* ! COAP_EPOLL_SUPPORT */
}

/*
 * While this code in part replicates coap_io_do_io(), doing the functions
 * directly saves having to iterate through the endpoints / sessions.
 */
void
coap_io_do_epoll(coap_context_t *ctx, struct epoll_event *events, size_t nevents) {
#ifndef COAP_EPOLL_SUPPORT
  (void)ctx;
  (void)events;
  (void)nevents;
   coap_log(LOG_EMERG,
            "coap_io_do_epoll() requires libcoap compiled for using epoll\n");
#else /* COAP_EPOLL_SUPPORT */
  coap_tick_t now;
  size_t j;

  coap_ticks(&now);
  for(j = 0; j < nevents; j++) {
    coap_socket_t *sock = (coap_socket_t*)events[j].data.ptr;

    /* Ignore 'timer trigger' ptr  which is NULL */
    if (sock) {
#if COAP_SERVER_SUPPORT
      if (sock->endpoint) {
        coap_endpoint_t *endpoint = sock->endpoint;
        if ((sock->flags & COAP_SOCKET_WANT_READ) &&
            (events[j].events & EPOLLIN)) {
          sock->flags |= COAP_SOCKET_CAN_READ;
          coap_read_endpoint(endpoint->context, endpoint, now);
        }

        if ((sock->flags & COAP_SOCKET_WANT_WRITE) &&
            (events[j].events & EPOLLOUT)) {
          /*
           * Need to update this to EPOLLIN as EPOLLOUT will normally always
           * be true causing epoll_wait to return early
           */
          coap_epoll_ctl_mod(sock, EPOLLIN, __func__);
          sock->flags |= COAP_SOCKET_CAN_WRITE;
          coap_write_endpoint(endpoint->context, endpoint, now);
        }

        if ((sock->flags & COAP_SOCKET_WANT_ACCEPT) &&
            (events[j].events & EPOLLIN)) {
          sock->flags |= COAP_SOCKET_CAN_ACCEPT;
          coap_accept_endpoint(endpoint->context, endpoint, now);
        }

      }
      else
#endif /* COAP_SERVER_SUPPORT */
      if (sock->session) {
        coap_session_t *session = sock->session;

        /* Make sure the session object is not deleted
           in one of the callbacks  */
        coap_session_reference(session);
#if COAP_CLIENT_SUPPORT
        if ((sock->flags & COAP_SOCKET_WANT_CONNECT) &&
            (events[j].events & (EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLRDHUP))) {
          sock->flags |= COAP_SOCKET_CAN_CONNECT;
          coap_connect_session(session->context, session, now);
          if (sock->flags != COAP_SOCKET_EMPTY &&
              !(sock->flags & COAP_SOCKET_WANT_WRITE)) {
            coap_epoll_ctl_mod(sock, EPOLLIN, __func__);
          }
        }
#endif /* COAP_CLIENT_SUPPORT */

        if ((sock->flags & COAP_SOCKET_WANT_READ) &&
            (events[j].events & (EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLRDHUP))) {
          sock->flags |= COAP_SOCKET_CAN_READ;
          coap_read_session(session->context, session, now);
        }

        if ((sock->flags & COAP_SOCKET_WANT_WRITE) &&
            (events[j].events & (EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLRDHUP))) {
          /*
           * Need to update this to EPOLLIN as EPOLLOUT will normally always
           * be true causing epoll_wait to return early
           */
          coap_epoll_ctl_mod(sock, EPOLLIN, __func__);
          sock->flags |= COAP_SOCKET_CAN_WRITE;
          coap_write_session(session->context, session, now);
        }
        /* Now dereference session so it can go away if needed */
        coap_session_release(session);
      }
    }
    else if (ctx->eptimerfd != -1) {
      /*
       * 'timer trigger' must have fired. eptimerfd needs to be read to clear
       *  it so that it does not set EPOLLIN in the next epoll_wait().
       */
      uint64_t count;

      /* Check the result from read() to suppress the warning on
       * systems that declare read() with warn_unused_result. */
      if (read(ctx->eptimerfd, &count, sizeof(count)) == -1) {
        /* do nothing */;
      }
    }
  }
  /* And update eptimerfd as to when to next trigger */
  coap_ticks(&now);
  coap_io_prepare_epoll(ctx, now);
#endif /* COAP_EPOLL_SUPPORT */
}

int
coap_handle_dgram(coap_context_t *ctx, coap_session_t *session,
  uint8_t *msg, size_t msg_len) {

  coap_pdu_t *pdu = NULL;

  assert(COAP_PROTO_NOT_RELIABLE(session->proto));
  if (msg_len < 4) {
    /* Minimum size of CoAP header - ignore runt */
    return -1;
  }

  /* Need max space incase PDU is updated with updated token etc. */
  pdu = coap_pdu_init(0, 0, 0, coap_session_max_pdu_size(session));
  if (!pdu)
    goto error;

  if (!coap_pdu_parse(session->proto, msg, msg_len, pdu)) {
    coap_log(LOG_WARNING, "discard malformed PDU\n");
    goto error;
  }

  coap_dispatch(ctx, session, pdu);
  coap_delete_pdu(pdu);
  return 0;

error:
  /*
   * https://tools.ietf.org/html/rfc7252#section-4.2 MUST send RST
   * https://tools.ietf.org/html/rfc7252#section-4.3 MAY send RST
   */
  coap_send_rst(session, pdu);
  coap_delete_pdu(pdu);
  return -1;
}
#endif /* not WITH_LWIP */

int
coap_remove_from_queue(coap_queue_t **queue, coap_session_t *session, coap_mid_t id, coap_queue_t **node) {
  coap_queue_t *p, *q;

  if (!queue || !*queue)
    return 0;

  /* replace queue head if PDU's time is less than head's time */

  if (session == (*queue)->session && id == (*queue)->id) { /* found message id */
    *node = *queue;
    *queue = (*queue)->next;
    if (*queue) {          /* adjust relative time of new queue head */
      (*queue)->t += (*node)->t;
    }
    (*node)->next = NULL;
    coap_log(LOG_DEBUG, "** %s: mid=0x%x: removed 1\n",
             coap_session_str(session), id);
    return 1;
  }

  /* search message id queue to remove (only first occurence will be removed) */
  q = *queue;
  do {
    p = q;
    q = q->next;
  } while (q && (session != q->session || id != q->id));

  if (q) {                        /* found message id */
    p->next = q->next;
    if (p->next) {                /* must update relative time of p->next */
      p->next->t += q->t;
    }
    q->next = NULL;
    *node = q;
    coap_log(LOG_DEBUG, "** %s: mid=0x%x: removed 2\n",
             coap_session_str(session), id);
    return 1;
  }

  return 0;

}

void
coap_cancel_session_messages(coap_context_t *context, coap_session_t *session,
  coap_nack_reason_t reason) {
  coap_queue_t *p, *q;

  while (context->sendqueue && context->sendqueue->session == session) {
    q = context->sendqueue;
    context->sendqueue = q->next;
    coap_log(LOG_DEBUG, "** %s: mid=0x%x: removed 3\n",
             coap_session_str(session), q->id);
    if (q->pdu->type == COAP_MESSAGE_CON && context->nack_handler)
      context->nack_handler(session, q->pdu, reason, q->id);
    coap_delete_node(q);
  }

  if (!context->sendqueue)
    return;

  p = context->sendqueue;
  q = p->next;

  while (q) {
    if (q->session == session) {
      p->next = q->next;
      coap_log(LOG_DEBUG, "** %s: mid=0x%x: removed 4\n",
               coap_session_str(session), q->id);
      if (q->pdu->type == COAP_MESSAGE_CON && context->nack_handler)
        context->nack_handler(session, q->pdu, reason, q->id);
      coap_delete_node(q);
      q = p->next;
    } else {
      p = q;
      q = q->next;
    }
  }
}

void
coap_cancel_all_messages(coap_context_t *context, coap_session_t *session,
  const uint8_t *token, size_t token_length) {
  /* cancel all messages in sendqueue that belong to session
   * and use the specified token */
  coap_queue_t **p, *q;

  if (!context->sendqueue)
    return;

  p = &context->sendqueue;
  q = *p;

  while (q) {
    if (q->session == session &&
      token_match(token, token_length,
        q->pdu->token, q->pdu->token_length)) {
      *p = q->next;
      coap_log(LOG_DEBUG, "** %s: mid=0x%x: removed 6\n",
               coap_session_str(session), q->id);
      if (q->pdu->type == COAP_MESSAGE_CON && session->con_active) {
        session->con_active--;
        if (session->state == COAP_SESSION_STATE_ESTABLISHED)
          /* Flush out any entries on session->delayqueue */
          coap_session_connected(session);
      }
      coap_delete_node(q);
    } else {
      p = &(q->next);
    }
    q = *p;
  }
}

coap_pdu_t *
coap_new_error_response(const coap_pdu_t *request, coap_pdu_code_t code,
  coap_opt_filter_t *opts) {
  coap_opt_iterator_t opt_iter;
  coap_pdu_t *response;
  size_t size = request->token_length;
  unsigned char type;
  coap_opt_t *option;
  coap_option_num_t opt_num = 0;        /* used for calculating delta-storage */

#if COAP_ERROR_PHRASE_LENGTH > 0
  const char *phrase;
  if (code != COAP_RESPONSE_CODE(508)) {
    phrase = coap_response_phrase(code);

    /* Need some more space for the error phrase and payload start marker */
    if (phrase)
      size += strlen(phrase) + 1;
  }
  else {
    /*
     * Need space for IP for 5.08 response which is filled in in
     * coap_send_internal()
     * https://www.rfc-editor.org/rfc/rfc8768.html#section-4
     */
    phrase = NULL;
    size += INET6_ADDRSTRLEN;
  }
#endif

  assert(request);

  /* cannot send ACK if original request was not confirmable */
  type = request->type == COAP_MESSAGE_CON
    ? COAP_MESSAGE_ACK
    : COAP_MESSAGE_NON;

  /* Estimate how much space we need for options to copy from
   * request. We always need the Token, for 4.02 the unknown critical
   * options must be included as well. */

  /* we do not want these */
  coap_option_filter_unset(opts, COAP_OPTION_CONTENT_FORMAT);
  coap_option_filter_unset(opts, COAP_OPTION_HOP_LIMIT);
  /* Unsafe to send this back */
  coap_option_filter_unset(opts, COAP_OPTION_OSCORE);

  coap_option_iterator_init(request, &opt_iter, opts);

  /* Add size of each unknown critical option. As known critical
     options as well as elective options are not copied, the delta
     value might grow.
   */
  while ((option = coap_option_next(&opt_iter))) {
    uint16_t delta = opt_iter.number - opt_num;
    /* calculate space required to encode (opt_iter.number - opt_num) */
    if (delta < 13) {
      size++;
    } else if (delta < 269) {
      size += 2;
    } else {
      size += 3;
    }

    /* add coap_opt_length(option) and the number of additional bytes
     * required to encode the option length */

    size += coap_opt_length(option);
    switch (*option & 0x0f) {
    case 0x0e:
      size++;
      /* fall through */
    case 0x0d:
      size++;
      break;
    default:
      ;
    }

    opt_num = opt_iter.number;
  }

  /* Now create the response and fill with options and payload data. */
  response = coap_pdu_init(type, code, request->mid, size);
  if (response) {
    /* copy token */
    if (!coap_add_token(response, request->token_length,
      request->token)) {
      coap_log(LOG_DEBUG, "cannot add token to error response\n");
      coap_delete_pdu(response);
      return NULL;
    }

    /* copy all options */
    coap_option_iterator_init(request, &opt_iter, opts);
    while ((option = coap_option_next(&opt_iter))) {
      coap_add_option_internal(response, opt_iter.number,
                               coap_opt_length(option),
                               coap_opt_value(option));
    }

#if COAP_ERROR_PHRASE_LENGTH > 0
    /* note that diagnostic messages do not need a Content-Format option. */
    if (phrase)
      coap_add_data(response, (size_t)strlen(phrase), (const uint8_t *)phrase);
#endif
  }

  return response;
}

#if COAP_SERVER_SUPPORT
/**
 * Quick hack to determine the size of the resource description for
 * .well-known/core.
 */
COAP_STATIC_INLINE ssize_t
get_wkc_len(coap_context_t *context, const coap_string_t *query_filter) {
  unsigned char buf[1];
  size_t len = 0;

  if (coap_print_wellknown(context, buf, &len, UINT_MAX, query_filter) &
        COAP_PRINT_STATUS_ERROR) {
    coap_log(LOG_WARNING, "cannot determine length of /.well-known/core\n");
    return -1L;
  }

  return len;
}

#define SZX_TO_BYTES(SZX) ((size_t)(1 << ((SZX) + 4)))

static void
free_wellknown_response(coap_session_t *session COAP_UNUSED, void *app_ptr) {
  coap_delete_string(app_ptr);
}

static void
hnd_get_wellknown(coap_resource_t *resource,
                  coap_session_t *session,
                  const coap_pdu_t *request,
                  const coap_string_t *query,
                  coap_pdu_t *response) {
  size_t len = 0;
  coap_string_t *data_string = NULL;
  int result = 0;
  ssize_t wkc_len = get_wkc_len(session->context, query);

  if (wkc_len) {
    if (wkc_len < 0)
      goto error;
    data_string = coap_new_string(wkc_len);
    if (!data_string)
      goto error;

    len = wkc_len;
    result = coap_print_wellknown(session->context, data_string->s, &len, 0,
                                  query);
    if ((result & COAP_PRINT_STATUS_ERROR) != 0) {
      coap_log(LOG_DEBUG, "coap_print_wellknown failed\n");
      goto error;
    }
    assert (len <= (size_t)wkc_len);
    data_string->length = len;

    if (!(session->block_mode & COAP_BLOCK_USE_LIBCOAP)) {
      uint8_t buf[4];

      if (!coap_insert_option(response, COAP_OPTION_CONTENT_FORMAT,
                         coap_encode_var_safe(buf, sizeof(buf),
                         COAP_MEDIATYPE_APPLICATION_LINK_FORMAT), buf)) {
        goto error;
      }
      if (response->used_size + len + 1 > response->max_size) {
        /*
         * Data does not fit into a packet and no libcoap block support
         * +1 for end of options marker
         */
        coap_log(LOG_DEBUG,
                 ".well-known/core: truncating data length to %zu from %zu\n",
                 len, response->max_size  - response->used_size - 1);
        len = response->max_size - response->used_size - 1;
      }
      if (!coap_add_data(response, len, data_string->s)) {
        goto error;
      }
      free_wellknown_response(session, data_string);
    } else if (!coap_add_data_large_response(resource, session, request,
                                             response, query,
                                      COAP_MEDIATYPE_APPLICATION_LINK_FORMAT,
                                             -1, 0, data_string->length,
                                             data_string->s,
                                             free_wellknown_response,
                                             data_string)) {
      goto error_released;
    }
  }
  response->code = COAP_RESPONSE_CODE(205);
  return;

error:
  free_wellknown_response(session, data_string);
error_released:
  if (response->code == 0) {
    /* set error code 5.03 and remove all options and data from response */
    response->code = COAP_RESPONSE_CODE(503);
    response->used_size = response->token_length;
    response->data = NULL;
  }
}
#endif /* COAP_SERVER_SUPPORT */

/**
 * This function cancels outstanding messages for the session and
 * token specified in @p sent. Any observation relationship for
 * sent->session and the token are removed. Calling this function is
 * required when receiving an RST message (usually in response to a
 * notification) or a GET request with the Observe option set to 1.
 *
 * This function returns @c 0 when the token is unknown with this
 * peer, or a value greater than zero otherwise.
 */
static int
coap_cancel(coap_context_t *context, const coap_queue_t *sent) {
  coap_binary_t token = { 0, NULL };
  int num_cancelled = 0;    /* the number of observers cancelled */

  (void)context;
  /* remove observer for this resource, if any
   * get token from sent and try to find a matching resource. Uh!
   */

  COAP_SET_STR(&token, sent->pdu->token_length, sent->pdu->token);

#if COAP_SERVER_SUPPORT
  RESOURCES_ITER(context->resources, r) {
    coap_cancel_all_messages(context, sent->session, token.s, token.length);
    num_cancelled += coap_delete_observer(r, sent->session, &token);
  }
#endif /* COAP_SERVER_SUPPORT */

  return num_cancelled;
}

#if COAP_SERVER_SUPPORT
/**
 * Internal flags to control the treatment of responses (specifically
 * in presence of the No-Response option).
 */
enum respond_t { RESPONSE_DEFAULT, RESPONSE_DROP, RESPONSE_SEND };

/*
 * Checks for No-Response option in given @p request and
 * returns @c RESPONSE_DROP if @p response should be suppressed
 * according to RFC 7967.
 *
 * If the response is a confirmable piggybacked response and RESPONSE_DROP,
 * change it to an empty ACK and @c RESPONSE_SEND so the client does not keep
 * on retrying.
 *
 * Checks if the response code is 0.00 and if either the session is reliable or
 * non-confirmable, @c RESPONSE_DROP is also returned.
 *
 * Multicast response checking is also carried out.
 *
 * NOTE: It is the responsibility of the application to determine whether
 * a delayed separate response should be sent as the original requesting packet
 * containing the No-Response option has long since gone.
 *
 * The value of the No-Response option is encoded as
 * follows:
 *
 * @verbatim
 *  +-------+-----------------------+-----------------------------------+
 *  | Value | Binary Representation |          Description              |
 *  +-------+-----------------------+-----------------------------------+
 *  |   0   |      <empty>          | Interested in all responses.      |
 *  +-------+-----------------------+-----------------------------------+
 *  |   2   |      00000010         | Not interested in 2.xx responses. |
 *  +-------+-----------------------+-----------------------------------+
 *  |   8   |      00001000         | Not interested in 4.xx responses. |
 *  +-------+-----------------------+-----------------------------------+
 *  |  16   |      00010000         | Not interested in 5.xx responses. |
 *  +-------+-----------------------+-----------------------------------+
 * @endverbatim
 *
 * @param request  The CoAP request to check for the No-Response option.
 *                 This parameter must not be NULL.
 * @param response The response that is potentially suppressed.
 *                 This parameter must not be NULL.
 * @param session  The session this request/response are associated with.
 *                 This parameter must not be NULL.
 * @return RESPONSE_DEFAULT when no special treatment is requested,
 *         RESPONSE_DROP    when the response must be discarded, or
 *         RESPONSE_SEND    when the response must be sent.
 */
static enum respond_t
no_response(coap_pdu_t *request, coap_pdu_t *response,
            coap_session_t *session, coap_resource_t *resource) {
  coap_opt_t *nores;
  coap_opt_iterator_t opt_iter;
  unsigned int val = 0;

  assert(request);
  assert(response);

  if (COAP_RESPONSE_CLASS(response->code) > 0) {
    nores = coap_check_option(request, COAP_OPTION_NORESPONSE, &opt_iter);

    if (nores) {
      val = coap_decode_var_bytes(coap_opt_value(nores), coap_opt_length(nores));

      /* The response should be dropped when the bit corresponding to
       * the response class is set (cf. table in function
       * documentation). When a No-Response option is present and the
       * bit is not set, the sender explicitly indicates interest in
       * this response. */
      if (((1 << (COAP_RESPONSE_CLASS(response->code) - 1)) & val) > 0) {
        /* Should be dropping the response */
        if (response->type == COAP_MESSAGE_ACK &&
            COAP_PROTO_NOT_RELIABLE(session->proto)) {
          /* Still need to ACK the request */
          response->code = 0;
          /* Remove token/data from piggybacked acknowledgment PDU */
          response->token_length = 0;
          response->used_size = 0;
          return RESPONSE_SEND;
        }
        else {
          return RESPONSE_DROP;
        }
      } else {
        /* True for mcast as well RFC7967 2.1 */
        return RESPONSE_SEND;
      }
    } else if (resource && session->context->mcast_per_resource &&
               coap_is_mcast(&session->addr_info.local)) {
      /* Handle any mcast suppression specifics if no NoResponse option */
      if ((resource->flags &
                    COAP_RESOURCE_FLAGS_LIB_ENA_MCAST_SUPPRESS_2_XX) &&
                 COAP_RESPONSE_CLASS(response->code) == 2) {
        return RESPONSE_DROP;
      } else if ((resource->flags &
                    COAP_RESOURCE_FLAGS_LIB_ENA_MCAST_SUPPRESS_2_05) &&
          response->code == COAP_RESPONSE_CODE(205)) {
        if (response->data == NULL)
          return RESPONSE_DROP;
      } else if ((resource->flags &
                    COAP_RESOURCE_FLAGS_LIB_DIS_MCAST_SUPPRESS_4_XX) == 0 &&
                 COAP_RESPONSE_CLASS(response->code) == 4) {
        return RESPONSE_DROP;
      } else if ((resource->flags &
                   COAP_RESOURCE_FLAGS_LIB_DIS_MCAST_SUPPRESS_5_XX) == 0 &&
                 COAP_RESPONSE_CLASS(response->code) == 5) {
        return RESPONSE_DROP;
      }
    }
  }
  else if (COAP_PDU_IS_EMPTY(response) &&
           (response->type == COAP_MESSAGE_NON ||
            COAP_PROTO_RELIABLE(session->proto))) {
    /* response is 0.00, and this is reliable or non-confirmable */
    return RESPONSE_DROP;
  }

  /*
   * Do not send error responses for requests that were received via
   * IP multicast.  RFC7252 8.1
   */

  if (coap_is_mcast(&session->addr_info.local)) {
    if (request->type == COAP_MESSAGE_NON &&
        response->type == COAP_MESSAGE_RST)
      return RESPONSE_DROP;

    if ((!resource || session->context->mcast_per_resource == 0) &&
        COAP_RESPONSE_CLASS(response->code) > 2)
      return RESPONSE_DROP;
  }

  /* Default behavior applies when we are not dealing with a response
   * (class == 0) or the request did not contain a No-Response option.
   */
  return RESPONSE_DEFAULT;
}

static coap_str_const_t coap_default_uri_wellknown =
          { sizeof(COAP_DEFAULT_URI_WELLKNOWN)-1,
           (const uint8_t *)COAP_DEFAULT_URI_WELLKNOWN };

/* Initialized in coap_startup() */
static coap_resource_t resource_uri_wellknown;

static void
handle_request(coap_context_t *context, coap_session_t *session, coap_pdu_t *pdu) {
  coap_method_handler_t h = NULL;
  coap_pdu_t *response = NULL;
  coap_opt_filter_t opt_filter;
  coap_resource_t *resource = NULL;
  /* The respond field indicates whether a response must be treated
   * specially due to a No-Response option that declares disinterest
   * or interest in a specific response class. DEFAULT indicates that
   * No-Response has not been specified. */
  enum respond_t respond = RESPONSE_DEFAULT;
  coap_opt_iterator_t opt_iter;
  coap_opt_t *opt;
  int is_proxy_uri = 0;
  int is_proxy_scheme = 0;
  int skip_hop_limit_check = 0;
  int resp;
  int send_early_empty_ack = 0;
  coap_binary_t token = { pdu->token_length, pdu->token };
  coap_string_t *query = NULL;
  coap_opt_t *observe = NULL;
  coap_string_t *uri_path = NULL;
  int added_block = 0;
#ifndef WITHOUT_ASYNC
  coap_bin_const_t tokenc = { pdu->token_length, pdu->token };
  coap_async_t *async;
#endif /* WITHOUT_ASYNC */

  if (coap_is_mcast(&session->addr_info.local)) {
    if (COAP_PROTO_RELIABLE(session->proto) || pdu->type != COAP_MESSAGE_NON) {
      coap_log(LOG_INFO, "Invalid multicast packet received RFC7252 8.1\n");
      return;
    }
  }
#ifndef WITHOUT_ASYNC
  async = coap_find_async(session, tokenc);
  if (async) {
    coap_tick_t now;

    coap_ticks(&now);
    if (async->delay == 0 || async->delay > now) {
      /* re-transmit missing ACK (only if CON) */
      coap_log(LOG_INFO, "Retransmit async response\n");
      coap_send_ack(session, pdu);
      /* and do not pass on to the upper layers */
      return;
    }
  }
#endif /* WITHOUT_ASYNC */

  coap_option_filter_clear(&opt_filter);
  opt = coap_check_option(pdu, COAP_OPTION_PROXY_SCHEME, &opt_iter);
  if (opt)
    is_proxy_scheme = 1;

  opt = coap_check_option(pdu, COAP_OPTION_PROXY_URI, &opt_iter);
  if (opt)
    is_proxy_uri = 1;

  if (is_proxy_scheme || is_proxy_uri) {
    coap_uri_t uri;

    if (!context->proxy_uri_resource) {
      /* Need to return a 5.05 RFC7252 Section 5.7.2 */
      coap_log(LOG_DEBUG, "Proxy-%s support not configured\n",
               is_proxy_scheme ? "Scheme" : "Uri");
      resp = 505;
      goto fail_response;
    }
    if (((size_t)pdu->code - 1 <
                (sizeof(resource->handler) / sizeof(resource->handler[0]))) &&
        !(context->proxy_uri_resource->handler[pdu->code - 1])) {
      /* Need to return a 5.05 RFC7252 Section 5.7.2 */
      coap_log(LOG_DEBUG, "Proxy-%s code %d.%02d handler not supported\n",
               is_proxy_scheme ? "Scheme" : "Uri",
               pdu->code/100,  pdu->code%100);
      resp = 505;
      goto fail_response;
    }

    /* Need to check if authority is the proxy endpoint RFC7252 Section 5.7.2 */
    if (is_proxy_uri) {
      if (coap_split_proxy_uri(coap_opt_value(opt),
                               coap_opt_length(opt), &uri) < 0) {
        /* Need to return a 5.05 RFC7252 Section 5.7.2 */
        coap_log(LOG_DEBUG, "Proxy-URI not decodable\n");
        resp = 505;
        goto fail_response;
      }
    }
    else {
      memset(&uri, 0, sizeof(uri));
      opt = coap_check_option(pdu, COAP_OPTION_URI_HOST, &opt_iter);
      if (opt) {
        uri.host.length = coap_opt_length(opt);
        uri.host.s = coap_opt_value(opt);
      } else
        uri.host.length = 0;
    }

    resource = context->proxy_uri_resource;
    if (uri.host.length && resource->proxy_name_count &&
        resource->proxy_name_list) {
      size_t i;

      if (resource->proxy_name_count == 1 &&
          resource->proxy_name_list[0]->length == 0) {
        /* If proxy_name_list[0] is zero length, then this is the endpoint */
        i = 0;
      } else {
        for (i = 0; i < resource->proxy_name_count; i++) {
          if (coap_string_equal(&uri.host, resource->proxy_name_list[i])) {
            break;
          }
        }
      }
      if (i != resource->proxy_name_count) {
        /* This server is hosting the proxy connection endpoint */
        if (pdu->crit_opt) {
          /* Cannot handle critical option */
          pdu->crit_opt = 0;
          resp = 402;
          goto fail_response;
        }
        is_proxy_uri = 0;
        is_proxy_scheme = 0;
        skip_hop_limit_check = 1;
      }
    }
    resource = NULL;
  }

  if (!skip_hop_limit_check) {
    opt = coap_check_option(pdu, COAP_OPTION_HOP_LIMIT, &opt_iter);
    if (opt) {
      size_t hop_limit;
      uint8_t buf[4];

      hop_limit =
        coap_decode_var_bytes (coap_opt_value (opt), coap_opt_length (opt));
      if (hop_limit == 1) {
        /* coap_send_internal() will fill in the IP address for us */
        resp = 508;
        goto fail_response;
      }
      else if (hop_limit < 1 || hop_limit > 255) {
        /* Need to return a 4.00 RFC8768 Section 3 */
        coap_log(LOG_INFO, "Invalid Hop Limit\n");
        resp = 400;
        goto fail_response;
      }
      hop_limit--;
      coap_update_option(pdu, COAP_OPTION_HOP_LIMIT,
                         coap_encode_var_safe8(buf, sizeof(buf), hop_limit),
                         buf);
    }
  }

  uri_path = coap_get_uri_path(pdu);
  if (!uri_path)
    return;

  if (!is_proxy_uri && !is_proxy_scheme) {
    /* try to find the resource from the request URI */
    coap_str_const_t uri_path_c = { uri_path->length, uri_path->s };
    resource = coap_get_resource_from_uri_path(context, &uri_path_c);
  }

  if ((resource == NULL) || (resource->is_unknown == 1) ||
      (resource->is_proxy_uri == 1)) {
    /* The resource was not found or there is an unexpected match against the
     * resource defined for handling unknown or proxy URIs.
     */
    if (resource != NULL)
      /* Close down unexpected match */
      resource = NULL;
    /*
     * Check if the request URI happens to be the well-known URI, or if the
     * unknown resource handler is defined, a PUT or optionally other methods,
     * if configured, for the unknown handler.
     *
     * if a PROXY URI/Scheme request and proxy URI handler defined, call the
     *  proxy URI handler
     *
     * else if well-known URI generate a default response
     *
     * else if unknown URI handler defined, call the unknown
     *  URI handler (to allow for potential generation of resource
     *  [RFC7272 5.8.3]) if the appropriate method is defined.
     *
     * else if DELETE return 2.02 (RFC7252: 5.8.4.  DELETE)
     *
     * else return 4.04 */

    if (is_proxy_uri || is_proxy_scheme) {
      resource = context->proxy_uri_resource;
    } else if (coap_string_equal(uri_path, &coap_default_uri_wellknown)) {
      /* request for .well-known/core */
      resource = &resource_uri_wellknown;
    } else if ((context->unknown_resource != NULL) &&
               ((size_t)pdu->code - 1 <
                (sizeof(resource->handler) / sizeof(coap_method_handler_t))) &&
               (context->unknown_resource->handler[pdu->code - 1])) {
      /*
       * The unknown_resource can be used to handle undefined resources
       * for a PUT request and can support any other registered handler
       * defined for it
       * Example set up code:-
       *   r = coap_resource_unknown_init(hnd_put_unknown);
       *   coap_register_request_handler(r, COAP_REQUEST_POST,
       *                                 hnd_post_unknown);
       *   coap_register_request_handler(r, COAP_REQUEST_GET,
       *                                 hnd_get_unknown);
       *   coap_register_request_handler(r, COAP_REQUEST_DELETE,
       *                                 hnd_delete_unknown);
       *   coap_add_resource(ctx, r);
       *
       * Note: It is not possible to observe the unknown_resource, a separate
       *       resource must be created (by PUT or POST) which has a GET
       *       handler to be observed
       */
      resource = context->unknown_resource;
    } else if (pdu->code == COAP_REQUEST_CODE_DELETE) {
      /*
       * Request for DELETE on non-existant resource (RFC7252: 5.8.4.  DELETE)
       */
      coap_log(LOG_DEBUG, "request for unknown resource '%*.*s',"
                          " return 2.02\n",
                          (int)uri_path->length,
                          (int)uri_path->length,
                          uri_path->s);
      resp = 202;
      goto fail_response;
    } else { /* request for any another resource, return 4.04 */

      coap_log(LOG_DEBUG, "request for unknown resource '%*.*s', return 4.04\n",
               (int)uri_path->length, (int)uri_path->length, uri_path->s);
      resp = 404;
      goto fail_response;
    }

  }

  /* the resource was found, check if there is a registered handler */
  if ((size_t)pdu->code - 1 <
    sizeof(resource->handler) / sizeof(coap_method_handler_t))
    h = resource->handler[pdu->code - 1];

  if (h) {
    if (context->mcast_per_resource &&
        (resource->flags & COAP_RESOURCE_FLAGS_HAS_MCAST_SUPPORT) == 0 &&
        coap_is_mcast(&session->addr_info.local)) {
      resp = 405;
      goto fail_response;
    }

    response = coap_pdu_init(pdu->type == COAP_MESSAGE_CON
      ? COAP_MESSAGE_ACK
      : COAP_MESSAGE_NON,
      0, pdu->mid, coap_session_max_pdu_size(session));
    if (!response) {
      coap_log(LOG_ERR, "could not create response PDU\n");
      resp = 500;
      goto fail_response;
    }
#ifndef WITHOUT_ASYNC
    /* If handling a separate response, need CON, not ACK response */
    if (async && pdu->type == COAP_MESSAGE_CON)
      response->type = COAP_MESSAGE_CON;
#endif /* WITHOUT_ASYNC */

    /* Implementation detail: coap_add_token() immediately returns 0
       if response == NULL */
    if (coap_add_token(response, pdu->token_length, pdu->token)) {
      int observe_action = COAP_OBSERVE_CANCEL;
      coap_block_b_t block;

      query = coap_get_query(pdu);
      /* check for Observe option RFC7641 and RFC8132 */
      if (resource->observable &&
          (pdu->code == COAP_REQUEST_CODE_GET ||
           pdu->code == COAP_REQUEST_CODE_FETCH)) {
        observe = coap_check_option(pdu, COAP_OPTION_OBSERVE, &opt_iter);
        if (observe) {
          observe_action =
            coap_decode_var_bytes(coap_opt_value(observe),
              coap_opt_length(observe));

          if (observe_action == COAP_OBSERVE_ESTABLISH) {
            coap_subscription_t *subscription;

            if (coap_get_block_b(session, pdu, COAP_OPTION_BLOCK2, &block)) {
              if (block.num != 0) {
                response->code = COAP_RESPONSE_CODE(400);
                goto skip_handler;
              }
            }
            subscription = coap_add_observer(resource, session, &token,
                                             pdu);
            if (subscription) {
              uint8_t buf[4];

              coap_touch_observer(context, session, &token);
              coap_add_option_internal(response, COAP_OPTION_OBSERVE,
                                       coap_encode_var_safe(buf, sizeof (buf),
                                                            resource->observe),
                                       buf);
            }
          }
          else if (observe_action == COAP_OBSERVE_CANCEL) {
            coap_delete_observer(resource, session, &token);
          }
          else {
            coap_log(LOG_INFO, "observe: unexpected action %d\n", observe_action);
          }
        }
      }

      /* TODO for non-proxy requests */
      if (resource == context->proxy_uri_resource &&
          COAP_PROTO_NOT_RELIABLE(session->proto) &&
          pdu->type == COAP_MESSAGE_CON) {
        /* Make the proxy response separate and fix response later */
        send_early_empty_ack = 1;
      }
      if (send_early_empty_ack) {
        coap_send_ack(session, pdu);
        if (pdu->mid == session->last_con_mid) {
          /* request has already been processed - do not process it again */
          coap_log(LOG_DEBUG,
                   "Duplicate request with mid=0x%04x - not processed\n",
                   pdu->mid);
          goto drop_it_no_debug;
        }
        session->last_con_mid = pdu->mid;
      }
      if (session->block_mode & COAP_BLOCK_USE_LIBCOAP) {
        if (coap_handle_request_put_block(context, session, pdu, response,
                                          resource, uri_path, observe,
                                          query, h, &added_block)) {
          goto skip_handler;
        }

        if (coap_handle_request_send_block(session, pdu, response, resource,
                                           query)) {
          goto skip_handler;
        }
      }

      /*
       * Call the request handler with everything set up
       */
      coap_log(LOG_DEBUG, "call custom handler for resource '%*.*s'\n",
               (int)resource->uri_path->length, (int)resource->uri_path->length,
               resource->uri_path->s);
      h(resource, session, pdu, query, response);

      /* Check if lg_xmit generated and update PDU code if so */
      coap_check_code_lg_xmit(session, response, resource, query, pdu->code);

skip_handler:
      if (send_early_empty_ack &&
          response->type == COAP_MESSAGE_ACK) {
        /* Response is now separate - convert to CON as needed */
        response->type = COAP_MESSAGE_CON;
        /* Check for empty ACK - need to drop as already sent */
        if (response->code == 0) {
          goto drop_it_no_debug;
        }
      }
      respond = no_response(pdu, response, session, resource);
      if (respond != RESPONSE_DROP) {
        coap_mid_t mid = pdu->mid;
        if (COAP_RESPONSE_CLASS(response->code) != 2) {
          if (observe) {
            coap_remove_option(response, COAP_OPTION_OBSERVE);
          }
        }
        if (COAP_RESPONSE_CLASS(response->code) > 2) {
          if (observe)
            coap_delete_observer(resource, session, &token);
          if (added_block)
            coap_remove_option(response, COAP_OPTION_BLOCK1);
        }

        /* If original request contained a token, and the registered
         * application handler made no changes to the response, then
         * this is an empty ACK with a token, which is a malformed
         * PDU */
        if ((response->type == COAP_MESSAGE_ACK)
          && (response->code == 0)) {
          /* Remove token from otherwise-empty acknowledgment PDU */
          response->token_length = 0;
          response->used_size = 0;
        }

        if (!coap_is_mcast(&session->addr_info.local) ||
            (context->mcast_per_resource &&
              resource &&
              (resource->flags & COAP_RESOURCE_FLAGS_LIB_DIS_MCAST_DELAYS))) {
          if (coap_send_internal(session, response) == COAP_INVALID_MID) {
            coap_log(LOG_DEBUG, "cannot send response for mid=0x%x\n", mid);
          }
        } else {
          /* Need to delay mcast response */
          coap_queue_t *node = coap_new_node();
          uint8_t r;
          coap_tick_t delay;

          if (!node) {
            coap_log(LOG_DEBUG, "mcast delay: insufficient memory\n");
            goto clean_up;
          }
          if (!coap_pdu_encode_header(response, session->proto)) {
            coap_delete_node(node);
            goto clean_up;
          }

          node->id = response->mid;
          node->pdu = response;
          node->is_mcast = 1;
          coap_prng(&r, sizeof(r));
          delay = (COAP_DEFAULT_LEISURE_TICKS(session) * r) / 256;
          coap_log(LOG_DEBUG,
                   "   %s: mid=0x%x: mcast response delayed for %u.%03u secs\n",
                   coap_session_str(session),
                   response->mid,
                   (unsigned int)(delay / COAP_TICKS_PER_SECOND),
                   (unsigned int)((delay % COAP_TICKS_PER_SECOND) *
                     1000 / COAP_TICKS_PER_SECOND));
          node->timeout = (unsigned int)delay;
          /* Use this to delay transmission */
          coap_wait_ack(session->context, session, node);
        }
      } else {
        coap_log(LOG_DEBUG, "   %s: mid=0x%x: response dropped\n",
                 coap_session_str(session),
                 response->mid);
        coap_show_pdu(LOG_DEBUG, response);
drop_it_no_debug:
        coap_delete_pdu(response);
      }
clean_up:
      if (query)
        coap_delete_string(query);
    } else {
      coap_log(LOG_WARNING, "cannot generate response\r\n");
      coap_delete_pdu(response);
    }
  } else {
    resp = 405;
    goto fail_response;
  }

  coap_delete_string(uri_path);
  return;

fail_response:
  response =
     coap_new_error_response(pdu, COAP_RESPONSE_CODE(resp),
       &opt_filter);
  if (response)
    goto skip_handler;
  coap_delete_string(uri_path);
}
#endif /* COAP_SERVER_SUPPORT */

#if COAP_CLIENT_SUPPORT
static void
handle_response(coap_context_t *context, coap_session_t *session,
                coap_pdu_t *sent, coap_pdu_t *rcvd) {
  /* In a lossy context, the ACK of a separate response may have
   * been lost, so we need to stop retransmitting requests with the
   * same token.
   */
  if (rcvd->type != COAP_MESSAGE_ACK)
    coap_cancel_all_messages(context, session, rcvd->token, rcvd->token_length);

  /* Check for message duplication */
  if (COAP_PROTO_NOT_RELIABLE(session->proto)) {
    if (rcvd->type == COAP_MESSAGE_CON) {
      if (rcvd->mid == session->last_con_mid) {
        /* Duplicate response */
        return;
      }
      session->last_con_mid = rcvd->mid;
    } else if (rcvd->type == COAP_MESSAGE_ACK) {
      if (rcvd->mid == session->last_ack_mid) {
        /* Duplicate response */
        return;
      }
      session->last_ack_mid = rcvd->mid;
    }
  }

  if (session->block_mode & COAP_BLOCK_USE_LIBCOAP) {
    /* See if need to send next block to server */
    if (coap_handle_response_send_block(session, sent, rcvd)) {
      /* Next block transmitted, no need to inform app */
      coap_send_ack(session, rcvd);
      return;
    }

    /* Need to see if needing to request next block */
    if (coap_handle_response_get_block(context, session, sent, rcvd,
                                       COAP_RECURSE_OK)) {
      /* Next block requested, no need to inform app */
      coap_send_ack(session, rcvd);
      return;
    }
  }

  /* Call application-specific response handler when available. */
  if (context->response_handler) {
    if (context->response_handler(session, sent, rcvd,
                                  rcvd->mid) == COAP_RESPONSE_FAIL)
      coap_send_rst(session, rcvd);
    else
      coap_send_ack(session, rcvd);
  }
  else {
    coap_send_ack(session, rcvd);
  }
}
#endif /* COAP_CLIENT_SUPPORT */

#if !COAP_DISABLE_TCP
static void
handle_signaling(coap_context_t *context, coap_session_t *session,
  coap_pdu_t *pdu) {
  coap_opt_iterator_t opt_iter;
  coap_opt_t *option;
  int set_mtu = 0;

  coap_option_iterator_init(pdu, &opt_iter, COAP_OPT_ALL);

  if (pdu->code == COAP_SIGNALING_CODE_CSM) {
    while ((option = coap_option_next(&opt_iter))) {
      if (opt_iter.number == COAP_SIGNALING_OPTION_MAX_MESSAGE_SIZE) {
        coap_session_set_mtu(session, coap_decode_var_bytes(coap_opt_value(option),
          coap_opt_length(option)));
        set_mtu = 1;
      } else if (opt_iter.number == COAP_SIGNALING_OPTION_BLOCK_WISE_TRANSFER) {
        session->csm_block_supported = 1;
      }
    }
    if (set_mtu) {
      if (session->mtu > COAP_BERT_BASE && session->csm_block_supported)
        session->csm_bert_rem_support = 1;
      else
        session->csm_bert_rem_support = 0;
    }
    if (session->state == COAP_SESSION_STATE_CSM)
      coap_session_connected(session);
  } else if (pdu->code == COAP_SIGNALING_CODE_PING) {
    coap_pdu_t *pong = coap_pdu_init(COAP_MESSAGE_CON, COAP_SIGNALING_CODE_PONG, 0, 1);
    if (context->ping_handler) {
      context->ping_handler(session, pdu, pdu->mid);
    }
    if (pong) {
      coap_add_option_internal(pong, COAP_SIGNALING_OPTION_CUSTODY, 0, NULL);
      coap_send_internal(session, pong);
    }
  } else if (pdu->code == COAP_SIGNALING_CODE_PONG) {
    session->last_pong = session->last_rx_tx;
    if (context->pong_handler) {
      context->pong_handler(session, pdu, pdu->mid);
    }
  } else if (pdu->code == COAP_SIGNALING_CODE_RELEASE
          || pdu->code == COAP_SIGNALING_CODE_ABORT) {
    coap_session_disconnected(session, COAP_NACK_RST);
  }
}
#endif /* !COAP_DISABLE_TCP */

void
coap_dispatch(coap_context_t *context, coap_session_t *session,
  coap_pdu_t *pdu) {
  coap_queue_t *sent = NULL;
  coap_pdu_t *response;
  coap_opt_filter_t opt_filter;
  int is_ping_rst;

  if (LOG_DEBUG <= coap_get_log_level()) {
    /* FIXME: get debug to work again **
    unsigned char addr[INET6_ADDRSTRLEN+8], localaddr[INET6_ADDRSTRLEN+8];
    if (coap_print_addr(remote, addr, INET6_ADDRSTRLEN+8) &&
        coap_print_addr(&packet->dst, localaddr, INET6_ADDRSTRLEN+8) )
      coap_log(LOG_DEBUG, "** received %d bytes from %s on interface %s:\n",
            (int)msg_len, addr, localaddr);

            */
    coap_show_pdu(LOG_DEBUG, pdu);
  }

  memset(&opt_filter, 0, sizeof(coap_opt_filter_t));

  switch (pdu->type) {
    case COAP_MESSAGE_ACK:
      /* find message id in sendqueue to stop retransmission */
      coap_remove_from_queue(&context->sendqueue, session, pdu->mid, &sent);

      if (sent && session->con_active) {
        session->con_active--;
        if (session->state == COAP_SESSION_STATE_ESTABLISHED)
          /* Flush out any entries on session->delayqueue */
          coap_session_connected(session);
      }
      if (coap_option_check_critical(session, pdu, &opt_filter) == 0)
        goto cleanup;

#if COAP_SERVER_SUPPORT
      /* if sent code was >= 64 the message might have been a
       * notification. Then, we must flag the observer to be alive
       * by setting obs->fail_cnt = 0. */
      if (sent && COAP_RESPONSE_CLASS(sent->pdu->code) == 2) {
        const coap_binary_t token =
        { sent->pdu->token_length, sent->pdu->token };
        coap_touch_observer(context, sent->session, &token);
      }
#endif /* COAP_SERVER_SUPPORT */

      if (pdu->code == 0) {
        /* an empty ACK needs no further handling */
        goto cleanup;
      }

      break;

    case COAP_MESSAGE_RST:
      /* We have sent something the receiver disliked, so we remove
       * not only the message id but also the subscriptions we might
       * have. */
      is_ping_rst = 0;
      if (pdu->mid == session->last_ping_mid &&
          context->ping_timeout && session->last_ping > 0)
        is_ping_rst = 1;

      if (!is_ping_rst)
        coap_log(LOG_ALERT, "got RST for mid=0x%x\n", pdu->mid);

      if (session->con_active) {
        session->con_active--;
        if (session->state == COAP_SESSION_STATE_ESTABLISHED)
          /* Flush out any entries on session->delayqueue */
          coap_session_connected(session);
      }

      /* find message id in sendqueue to stop retransmission */
      coap_remove_from_queue(&context->sendqueue, session, pdu->mid, &sent);

      if (sent) {
        coap_cancel(context, sent);

        if (!is_ping_rst) {
          if(sent->pdu->type==COAP_MESSAGE_CON && context->nack_handler)
            context->nack_handler(sent->session, sent->pdu,
                                  COAP_NACK_RST, sent->id);
        }
        else {
          if (context->pong_handler) {
            context->pong_handler(session, pdu, pdu->mid);
          }
          session->last_pong = session->last_rx_tx;
          session->last_ping_mid = COAP_INVALID_MID;
        }
      }
#if COAP_SERVER_SUPPORT
      else {
        /* Need to check is there is a subscription active and delete it */
        RESOURCES_ITER(context->resources, r) {
          coap_subscription_t *obs, *tmp;
          LL_FOREACH_SAFE(r->subscribers, obs, tmp) {
            if (obs->pdu->mid == pdu->mid && obs->session == session) {
              coap_binary_t token = { 0, NULL };
              COAP_SET_STR(&token, obs->pdu->token_length, obs->pdu->token);
              coap_delete_observer(r, session, &token);
              goto cleanup;
            }
          }
        }
      }
#endif /* COAP_SERVER_SUPPORT */
      goto cleanup;

    case COAP_MESSAGE_NON:
      /* find transaction in sendqueue in case large response */
      coap_remove_from_queue(&context->sendqueue, session, pdu->mid, &sent);
      /* check for unknown critical options */
      if (coap_option_check_critical(session, pdu, &opt_filter) == 0) {
        coap_send_rst(session, pdu);
        goto cleanup;
      }
      break;

    case COAP_MESSAGE_CON:        /* check for unknown critical options */
      if (coap_option_check_critical(session, pdu, &opt_filter) == 0) {

        if (COAP_PDU_IS_REQUEST(pdu)) {
          response =
            coap_new_error_response(pdu, COAP_RESPONSE_CODE(402), &opt_filter);

          if (!response) {
            coap_log(LOG_WARNING,
                     "coap_dispatch: cannot create error response\n");
          } else {
            if (coap_send_internal(session, response) == COAP_INVALID_MID)
              coap_log(LOG_WARNING, "coap_dispatch: error sending response\n");
          }
        }
        else {
          coap_send_rst(session, pdu);
        }

        goto cleanup;
      }
    default: break;
  }

  /* Pass message to upper layer if a specific handler was
    * registered for a request that should be handled locally. */
#if !COAP_DISABLE_TCP
  if (COAP_PDU_IS_SIGNALING(pdu))
    handle_signaling(context, session, pdu);
  else
#endif /* !COAP_DISABLE_TCP */
#if COAP_SERVER_SUPPORT
  if (COAP_PDU_IS_REQUEST(pdu))
    handle_request(context, session, pdu);
  else
#endif /* COAP_SERVER_SUPPORT */
#if COAP_CLIENT_SUPPORT
  if (COAP_PDU_IS_RESPONSE(pdu))
    handle_response(context, session, sent ? sent->pdu : NULL, pdu);
  else
#endif /* COAP_CLIENT_SUPPORT */
  {
    if (COAP_PDU_IS_EMPTY(pdu)) {
      if (context->ping_handler) {
        context->ping_handler(session, pdu, pdu->mid);
      }
    }
    coap_log(LOG_DEBUG, "dropped message with invalid code (%d.%02d)\n",
             COAP_RESPONSE_CLASS(pdu->code),
      pdu->code & 0x1f);

    if (!coap_is_mcast(&session->addr_info.local)) {
      if (COAP_PDU_IS_EMPTY(pdu)) {
        if (session->proto != COAP_PROTO_TCP && session->proto != COAP_PROTO_TLS) {
          coap_tick_t now;
          coap_ticks(&now);
          if (session->last_tx_rst + COAP_TICKS_PER_SECOND/4 < now) {
            coap_send_message_type(session, pdu, COAP_MESSAGE_RST);
            session->last_tx_rst = now;
          }
        }
      }
      else {
        coap_send_message_type(session, pdu, COAP_MESSAGE_RST);
      }
    }
  }

cleanup:
  coap_delete_node(sent);
}

int
coap_handle_event(coap_context_t *context, coap_event_t event, coap_session_t *session) {
  coap_log(LOG_DEBUG, "***EVENT: 0x%04x\n", event);

  if (context->handle_event) {
    return context->handle_event(session, event);
  } else {
    return 0;
  }
}

int
coap_can_exit(coap_context_t *context) {
  coap_session_t *s, *rtmp;
  if (!context)
    return 1;
  if (context->sendqueue)
    return 0;
#if COAP_SERVER_SUPPORT
  coap_endpoint_t *ep;

  LL_FOREACH(context->endpoint, ep) {
    SESSIONS_ITER(ep->sessions, s, rtmp) {
      if (s->delayqueue)
        return 0;
      if (s->lg_xmit)
        return 0;
    }
  }
#endif /* COAP_SERVER_SUPPORT */
#if COAP_CLIENT_SUPPORT
  SESSIONS_ITER(context->sessions, s, rtmp) {
    if (s->delayqueue)
      return 0;
    if (s->lg_xmit)
      return 0;
  }
#endif /* COAP_CLIENT_SUPPORT */
  return 1;
}
#ifndef WITHOUT_ASYNC
coap_tick_t
coap_check_async(coap_context_t *context, coap_tick_t now) {
  coap_tick_t next_due = 0;
  coap_async_t *async, *tmp;

  LL_FOREACH_SAFE(context->async_state, async, tmp) {
    if (async->delay != 0 && async->delay <= now) {
      /* Send off the request to the application */
      handle_request(context, async->session, async->pdu);

      /* Remove this async entry as it has now fired */
      coap_free_async(async->session, async);
    }
    else {
      if (next_due == 0 || next_due > async->delay - now)
        next_due = async->delay - now;
    }
  }
  return next_due;
}
#endif /* WITHOUT_ASYNC */

static int coap_started = 0;

void coap_startup(void) {
  coap_tick_t now;
  uint64_t us;

  if (coap_started)
    return;
  coap_started = 1;
#if defined(HAVE_WINSOCK2_H)
  WORD wVersionRequested = MAKEWORD(2, 2);
  WSADATA wsaData;
  WSAStartup(wVersionRequested, &wsaData);
#endif
  coap_clock_init();
  coap_ticks(&now);
  us = coap_ticks_to_rt_us(now);
  /* Be accurate to the nearest (approx) us */
  coap_prng_init((unsigned int)us);
  coap_memory_init();
  coap_dtls_startup();
#if COAP_SERVER_SUPPORT
 static coap_str_const_t well_known = { sizeof(".well-known/core")-1,
                                        (const uint8_t *)".well-known/core" };
  memset(&resource_uri_wellknown, 0, sizeof(resource_uri_wellknown));
  resource_uri_wellknown.handler[COAP_REQUEST_GET-1] = hnd_get_wellknown;
  resource_uri_wellknown.flags = COAP_RESOURCE_FLAGS_HAS_MCAST_SUPPORT;
  resource_uri_wellknown.uri_path = &well_known;
#endif /* COAP_SERVER_SUPPORT */
}

void coap_cleanup(void) {
#if defined(HAVE_WINSOCK2_H)
  WSACleanup();
#endif
  coap_dtls_shutdown();
}

void
coap_register_response_handler(coap_context_t *context,
                               coap_response_handler_t handler) {
#if COAP_CLIENT_SUPPORT
  context->response_handler = handler;
#else /* ! COAP_CLIENT_SUPPORT */
  (void)context;
  (void)handler;
#endif /* COAP_CLIENT_SUPPORT */
}

void
coap_register_nack_handler(coap_context_t *context,
                           coap_nack_handler_t handler) {
  context->nack_handler = handler;
}

void
coap_register_ping_handler(coap_context_t *context,
                           coap_ping_handler_t handler) {
  context->ping_handler = handler;
}

void
coap_register_pong_handler(coap_context_t *context,
                           coap_pong_handler_t handler) {
  context->pong_handler = handler;
}

void
coap_register_option(coap_context_t *ctx, uint16_t type) {
  coap_option_filter_set(&ctx->known_options, type);
}

#if ! defined WITH_CONTIKI && ! defined WITH_LWIP && ! defined RIOT_VERSION
#if COAP_SERVER_SUPPORT
int
coap_join_mcast_group_intf(coap_context_t *ctx, const char *group_name,
                           const char *ifname) {
  struct ip_mreq mreq4;
  struct ipv6_mreq mreq6;
  struct addrinfo *resmulti = NULL, hints, *ainfo;
  int result = -1;
  coap_endpoint_t *endpoint;
  int mgroup_setup = 0;

  /* Need to have at least one endpoint! */
  assert(ctx->endpoint);
  if (!ctx->endpoint)
    return -1;

  /* Default is let the kernel choose */
  mreq6.ipv6mr_interface = 0;
  mreq4.imr_interface.s_addr = INADDR_ANY;

  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM;

  /* resolve the multicast group address */
  result = getaddrinfo(group_name, NULL, &hints, &resmulti);

  if (result != 0) {
    coap_log(LOG_ERR,
             "coap_join_mcast_group_intf: %s: "
              "Cannot resolve multicast address: %s\n",
             group_name, gai_strerror(result));
    goto finish;
  }

/* Need to do a windows equivalent at some point */
#ifndef _WIN32
  if (ifname) {
    /* interface specified - check if we have correct IPv4/IPv6 information */
    int done_ip4 = 0;
    int done_ip6 = 0;
#if defined(ESPIDF_VERSION)
    struct netif *netif;
#else /* !ESPIDF_VERSION */
    int ip4fd;
    struct ifreq ifr;
#endif /* !ESPIDF_VERSION */

    /* See which mcast address family types are being asked for */
    for (ainfo = resmulti; ainfo != NULL && !(done_ip4 == 1 && done_ip6 == 1);
         ainfo = ainfo->ai_next) {
      switch (ainfo->ai_family) {
      case AF_INET6:
        if (done_ip6)
          break;
        done_ip6 = 1;
#if defined(ESPIDF_VERSION)
        netif = netif_find(ifname);
        if (netif)
          mreq6.ipv6mr_interface = netif_get_index(netif);
        else
          coap_log(LOG_ERR,
                   "coap_join_mcast_group_intf: %s: "
                    "Cannot get IPv4 address: %s\n",
                    ifname, coap_socket_strerror());
#else /* !ESPIDF_VERSION */
        memset (&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
        ifr.ifr_name[IFNAMSIZ - 1] = '\000';

#ifdef HAVE_IF_NAMETOINDEX
        mreq6.ipv6mr_interface = if_nametoindex(ifr.ifr_name);
        if (mreq6.ipv6mr_interface == 0) {
          coap_log(LOG_WARNING, "coap_join_mcast_group_intf: "
                    "cannot get interface index for '%s'\n",
                   ifname);
        }
#else /* !HAVE_IF_NAMETOINDEX */
        result = ioctl(ctx->endpoint->sock.fd, SIOCGIFINDEX, &ifr);
        if (result != 0) {
          coap_log(LOG_WARNING, "coap_join_mcast_group_intf: "
                    "cannot get interface index for '%s': %s\n",
                   ifname, coap_socket_strerror());
        }
        else {
          /* Capture the IPv6 if_index for later */
          mreq6.ipv6mr_interface = ifr.ifr_ifindex;
        }
#endif /* !HAVE_IF_NAMETOINDEX */
#endif /* !ESPIDF_VERSION */
        break;
      case AF_INET:
        if (done_ip4)
          break;
        done_ip4 = 1;
#if defined(ESPIDF_VERSION)
        netif = netif_find(ifname);
        if (netif)
          mreq4.imr_interface.s_addr = netif_ip4_addr(netif)->addr;
        else
          coap_log(LOG_ERR,
                   "coap_join_mcast_group_intf: %s: "
                    "Cannot get IPv4 address: %s\n",
                    ifname, coap_socket_strerror());
#else /* !ESPIDF_VERSION */
        /*
         * Need an AF_INET socket to do this unfortunately to stop
         * "Invalid argument" error if AF_INET6 socket is used for SIOCGIFADDR
         */
        ip4fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (ip4fd == -1) {
          coap_log(LOG_ERR,
                   "coap_join_mcast_group_intf: %s: socket: %s\n",
                   ifname, coap_socket_strerror());
          continue;
        }
        memset (&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
        ifr.ifr_name[IFNAMSIZ - 1] = '\000';
        result = ioctl(ip4fd, SIOCGIFADDR, &ifr);
        if (result != 0) {
          coap_log(LOG_ERR,
                   "coap_join_mcast_group_intf: %s: "
                    "Cannot get IPv4 address: %s\n",
                    ifname, coap_socket_strerror());
        }
        else {
          /* Capture the IPv4 address for later */
          mreq4.imr_interface = ((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr;
        }
        close(ip4fd);
#endif /* !ESPIDF_VERSION */
        break;
      default:
        break;
      }
    }
  }
#endif /* ! _WIN32 */

  /* Add in mcast address(es) to appropriate interface */
  for (ainfo = resmulti; ainfo != NULL; ainfo = ainfo->ai_next) {
    LL_FOREACH(ctx->endpoint, endpoint) {
      /* Only UDP currently supported */
      if (endpoint->proto == COAP_PROTO_UDP) {
        coap_address_t gaddr;

        coap_address_init(&gaddr);
        if (ainfo->ai_family == AF_INET6) {
          if (!ifname) {
            if(endpoint->bind_addr.addr.sa.sa_family == AF_INET6) {
              /*
               * Do it on the ifindex that the server is listening on
               * (sin6_scope_id could still be 0)
               */
              mreq6.ipv6mr_interface =
                                 endpoint->bind_addr.addr.sin6.sin6_scope_id;
            }
            else {
              mreq6.ipv6mr_interface = 0;
            }
          }
          gaddr.addr.sin6.sin6_family = AF_INET6;
          gaddr.addr.sin6.sin6_port = endpoint->bind_addr.addr.sin6.sin6_port;
          gaddr.addr.sin6.sin6_addr = mreq6.ipv6mr_multiaddr =
                    ((struct sockaddr_in6 *)ainfo->ai_addr)->sin6_addr;
          result = setsockopt(endpoint->sock.fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                              (char *)&mreq6, sizeof(mreq6));
        }
        else if (ainfo->ai_family == AF_INET) {
          if (!ifname) {
            if(endpoint->bind_addr.addr.sa.sa_family == AF_INET) {
              /*
               * Do it on the interface that the server is listening on
               * (sin_addr could still be INADDR_ANY)
               */
              mreq4.imr_interface = endpoint->bind_addr.addr.sin.sin_addr;
            }
            else {
              mreq4.imr_interface.s_addr = INADDR_ANY;
            }
          }
          gaddr.addr.sin.sin_family = AF_INET;
          gaddr.addr.sin.sin_port = endpoint->bind_addr.addr.sin.sin_port;
          gaddr.addr.sin.sin_addr.s_addr = mreq4.imr_multiaddr.s_addr =
              ((struct sockaddr_in *)ainfo->ai_addr)->sin_addr.s_addr;
          result = setsockopt(endpoint->sock.fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                              (char *)&mreq4, sizeof(mreq4));
        }
        else {
          continue;
        }

        if (result == COAP_SOCKET_ERROR) {
          coap_log(LOG_ERR,
                   "coap_join_mcast_group_intf: %s: setsockopt: %s\n",
                   group_name, coap_socket_strerror());
        }
        else {
          char addr_str[INET6_ADDRSTRLEN + 8 + 1];

          addr_str[sizeof(addr_str)-1] = '\000';
          if (coap_print_addr(&gaddr, (uint8_t*)addr_str,
                            sizeof(addr_str) - 1)) {
            if (ifname)
              coap_log(LOG_DEBUG, "added mcast group %s i/f %s\n", addr_str,
                       ifname);
            else
              coap_log(LOG_DEBUG, "added mcast group %s\n", addr_str);
          }
          mgroup_setup = 1;
        }
      }
    }
  }
  if (!mgroup_setup) {
    result = -1;
  }

 finish:
  freeaddrinfo(resmulti);

  return result;
}

void
coap_mcast_per_resource(coap_context_t *context) {
  context->mcast_per_resource = 1;
}

#endif /* ! COAP_SERVER_SUPPORT */

#if COAP_CLIENT_SUPPORT
int
coap_mcast_set_hops(coap_session_t *session, size_t hops) {
  if (session && coap_is_mcast(&session->addr_info.remote)) {
    switch (session->addr_info.remote.addr.sa.sa_family) {
    case AF_INET:
      if (setsockopt(session->sock.fd, IPPROTO_IP, IP_MULTICAST_TTL,
          (const char *)&hops, sizeof(hops)) < 0) {
        coap_log(LOG_INFO, "coap_mcast_set_hops: %zu: setsockopt: %s\n",
                 hops, coap_socket_strerror());
        return 0;
      }
      return 1;
    case AF_INET6:
      if (setsockopt(session->sock.fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
          (const char *)&hops, sizeof(hops)) < 0) {
        coap_log(LOG_INFO, "coap_mcast_set_hops: %zu: setsockopt: %s\n",
                 hops, coap_socket_strerror());
        return 0;
      }
      return 1;
    default:
      break;
    }
  }
  return 0;
}
#endif /* COAP_CLIENT_SUPPORT */

#else /* defined WITH_CONTIKI || defined WITH_LWIP */
int
coap_join_mcast_group_intf(coap_context_t *ctx COAP_UNUSED,
                           const char *group_name COAP_UNUSED,
                           const char *ifname COAP_UNUSED) {
  return -1;
}

int
coap_mcast_set_hops(coap_session_t *session COAP_UNUSED,
                    size_t hops COAP_UNUSED) {
  return 0;
}

void
coap_mcast_per_resource(coap_context_t *context COAP_UNUSED) {
}
#endif /* defined WITH_CONTIKI || defined WITH_LWIP */

#ifdef WITH_CONTIKI

/*---------------------------------------------------------------------------*/
/* CoAP message retransmission */
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(coap_retransmit_process, ev, data) {
  coap_tick_t now;
  coap_queue_t *nextpdu;

  PROCESS_BEGIN();

  coap_log(LOG_DEBUG, "Started retransmit process\n");

  while (1) {
    PROCESS_YIELD();
    if (ev == PROCESS_EVENT_TIMER) {
      if (etimer_expired(&the_coap_context.retransmit_timer)) {

        nextpdu = coap_peek_next(&the_coap_context);

        coap_ticks(&now);
        while (nextpdu && nextpdu->t <= now) {
          coap_retransmit(&the_coap_context, coap_pop_next(&the_coap_context));
          nextpdu = coap_peek_next(&the_coap_context);
        }

        /* need to set timer to some value even if no nextpdu is available */
        etimer_set(&the_coap_context.retransmit_timer,
          nextpdu ? nextpdu->t - now : 0xFFFF);
      }
      if (etimer_expired(&the_coap_context.notify_timer)) {
        coap_check_notify(&the_coap_context);
        etimer_reset(&the_coap_context.notify_timer);
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

#endif /* WITH_CONTIKI */

#ifdef WITH_LWIP
/* FIXME: retransmits that are not required any more due to incoming packages
 * do *not* get cleared at the moment, the wakeup when the transmission is due
 * is silently accepted. this is mainly due to the fact that the required
 * checks are similar in two places in the code (when receiving ACK and RST)
 * and that they cause more than one patch chunk, as it must be first checked
 * whether the sendqueue item to be dropped is the next one pending, and later
 * the restart function has to be called. nothing insurmountable, but it can
 * also be implemented when things have stabilized, and the performance
 * penality is minimal
 *
 * also, this completely ignores COAP_RESOURCE_CHECK_TIME.
 * */

static void coap_retransmittimer_execute(void *arg) {
  coap_context_t *ctx = (coap_context_t*)arg;
  coap_tick_t now;
  coap_tick_t elapsed;
  coap_queue_t *nextinqueue;

  ctx->timer_configured = 0;

  coap_ticks(&now);

  elapsed = now - ctx->sendqueue_basetime; /* that's positive for sure, and unless we haven't been called for a complete wrapping cycle, did not wrap */

  nextinqueue = coap_peek_next(ctx);
  while (nextinqueue != NULL) {
    if (nextinqueue->t > elapsed) {
      nextinqueue->t -= elapsed;
      break;
    } else {
      elapsed -= nextinqueue->t;
      coap_retransmit(ctx, coap_pop_next(ctx));
      nextinqueue = coap_peek_next(ctx);
    }
  }

  ctx->sendqueue_basetime = now;

  coap_retransmittimer_restart(ctx);
}

static void coap_retransmittimer_restart(coap_context_t *ctx) {
  coap_tick_t now, elapsed, delay;

  if (ctx->timer_configured) {
    printf("clearing\n");
    sys_untimeout(coap_retransmittimer_execute, (void*)ctx);
    ctx->timer_configured = 0;
  }
  if (ctx->sendqueue != NULL) {
    coap_ticks(&now);
    elapsed = now - ctx->sendqueue_basetime;
    if (ctx->sendqueue->t >= elapsed) {
      delay = ctx->sendqueue->t - elapsed;
    } else {
      /* a strange situation, but not completely impossible.
       *
       * this happens, for example, right after
       * coap_retransmittimer_execute, when a retransmission
       * was *just not yet* due, and the clock ticked before
       * our coap_ticks was called.
       *
       * not trying to retransmit anything now, as it might
       * cause uncontrollable recursion; let's just try again
       * with the next main loop run.
       * */
      delay = 0;
    }

    printf("scheduling for %d ticks\n", delay);
    sys_timeout(delay, coap_retransmittimer_execute, (void*)ctx);
    ctx->timer_configured = 1;
  }
}
#endif
