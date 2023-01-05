/*
 * coap_prng.c -- random number generation
 *
 * Copyright (C) 2020 Olaf Bergmann <bergmann@tzi.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * This file is part of the CoAP library libcoap. Please see README
 * for terms of use.
 */

/**
 * @file coap_prng.c
 * @brief Pseudo Random Number functions
 */

#include "coap3/coap_internal.h"

#ifdef HAVE_GETRANDOM
#include <sys/random.h>
#else /* !HAVE_GETRANDOM */
#include <stdlib.h>
#endif /* !HAVE_GETRANDOM */

#if defined(MBEDTLS_ENTROPY_HARDWARE_ALT)
#include <entropy_poll.h>
#endif /* MBEDTLS_ENTROPY_HARDWARE_ALT */

#if defined(_WIN32)

errno_t __cdecl rand_s( _Out_ unsigned int* _RandomValue );
/**
 * Fills \p buf with \p len random bytes. This is the default implementation for
 * coap_prng(). You might want to change coap_prng_impl() to use a better
 * PRNG on your specific platform.
 */
COAP_STATIC_INLINE int
coap_prng_impl( unsigned char *buf, size_t len ) {
        while ( len != 0 ) {
                uint32_t r = 0;
                size_t i;
                if ( rand_s( &r ) != 0 )
                        return 0;
                for ( i = 0; i < len && i < 4; i++ ) {
                        *buf++ = (uint8_t)r;
                        r >>= 8;
                }
                len -= i;
        }
        return 1;
}

#endif /* _WIN32 */

/*
 * This, or any user provided alternative, function is expected to
 * return 0 on failure and 1 on success.
 */
static int
coap_prng_default(void *buf, size_t len) {
#if defined(MBEDTLS_ENTROPY_HARDWARE_ALT)
  /* mbedtls_hardware_poll() returns 0 on success */
  return (mbedtls_hardware_poll(NULL, buf, len, NULL) ? 0 : 1);
#else /* !MBEDTLS_ENTROPY_HARDWARE_ALT */
#ifdef HAVE_GETRANDOM
  return (getrandom(buf, len, 0) > 0) ? 1 : 0;
#else /* !HAVE_GETRANDOM */
#if defined(_WIN32)
  return coap_prng_impl(buf,len);
#else /* !_WIN32 */
  unsigned char *dst = (unsigned char *)buf;
  while (len--)
    *dst++ = rand() & 0xFF;
  return 1;
#endif /* !_WIN32 */
#endif /* !HAVE_GETRANDOM */
#endif /* !MBEDTLS_ENTROPY_HARDWARE_ALT */
}

static coap_rand_func_t rand_func = coap_prng_default;

#if defined(WITH_CONTIKI)

#elif defined(WITH_LWIP) && defined(LWIP_RAND)

#else

void
coap_set_prng(coap_rand_func_t rng) {
  rand_func = rng;
}

void
coap_prng_init(unsigned int seed) {
#ifdef HAVE_GETRANDOM
  /* No seed to seed the random source if getrandom() is used,
   * see dtls_prng(). */
  (void)seed;
#else /* !HAVE_GETRANDOM */
  srand(seed);
#endif /* !HAVE_GETRANDOM */
}

int
coap_prng(void *buf, size_t len) {
  if (!rand_func) {
    return 0;
  }

  return rand_func(buf, len);
}

#endif
