//
// Copyright (c) 2019 IOTech
//
// SPDX-License-Identifier: Apache-2.0
//
#include "threadpool.h"
#include "thread.h"

#ifdef IOT_HAS_PRCTL
#include <sys/prctl.h>
#endif

#define IOT_PRCTL_NAME_MAX 16
#define IOT_TP_THREADS_DEFAULT 2
#define IOT_TP_JOBS_DEFAULT 0
#define IOT_TP_SHUTDOWN_MIN 200

typedef struct iot_job_t
{
  struct iot_job_t * prev;           // Pointer to previous job
  void * (*function) (void * arg);   // Function to run
  void * arg;                        // Function's argument
  int priority;                      // Job priority
  uint32_t id;                       // Job id
} iot_job_t;

typedef struct iot_thread_t
{
  uint16_t id;                       // Thread number
  pthread_t tid;                     // Thread id
  struct iot_threadpool_t * pool;    // Thread pool
} iot_thread_t;

typedef enum
{
  IOT_COMPONENT_INITIAL = 0U,  /**< Initial component state */
  IOT_COMPONENT_STOPPED = 1U,  /**< Stopped component state */
  IOT_COMPONENT_RUNNING = 2U,  /**< Running component state */
  IOT_COMPONENT_DELETED = 4U,  /**< Deleted component state */
  IOT_COMPONENT_STARTING = 8U  /**< Starting transient component state */
} iot_component_state_t;

typedef struct iot_threadpool_t
{
  volatile iot_component_state_t state;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  iot_thread_t * thread_array;       // Array of threads
  const uint32_t max_jobs;           // Maximum number of queued jobs
  const uint16_t id;                 // Thread pool id
  uint16_t working;                  // Number of threads currently working
  uint32_t jobs;                     // Number of jobs in queue
  uint32_t delay;                    // Shutdown delay in milli seconds
  uint32_t next_id;                  // Job id counter
  iot_job_t * front;                 // Front of job queue
  iot_job_t * rear;                  // Rear of job queue
  iot_job_t * cache;                 // Free job cache
  pthread_cond_t work_cond;          // Work control condition
  pthread_cond_t job_cond;           // Job control condition
  pthread_cond_t queue_cond;         // Job queue control condition
} iot_threadpool_t;

iot_component_state_t wait_and_lock (iot_threadpool_t * component, uint32_t states)
{
  assert (component);
  assert (pthread_mutex_lock (&component->mutex) == 0);
  while ((component->state & states) == 0)
  {
    pthread_cond_wait (&component->cond, &component->mutex);
  }
  return component->state;
}

static void * iot_threadpool_thread (void * arg)
{
  iot_thread_t * th = (iot_thread_t*) arg;
  iot_threadpool_t * pool = th->pool;
  pthread_t tid = pthread_self ();
  int priority = iot_thread_get_priority (tid);
  char name[IOT_PRCTL_NAME_MAX];
  iot_component_state_t state;

  snprintf (name, IOT_PRCTL_NAME_MAX, "iot-%" PRIu16 "-%" PRIu16 "\n", th->pool->id, th->id);
#ifdef IOT_HAS_PRCTL
  prctl (PR_SET_NAME, name);
#endif
  printf ("Thread %s starting\n", name);

  while (true)
  {
    state = wait_and_lock (pool, (uint32_t) IOT_COMPONENT_DELETED | (uint32_t) IOT_COMPONENT_RUNNING);

    if (state == IOT_COMPONENT_DELETED) // Exit thread on deletion
    {
      assert (pthread_mutex_unlock (&pool->mutex) == 0);
      break;
    }
    iot_job_t * first = pool->front;
    if (first) // Pull job from queue
    {
      iot_job_t job = *first;
      printf ("Thread processing job #%u\n", job.id);
      pool->front = first->prev;
      first->prev = pool->cache;
      pool->cache = first;
#if 0
      if (--pool->jobs == 0)
      {
        pool->front = NULL;
        pool->rear = NULL;
        pthread_cond_signal (&pool->work_cond); // Signal no jobs in queue
      }
      if ((pool->jobs + 1) == pool->max_jobs)
      {
        pthread_cond_broadcast (&pool->queue_cond); // Signal now space in job queue
      }
#endif
      pool->working++;
      assert (pthread_mutex_unlock (&pool->mutex) == 0);
      if ((job.priority != IOT_THREAD_NO_PRIORITY) && (job.priority != priority)) // If required, set thread priority
      {
        if (iot_thread_set_priority (tid, job.priority))
        {
          priority = job.priority;
        }
      }
      (job.function) (job.arg); // Run job
      printf ("Thread completed job #%u\n", job.id);
      assert (pthread_mutex_lock (&pool->mutex) == 0);
      if (--pool->working == 0)
      {
        pthread_cond_signal (&pool->work_cond); // Signal when no threads working
      }
    }
    else
    {
      printf ("Thread waiting for new job\n");
      pthread_cond_wait (&pool->job_cond, &pool->mutex); // Wait for new job
    }
    assert (pthread_mutex_unlock (&pool->mutex) == 0);
  }
  printf ("Thread exiting\n");
  return NULL;
}

