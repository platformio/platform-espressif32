/*******************************************************************************
 *
 * Copyright (c) 2011, 2012, 2013, 2014, 2015 Olaf Bergmann (TZI) and others.
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
 *
 *******************************************************************************/

/**
 * @file dtls_time.h
 * @brief Clock Handling
 */

#ifndef _DTLS_DTLS_TIME_H_
#define _DTLS_DTLS_TIME_H_

#include <stdint.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */

#include "tinydtls.h"

/**
 * @defgroup clock Clock Handling
 * Default implementation of internal clock. You should redefine this if
 * you do not have time() and gettimeofday().
 * @{
 */

#ifdef WITH_CONTIKI
#include "clock.h"

#elif defined(RIOT_VERSION)

#include "ztimer.h"
#include "timex.h"

/* this macro is already present on FreeBSD
   which causes a redefine error otherwise */
#ifndef CLOCK_SECOND
#define CLOCK_SECOND (MS_PER_SEC)
#endif

typedef uint32_t clock_time_t;

#elif defined(WITH_ZEPHYR)

#include <zephyr.h>

#ifndef CLOCK_SECOND
# define CLOCK_SECOND 1000
#endif

typedef int64_t clock_time_t;

#else /* WITH_CONTIKI || RIOT_VERSION */

#ifdef HAVE_TIME_H
#include <time.h>
#endif /* HAVE_TIME_H */

#ifndef CLOCK_SECOND
# define CLOCK_SECOND 1000
#endif

typedef uint32_t clock_time_t;

#endif /* WITH_CONTIKI || RIOT_VERSION */

typedef clock_time_t dtls_tick_t;

#ifndef DTLS_TICKS_PER_SECOND
#define DTLS_TICKS_PER_SECOND CLOCK_SECOND
#endif /* DTLS_TICKS_PER_SECOND */

void dtls_clock_init(void);
void dtls_ticks(dtls_tick_t *t);

/* see https://godbolt.org/z/YchexKaeT */
#define DTLS_OFFSET_TIME (((clock_time_t)~0) >> 1)
/** Checks if A is before (or equal) B. Considers 32 bit time overflow */
#define DTLS_IS_BEFORE_TIME(A, B) ((clock_time_t)(DTLS_OFFSET_TIME + (B)-(A)) >= DTLS_OFFSET_TIME)

/** @} */

#endif /* _DTLS_DTLS_TIME_H_ */
