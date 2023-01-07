/*******************************************************************************
 *
 *  Copyright (c) 2022 Contributors to the Eclipse Foundation
 *
 *  All rights reserved. This program and the accompanying materials
 *  are made available under the terms of the Eclipse Public License v1.0
 *  and Eclipse Distribution License v. 1.0 which accompanies this distribution.
 *
 *  The Eclipse Public License is available at
 *     http://www.eclipse.org/legal/epl-v10.html
 *  and the Eclipse Distribution License is available at
 *     http://www.eclipse.org/org/documents/edl-v10.html.
 *
 *  SPDX-License-Identifier: EPL-1.0
 *
 *  Contributors:
 *     Achim Kraus    - initial port for zephyr
 *
 *******************************************************************************/

#include "tinydtls.h"
#include "dtls_prng.h"
#include "random/rand32.h"

int
dtls_prng(unsigned char *buf, size_t len) {
  sys_csrand_get(buf, len);
  return len;
}

void
dtls_prng_init(unsigned seed) {
  (void) seed;
}

