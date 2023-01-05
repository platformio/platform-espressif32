/* coap_subscribe.c -- subscription handling for CoAP
 *                see RFC7641
 *
 * Copyright (C) 2010-2019,2022 Olaf Bergmann <bergmann@tzi.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * This file is part of the CoAP library libcoap. Please see
 * README for terms of use.
 */

/**
 * @file coap_subscribe.c
 * @brief Subscription handling functions
 */

#include "coap3/coap_internal.h"

#if COAP_SERVER_SUPPORT
void
coap_subscription_init(coap_subscription_t *s) {
  assert(s);
  memset(s, 0, sizeof(coap_subscription_t));
}
#endif /* COAP_SERVER_SUPPORT */