static void mutex_init (pthread_mutex_t * mutex)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init (&attr);
#ifndef NDEBUG
  pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_ERRORCHECK);
#endif
  pthread_mutexattr_setprotocol (&attr, PTHREAD_PRIO_INHERIT);
  pthread_mutex_init (mutex, &attr);
  pthread_mutexattr_destroy (&attr);
}


iot_threadpool_t * iot_threadpool_alloc (uint16_t threads, uint32_t max_jobs, int default_prio)
{
  static int pool_id = 0;

  uint16_t created;
  iot_threadpool_t * pool = (iot_threadpool_t*) calloc (1, sizeof (*pool));
  *(uint16_t*) &pool->id = pool_id++;
  printf ("iot_threadpool_alloc (threads: %" PRIu16 " max_jobs: %u default_priority: %d)\n", threads, max_jobs, default_prio);
  pool->thread_array = (iot_thread_t*) calloc (threads, sizeof (iot_thread_t));
  *(uint32_t*) &pool->max_jobs = max_jobs ? max_jobs : UINT32_MAX;
  pool->delay = IOT_TP_SHUTDOWN_MIN;
  pthread_cond_init (&pool->work_cond, NULL);
  pthread_cond_init (&pool->queue_cond, NULL);
  pthread_cond_init (&pool->job_cond, NULL);
  mutex_init (&pool->mutex);
  pthread_cond_init (&pool->cond, NULL);
  for (created = 0; created < threads; created++)
  {
    iot_thread_t * th = &pool->thread_array[created];
    th->pool = pool;
    th->id = created;
    if (! iot_thread_create (&th->tid, iot_threadpool_thread, th, default_prio))
    {
      break;
    }
  }
  usleep (100); /* Wait until all threads running */
  return pool;
}

static void iot_threadpool_add_work_locked (iot_threadpool_t * pool, void * (*func) (void*), void * arg, int prio)
{
  iot_job_t * job = pool->cache;
  if (job)
  {
    pool->cache = job->prev;
  }
  else
  {
    job = malloc (sizeof (*job));
  }
  job->function = func;
  job->arg = arg;
  job->priority = prio;
  job->prev = NULL;
  job->id = pool->next_id++;
  printf ("Added new job #%u\n", job->id);

  if (job->priority != IOT_THREAD_NO_PRIORITY) // Order job by priority
  {
    iot_job_t * iter = pool->front;
    iot_job_t * prev = NULL;
    while (iter)
    {
      if (iter->priority == IOT_THREAD_NO_PRIORITY || iter->priority < job->priority)
      {
        job->prev = iter;
        if (prev)
        {
          prev->prev = job;
        }
        else
        {
          pool->front = job;
        }
        goto added;
      }
      prev = iter;
      iter = iter->prev;
    }
  }
  job->prev = NULL; // Add job to back of queue
  if (pool->rear)
  {
    pool->rear->prev = job;
  }
  pool->rear = job;
  if (pool->front == NULL)
  {
    pool->front = job;
  }

added:

  pool->jobs++;
  pthread_cond_signal (&pool->job_cond); // Signal new job added
}

bool iot_threadpool_try_work (iot_threadpool_t * pool, void * (*func) (void*), void * arg, int prio)
{
  assert (pool && func);
  printf ("iot_threadpool_try_work()\n");
  bool ret = false;
  assert (pthread_mutex_lock (&pool->mutex) == 0);
  if (pool->jobs < pool->max_jobs)
  {
    iot_threadpool_add_work_locked (pool, func, arg, prio);
    ret = true;
  }
  assert (pthread_mutex_unlock (&pool->mutex) == 0);
  return ret;
}

