/*--------------------------------------------------------------------*/
/* rwlock.h                                                           */
/* Author: Junghan Yoon, KyoungSoo Park                               */
/*--------------------------------------------------------------------*/
#ifndef _RWLOCK_H
#define _RWLOCK_H
/*--------------------------------------------------------------------*/
#include "common.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#define RING_SIZE NUM_THREADS
#define WRITER_RING_SIZE (RING_SIZE)
#define READER_RING_SIZE (RING_SIZE)
/*--------------------------------------------------------------------*/
typedef struct
{
    pthread_mutex_t lock;         // mutex lock for protecting others
    unsigned int current_readers; // number of current readers
    unsigned int current_writers; // number of current writers
    unsigned int delay;           // for semantic test
    void *uctx;                   // free to use (user context)
} rwlock_t;
/*--------------------------------------------------------------------*/
/**
 * Initializes rwlock.
 * Returns -1 when any internal errors occur.
 * Returns 0 on success.
 *
 * !caveat!
 * Do not call this twice on the same rwlock.
 */
int rwlock_init(rwlock_t *rw, int delay);
/*--------------------------------------------------------------------*/
/**
 * Acquires read lock.
 * If quick is set, it should acquire the read lock
 * earlier than other pending threads.
 * Returns -1 when any internal errors occur.
 * Returns 0 on success.
 */
int rwlock_read_lock(rwlock_t *rw, int quick);
/*--------------------------------------------------------------------*/
/**
 * Releases read lock.
 * Returns -1 when any internal errors occur.
 * Returns 0 on success.
 */
int rwlock_read_unlock(rwlock_t *rw);
/*--------------------------------------------------------------------*/
/**
 * Acquires write lock.
 * Returns -1 when any internal errors occur.
 * Returns 0 on success.
 */
int rwlock_write_lock(rwlock_t *rw);
/*--------------------------------------------------------------------*/
/**
 * Releases write lock.
 * Returns -1 when any internal errors occur.
 * Returns 0 on success.
 */
int rwlock_write_unlock(rwlock_t *rw);
/*--------------------------------------------------------------------*/
/**
 * Destroys rwlock.
 * Returns -1 when any internal errors occur.
 * Returns 0 on success.
 */
int rwlock_destroy(rwlock_t *rw);
/*--------------------------------------------------------------------*/
static inline int
rwlock_current_readers(rwlock_t *rw)
{
    return rw->current_readers;
}
/*--------------------------------------------------------------------*/
static inline int
rwlock_current_writers(rwlock_t *rw)
{
    return rw->current_writers;
}
/*--------------------------------------------------------------------*/
#endif // _RWLOCK_H