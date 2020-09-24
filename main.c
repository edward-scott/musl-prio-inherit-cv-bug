#include "thread.h"
#include "threadpool.h"

static uint32_t counter = 0;

static void * pool_counter (void * arg)
{
  (void) arg;
  counter++;
  return NULL;
}

int main (int argc, char **argv)
{
  iot_threadpool_t * pool = iot_threadpool_alloc (2u, 0u, IOT_THREAD_NO_PRIORITY, IOT_THREAD_NO_AFFINITY);
  counter = 0;
  iot_threadpool_add_work (pool, pool_counter, NULL, IOT_THREAD_NO_PRIORITY);
  iot_threadpool_start (pool);
  iot_threadpool_wait (pool);
  assert (counter == 1);
  iot_threadpool_stop (pool);
  iot_threadpool_free (pool);
}