void iot_threadpool_add_work (iot_threadpool_t * pool, void * (*func) (void*), void * arg, int prio)
{
  assert (pool && func);
  printf ("iot_threadpool_add_work()\n");
  assert (pthread_mutex_lock (&pool->mutex) == 0);
  while (pool->jobs == pool->max_jobs)
  {
    printf ("iot_threadpool_add_work jobs at max (%u), waiting for job completion\n", pool->max_jobs);
    pthread_cond_wait (&pool->queue_cond, &pool->mutex); // Wait until space in job queue
  }
  iot_threadpool_add_work_locked (pool, func, arg, prio);
  printf ("iot_threadpool_add_work jobs/max: %u/%u\n", pool->jobs, pool->max_jobs);
  assert (pthread_mutex_unlock (&pool->mutex) == 0);
}

void iot_threadpool_wait (iot_threadpool_t * pool)
{
  assert (pool);
  printf ("iot_threadpool_wait()\n");
  assert (pthread_mutex_lock (&pool->mutex) == 0);
  while (pool->jobs || pool->working)
  {
    printf ("iot_threadpool_wait (jobs:%u active threads:%u)\n", pool->jobs, pool->working);
    pthread_cond_wait (&pool->work_cond, &pool->mutex); // Wait until all jobs processed
  }
  assert (pthread_mutex_unlock (&pool->mutex) == 0);
}

static bool iot_component_set_state (iot_threadpool_t * component, uint32_t state)
{
  assert (component);
  bool valid = false;
  bool changed = false;
  assert (pthread_mutex_lock (&component->mutex) == 0);
  switch (state)
  {
    case IOT_COMPONENT_STOPPED:
    case IOT_COMPONENT_RUNNING:
    case IOT_COMPONENT_STARTING: valid = (component->state != IOT_COMPONENT_DELETED); break;
    case IOT_COMPONENT_DELETED: valid = (component->state != IOT_COMPONENT_RUNNING); break;
    default: break;
  }
  if (valid)
  {
    changed = component->state != state;
    component->state = state;
    assert (pthread_cond_broadcast (&component->cond) == 0);
  }
  assert (pthread_mutex_unlock (&component->mutex) == 0);
  return changed;
}

static bool iot_component_set_running (iot_threadpool_t * component)
{
  return iot_component_set_state (component, IOT_COMPONENT_RUNNING);
}

static bool iot_component_set_stopped (iot_threadpool_t * component)
{
  return iot_component_set_state (component, IOT_COMPONENT_STOPPED);
}

static bool iot_component_set_deleted (iot_threadpool_t * component)
{
  return iot_component_set_state (component, IOT_COMPONENT_DELETED);
}

void iot_threadpool_stop (iot_threadpool_t * pool)
{
  assert (pool);
  printf ("iot_threadpool_stop()\n");
  iot_component_set_stopped (pool);
  assert (pthread_mutex_lock (&pool->mutex) == 0);
  pthread_cond_broadcast (&pool->job_cond);
  assert (pthread_mutex_unlock (&pool->mutex) == 0);
}

void iot_threadpool_start (iot_threadpool_t * pool)
{
  assert (pool);
  printf ("iot_threadpool_start()\n");
  iot_component_set_running (pool);
  assert (pthread_mutex_lock (&pool->mutex) == 0);
  pthread_cond_broadcast (&pool->job_cond);
  assert (pthread_mutex_unlock (&pool->mutex) == 0);
}

void iot_threadpool_free (iot_threadpool_t * pool)
{
  if (pool)
  {
    iot_job_t * job;
    printf ("iot_threadpool_free()\n");
    iot_threadpool_stop (pool);
    iot_component_set_deleted (pool);
    usleep (pool->delay * 1000);
    while ((job = pool->cache))
    {
      pool->cache = job->prev;
      free (job);
    }
    while ((job = pool->front))
    {
      pool->front = job->prev;
      free (job);
    }
    pthread_cond_destroy (&pool->work_cond);
    pthread_cond_destroy (&pool->queue_cond);
    pthread_cond_destroy (&pool->job_cond);
    free (pool->thread_array);
    pthread_cond_destroy (&pool->cond);
    pthread_mutex_destroy (&pool->mutex);
    free (pool);
  }
}
