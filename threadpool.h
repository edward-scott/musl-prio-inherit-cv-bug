//
// Copyright (c) 2019-2020 IOTech
//
// SPDX-License-Identifier: Apache-2.0
//
#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <sched.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Alias for threadpool structure */
typedef struct iot_threadpool_t iot_threadpool_t;

/**
 * @brief Allocate memory and initialise thread pool
 *
 * The function to allocate memory and initialise a thread pool. This function does not return until all
 * threads have initialised successfully.
 *
 * @param num_threads        Number of threads to be created in the threadpool
 * @param max_jobs           Maximum number of jobs to queue (before blocking)
 * @param default_prio       Default priority for created threads (not set if -1)
 * @param affinity           Processor affinity for pool threads (not set if less than zero)
 * @param logger             Logger, can be NULL
 * @returns iot_threadpool_t Created thread pool on success, NULL on error
 */
extern iot_threadpool_t * iot_threadpool_alloc (uint16_t num_threads, uint32_t max_jobs, int default_prio);

/**
 * @brief Add work to the thread pool
 *
 * The function to add a function to the thread pool's job queue. This function will wait until a space is available in the job queue to add new work.
 *
 * @param  pool          Pool to which the work will be added
 * @param  function      Function to add as work
 * @param  arg           Function argument
 * @param  priority      Priority to run thread at (not set if -1)
 */
extern void iot_threadpool_add_work (iot_threadpool_t * pool, void * (*function) (void*), void * arg, int priority);

/**
 * @brief Try to add work to the thread pool
 *
 * The function to add a function to the thread pool's job queue. This function will return the status immediately and doesn't block.
 * Work is not added to the thread pool if the maximum number of queued jobs is exceeded the maximum limit.
 *
 * @param  pool          Pool to which the work will be added
 * @param  function      Function to add as work
 * @param  arg           Function argument
 * @param  priority      Priority to run thread at (not set if -1)
 * @return               whether the work was added. 'true' if the work is successfully added to the pool, 'false' otherwise
 */
extern bool iot_threadpool_try_work (iot_threadpool_t * pool, void * (*function) (void*), void * arg, int priority);

/**
 * @brief Wait for all queued jobs to finish
 *
 * The function that will wait for all jobs - both queued and currently running to finish.
 *
 * @param pool the thread pool to wait for
 */
extern void iot_threadpool_wait (iot_threadpool_t * pool);

/**
 * @brief Start the thread pool
 *
 * The function to start the thread pool for handling jobs.
 *
 * @param pool Pool to start
 */
extern void iot_threadpool_start (iot_threadpool_t * pool);

/**
 * @brief Stop the thread pool
 *
 * The function to stop the thread pool from handling jobs.
 *
 * @param pool  Pool to stop
 */
extern void iot_threadpool_stop (iot_threadpool_t * pool);

/**
 * @brief Destroy the thread pool
 *
 * The function that will wait for the currently active threads to finish and then frees the thread pool.
 *
 * @param pool  Pool to free
 */
extern void iot_threadpool_free (iot_threadpool_t * pool);

/**
 * @brief Increment the thread pool reference count
 *
 * The function to increment the thread pool reference count
 *
 * @param pool  Pointer to the thread pool to increment the reference count
 */
extern void iot_threadpool_add_ref (iot_threadpool_t * pool);

#ifdef __cplusplus
}
#endif
#endif
