/*--------------------------------------------------------------------*/
/* hashtable.h                                                        */
/* Author: Junghan Yoon, KyoungSoo Park                               */
/*--------------------------------------------------------------------*/
#ifndef _HASHTABLE_H
#define _HASHTABLE_H
/*--------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rwlock.h"
#include "common.h"
/*--------------------------------------------------------------------*/
#define DEFAULT_HASH_SIZE 1024
/*--------------------------------------------------------------------*/
typedef struct node_t
{
    char *key;
    size_t key_size;
    char *value;
    size_t value_size;
    struct node_t *next;
} node_t;
/*--------------------------------------------------------------------*/
typedef struct hashtable_t
{
    node_t **buckets;
    rwlock_t *locks;
    size_t *bucket_sizes; // number of entries in each bucket
    size_t hash_size;
} hashtable_t;
/*--------------------------------------------------------------------*/
/**
 * Calculates hash of key
 */
int hash(const char *key, size_t hash_size);
/*--------------------------------------------------------------------*/
/**
 * Initializes a hash table
 */
hashtable_t *hash_init(size_t hash_size, int delay);
/*--------------------------------------------------------------------*/
/**
 * Destroys a hash table
 */
int hash_destroy(hashtable_t *table);
/*--------------------------------------------------------------------*/
/**
 * Inserts a key-value pair to the hash table.
 * Returns -1 when any internal errors occur.
 * Returns 1 when successfully inserted.
 * Returns 0 when the key already exists. (collision)
 */
int hash_insert(hashtable_t *table, const char *key, const char *value);
/*--------------------------------------------------------------------*/
/**
 * Searches a key-value pair in the hash table,
 * and copy the searched value to dst.
 * Returns -1 when any internal errors occur.
 * Returns 1 when successfully found.
 * Returns 0 when there is no such key found.
 *
 * !caveat!
 * hash_read() should keep rlock until copy is done.
 */
int hash_read(hashtable_t *table, const char *key, char *dst,
              int quick);
/*--------------------------------------------------------------------*/
/**
 * Updates a key-value pair in the hash table.
 * Returns -1 when any internal errors occur.
 * Returns 1 when successfully updated.
 * Returns 0 when there is no such key found.
 */
int hash_update(hashtable_t *table, const char *key, const char *value);
/*--------------------------------------------------------------------*/
/**
 * Deletes a key-value pair from the hash table.
 * Returns -1 when any internal errors occur.
 * Returns 1 when successfully deleted.
 * Returns 0 when there is no such key found.
 */
int hash_delete(hashtable_t *table, const char *key);
/*--------------------------------------------------------------------*/
/**
 * Dumps the hash table
 */
void hash_dump(hashtable_t *table);
/*--------------------------------------------------------------------*/
#endif // _HASHTABLE_H
