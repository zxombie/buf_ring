/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Arm Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/errno.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* #define DEBUG_BUFRING */

#ifdef DEBUG_BUFRING
static void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}
#endif

static void
critical_enter(void)
{
}

static void
critical_exit(void)
{
}

#include "buf_ring.h"

#define	PROD_COUNT	2
#define	PROD_ITERATIONS	100000000

#define	CONSUMER_MC

static struct buf_ring *br;
static _Atomic bool prod_done = false;
static _Atomic int prod_done_count = 0;

static void *
producer(void *arg)
{
	int id;

	id = (int)(uintptr_t)arg;

	for (size_t i = 0; i < PROD_ITERATIONS;) {
		if (buf_ring_enqueue(br, (void *)(i * PROD_COUNT + 1 + id)) == 0) {
			i++;
		}
	}
	if (atomic_fetch_add(&prod_done_count, 1) == (PROD_COUNT - 1))
		atomic_store(&prod_done, true);

	return (NULL);
}

static void *
consumer(void *arg)
{
	void *val;
	size_t max_vals[PROD_COUNT] = {};
	size_t consume_count, curr;
	int id;

	(void)arg;

	/* Set the initial value to be the expected value */
	for (int i = 1; i < PROD_COUNT; i++) {
		max_vals[i] = i - PROD_COUNT;
	}

	consume_count = 0;
	while (!atomic_load(&prod_done) || !buf_ring_empty(br)) {
#ifdef CONSUMER_MC
		val = buf_ring_dequeue_mc(br);
#else
		val = buf_ring_dequeue_sc(br);
#endif
		if (val != NULL) {
			consume_count++;
			curr = (size_t)(uintptr_t)val;
			id = curr % PROD_COUNT;
			if (curr != max_vals[id] + PROD_COUNT)
				printf("Incorrect val: %zu Expect: %zu "
				    "Difference: %zd\n", curr,
				    max_vals[id] + PROD_COUNT,
				    curr - max_vals[id] - PROD_COUNT);
			max_vals[id] = (uintptr_t)val;
		}
	}

	printf("Expected: %zu\n", (size_t)PROD_ITERATIONS * PROD_COUNT);
	printf("Received: %zu\n", consume_count);
	for (int i = 0; i < PROD_COUNT; i++)
		printf("max[%d] = %zu\n", i, max_vals[i]);

	return (NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t prod[PROD_COUNT];
	pthread_t cons;
	int ret;

	(void)argc;
	(void)argv;

	br = buf_ring_alloc(4);

	ret = pthread_create(&cons, NULL, consumer, NULL);
	assert(ret == 0);
	for (int i = 0; i < PROD_COUNT; i++) {
		ret = pthread_create(&prod[i], NULL, producer,
		    (void *)(uintptr_t)i);
		assert(ret == 0);
	}

	for (int i = 0; i < PROD_COUNT; i++) {
		ret = pthread_join(prod[i], NULL);
		assert(ret == 0);
	}
	ret = pthread_join(cons, NULL);
	assert(ret == 0);

	buf_ring_free(br);

	return (0);
}
