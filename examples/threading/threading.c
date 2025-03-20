#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // (DONE): wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    int ret;
    struct thread_data* thread_func_args = (struct thread_data*) thread_param;
    
    usleep(1000 * thread_func_args->time_before_mutex);
    pthread_mutex_lock(thread_func_args->mutex_to_acquire);
    usleep(1000 * thread_func_args->time_holding_mutex);
    ret = pthread_mutex_unlock(thread_func_args->mutex_to_acquire);

    if (ret != 0)
    {
        thread_func_args->thread_complete_success = false;
    }

    thread_func_args->thread_complete_success = true;

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * (DONE): allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    int ret;
    struct thread_data * thread_args;

    thread_args = malloc(sizeof(struct thread_data));
    thread_args->mutex_to_acquire = mutex;
    thread_args->time_before_mutex = wait_to_obtain_ms;
    thread_args->time_holding_mutex = wait_to_release_ms;

    ret = pthread_create(thread, NULL, threadfunc, (void*) thread_args);

    if (ret == 0)
    {
        return true;
    }
    
    return false;
}

