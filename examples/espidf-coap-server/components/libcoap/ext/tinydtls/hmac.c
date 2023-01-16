/*******************************************************************************
 *
 * Copyright (c) 2011-2020 Olaf Bergmann (TZI) and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v. 1.0 which accompanies this distribution.
 *
 * The Eclipse Public License is available at http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 * http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Olaf Bergmann  - initial API and implementation
 *    Hauke Mehrtens - memory optimization, ECC integration
 *
 *******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tinydtls.h"
#if defined(HAVE_ASSERT_H) && !defined(assert)
#include <assert.h>
#endif /* HAVE_ASSERT_H && !assert */

#include "dtls_debug.h"
#include "hmac.h"

void
dtls_hmac_update(dtls_hmac_context_t *ctx,
		 const unsigned char *input, size_t ilen) {
  assert(ctx);
  dtls_hash_update(&ctx->data, input, ilen);
}

void
dtls_hmac_init(dtls_hmac_context_t *ctx, const unsigned char *key, size_t klen) {
  int i;

  assert(ctx);

  memset(ctx, 0, sizeof(dtls_hmac_context_t));

  if (klen > DTLS_HMAC_BLOCKSIZE) {
    dtls_hash_init(&ctx->data);
    dtls_hash_update(&ctx->data, key, klen);
    dtls_hash_finalize(ctx->pad, &ctx->data);
  } else
    memcpy(ctx->pad, key, klen);

  /* create ipad: */
  for (i=0; i < DTLS_HMAC_BLOCKSIZE; ++i)
    ctx->pad[i] ^= 0x36;

  dtls_hash_init(&ctx->data);
  dtls_hmac_update(ctx, ctx->pad, DTLS_HMAC_BLOCKSIZE);

  /* create opad by xor-ing pad[i] with 0x36 ^ 0x5C: */
  for (i=0; i < DTLS_HMAC_BLOCKSIZE; ++i)
    ctx->pad[i] ^= 0x6A;
}

int
dtls_hmac_finalize(dtls_hmac_context_t *ctx, unsigned char *result) {
  unsigned char buf[DTLS_HMAC_DIGEST_SIZE];
  size_t len; 

  assert(ctx);
  assert(result);
  
  len = dtls_hash_finalize(buf, &ctx->data);

  dtls_hash_init(&ctx->data);
  dtls_hash_update(&ctx->data, ctx->pad, DTLS_HMAC_BLOCKSIZE);
  dtls_hash_update(&ctx->data, buf, len);

  len = dtls_hash_finalize(result, &ctx->data);

  return len;
}

#ifdef HMAC_TEST
#include <stdio.h>

int main(int argc, char **argv) {
  static unsigned char buf[DTLS_HMAC_DIGEST_SIZE];
  size_t len, i;
  dtls_hmac_context_t ctx;

  if (argc < 3) {
    fprintf(stderr, "usage: %s key text", argv[0]);
    return -1;
  }

  dtls_hmac_init(&ctx, argv[1], strlen(argv[1]));
  dtls_hmac_update(ctx, argv[2], strlen(argv[2]));
  
  len = dtls_hmac_finalize(ctx, buf);

  for(i = 0; i < len; i++) 
    printf("%02x", buf[i]);
  printf("\n");

  return 0;
}
#endif
