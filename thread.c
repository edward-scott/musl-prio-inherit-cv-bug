//
// Copyright (c) 2019-2020 IOTech
//
// SPDX-License-Identifier: Apache-2.0
//
#include "thread.h"

bool iot_thread_priority_valid (int priority)
{
  static int max_priority = -2;
  static int min_priority = -2;
  if (max_priority == -2)
  {
    max_priority = sched_get_priority_max (SCHED_FIFO);
    min_priority = sched_get_priority_min (SCHED_FIFO);
  }
  return (priority >= min_priority && priority <= max_priority);
}

bool iot_thread_create (pthread_t * tid, iot_thread_fn_t func, void * arg, int priority)
{
  int ret;
  pthread_attr_t attr;
  pthread_t id;

  if (tid == NULL) tid = &id;

  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
  if (iot_thread_priority_valid (priority) && (geteuid () == 0)) // No guarantee that can set RT policies in container
  {
    struct sched_param param;
    param.sched_priority = priority;

    /* If priority set, also set FIFO scheduling */

    ret = pthread_attr_setschedpolicy (&attr, SCHED_FIFO);
    if (ret != 0) printf ("pthread_attr_setschedpolicy failed ret: %d\n", ret);
    ret = pthread_attr_setschedparam (&attr, &param);
    if (ret != 0) printf ("pthread_attr_setschedparam failed ret: %d\n", ret);
  }
  ret = pthread_create (tid, &attr, func, arg);
  if (ret != 0)
  {
    ret = pthread_create (tid, NULL, func, arg);
    if (ret != 0) printf ("pthread_create failed ret: %d\n", ret);
  }
  pthread_attr_destroy (&attr);
  return ret == 0;
}

int iot_thread_get_priority (pthread_t thread)
{
  struct sched_param param;
  int policy;
  pthread_getschedparam (thread, &policy, &param);
  return param.sched_priority;
}

int iot_thread_current_get_priority (void)
{
  return iot_thread_get_priority (pthread_self ());
}

bool iot_thread_set_priority (pthread_t thread, int priority)
{
  struct sched_param param = { .sched_priority = priority };
  return (pthread_setschedparam (thread, SCHED_FIFO, &param) == 0);
}

bool iot_thread_current_set_priority (int priority)
{
  return iot_thread_set_priority (pthread_self (), priority);
}

void iot_mutex_init (pthread_mutex_t * mutex)
{
  assert (mutex);
  pthread_mutexattr_t attr;
  pthread_mutexattr_init (&attr);
  // Comment out next line to suppress bug
  pthread_mutexattr_setprotocol (&attr, PTHREAD_PRIO_INHERIT);
  pthread_mutex_init (mutex, &attr);
  pthread_mutexattr_destroy (&attr);
}
