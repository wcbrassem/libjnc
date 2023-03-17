/**
 * @file macosx_pthread.h
 * @author Wayne Brassem <wbrassem@juniper.net>
 * @brief libnetconf2 MAC OSX pthread implementations
 *
 * @copyright
 * Copyright (c) 2017 - 2021 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#ifndef MACOSX_PTHREAD_H_
#define MACOSX_PTHREAD_H_

#include <pthread.h>

/**
 * @brief pthread barrier structure.
 */
typedef int pthread_barrierattr_t;
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;              /**< Size of the pthread barrier. */
    int tripCount;
} pthread_barrier_t;

/**
 * @brief Initialize a pthread barrier
 * *
 * @param[in] barrier Pointer to pthread structure.
 * @param[in] attr Pointer to barrier attribute (not used), outputd a warning if not NULL.
 * @param[in] count Set the number of threads required to pass the barrier.
 * @return -1 on error.
 * @return 0 otherwise.
 */
int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count);
// {
//     if(count == 0)
//     {
//         errno = EINVAL;
//         return -1;
//     }
//     if(pthread_mutex_init(&barrier->mutex, 0) < 0)
//     {
//         return -1;
//     }
//     if(pthread_cond_init(&barrier->cond, 0) < 0)
//     {
//         pthread_mutex_destroy(&barrier->mutex);
//         return -1;
//     }
//     barrier->tripCount = count;
//     barrier->count = 0;

//     if (attr)
//         WRN(NULL, "pthread init included attributes not used.");

//     return 0;
// }

/**
 * @brief Destroy a pthread barrier
 * *
 * @param[in] barrier Pointer to pthread structure.
 * @return returns 0.
 */
int pthread_barrier_destroy(pthread_barrier_t *barrier);
// {
//     pthread_cond_destroy(&barrier->cond);
//     pthread_mutex_destroy(&barrier->mutex);
//     return 0;
// }

/**
 * @brief Wait for a pthread barrier
 * *
 * @param[in] barrier Pointer to pthread structure.
 * @return returns 1 if exceeding trip count, 0 otherwise.
 */
int pthread_barrier_wait(pthread_barrier_t *barrier);
// {
//     pthread_mutex_lock(&barrier->mutex);
//     ++(barrier->count);
//     if(barrier->count >= barrier->tripCount)
//     {
//         barrier->count = 0;
//         pthread_cond_broadcast(&barrier->cond);
//         pthread_mutex_unlock(&barrier->mutex);
//         return 1;
//     }
//     else
//     {
//         pthread_cond_wait(&barrier->cond, &(barrier->mutex));
//         pthread_mutex_unlock(&barrier->mutex);
//         return 0;
//     }
// }

/**
 * @brief Timed mutex pthread replacement for OSX
 *
 * This function Will not return EOWNERDEAD, EDEADLK or EOWNERDEAD as does the true pthread function
 *
 * @param[in] mutex Pointer to mutex structure.
 * @param[in] abs_timeout Pointer to timeout timespec structure.
 * @return returns pthread_mutex_trylock() != EBUSY or ETIMEDOUT.
 */
int macos_pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abs_timeout);
// {
//     int rv;
//     struct timespec remaining, slept, ts;

//     remaining = *abs_timeout;
//     while ((rv = pthread_mutex_trylock(mutex)) == EBUSY) {
//         ts.tv_sec = 0;
//         ts.tv_nsec = (remaining.tv_sec > 0 ? 10000000 : 
//                      (remaining.tv_nsec < 10000000 ? remaining.tv_nsec : 10000000));
//         nanosleep(&ts, &slept);
//         ts.tv_nsec -= slept.tv_nsec;
//         if (ts.tv_nsec <= remaining.tv_nsec) {
//             remaining.tv_nsec -= ts.tv_nsec;
//         }
//         else {
//             remaining.tv_sec--;
//             remaining.tv_nsec = (1000000 - (ts.tv_nsec - remaining.tv_nsec));
//         }
//         if (remaining.tv_sec < 0 || (!remaining.tv_sec && remaining.tv_nsec <= 0)) {
//             return ETIMEDOUT;
//         }
//     }

//     return rv;
// }

#endif /* MACOSX_PTHREAD_H_ */
