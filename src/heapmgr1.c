/*--------------------------------------------------------------------*/
/* heapmgrbase.c                                                      */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "chunk.h"  /* Provides Chunk_T and span-based header API */

#define FALSE 0
#define TRUE  1

/* Minimum number of *payload* units to request on heap growth.
 * (The actual request adds 2 units on top for header and footer.) */
enum { SYS_MIN_ALLOC_UNITS = 1024 };

/* Head of the free list (ordered by ascending address). */
static Chunk_T s_free_head = NULL;

/* Heap bounds: [s_heap_lo, s_heap_hi).
 * s_heap_hi moves forward whenever the heap grows. */
static void *s_heap_lo = NULL, *s_heap_hi = NULL;

/*--------------------------------------------------------------------*/
/* check_heap_validity
 *
 * Lightweight integrity checks for the entire heap and the free list.
 * This is a *basic* sanity check. Passing it does not prove correctness
 * of all invariants, but it helps catch common structural bugs.
 *
 * Returns TRUE (1) on success, FALSE (0) on failure.
 *
 * Checks performed:
 *  - Heap bounds are initialized.
 *  - Every physical block within [s_heap_lo, s_heap_hi) passes chunk_is_valid.
 *  - Each node in the free list is marked CHUNK_FREE and passes chunk_is_valid.
 *  - Adjacency/Free-List consistency: if a free block's physical successor
 *    is exactly its next free block, then they should have been coalesced
 *    already (report as "uncoalesced").
 */
#ifndef NDEBUG
static int
check_heap_validity(void)
{
    Chunk_T w;

    if (s_heap_lo == NULL) { fprintf(stderr, "Uninitialized heap start\n"); return FALSE; }
    if (s_heap_hi == NULL) { fprintf(stderr, "Uninitialized heap end\n");   return FALSE; }

    if (s_heap_lo == s_heap_hi) {
        if (s_free_head == NULL) return TRUE;
        fprintf(stderr, "Inconsistent empty heap\n");
        return FALSE;
    }

    /* Walk all physical blocks in address order. */
    for (w = (Chunk_T)s_heap_lo;
         w && w < (Chunk_T)s_heap_hi;
         w = chunk_get_adjacent(w, s_heap_lo, s_heap_hi)) {
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;
    }

    /* Walk the free list; ensure nodes are free and not trivially coalescible. */
    for (w = s_free_head; w; w = chunk_get_next_free(w)) {
        Chunk_T n;

        if (chunk_get_status(w) != CHUNK_FREE) {
            fprintf(stderr, "Non-free chunk in the free list\n");
            return FALSE;
        }
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;

        n = chunk_get_adjacent(w, s_heap_lo, s_heap_hi);
        if (n != NULL && n == chunk_get_next_free(w)) {
            fprintf(stderr, "Uncoalesced adjacent free chunks\n");
            return FALSE;
        }
    }
    return TRUE;
}
#endif /* NDEBUG */

/*--------------------------------------------------------------------*/
/* bytes_to_payload_units
 *
 * Convert a byte count to the number of *payload* units, rounding up
 * to the nearest multiple of CHUNK_UNIT. The result does not include
 * the header unit. */
static size_t
bytes_to_payload_units(size_t bytes)
{
    return (bytes + (CHUNK_UNIT - 1)) / CHUNK_UNIT;
}

/*--------------------------------------------------------------------*/
/* header_from_payload
 *
 * Map a client data pointer back to its block header pointer by
 * stepping one header unit backward. */
static Chunk_T
header_from_payload(void *p)
{
    return (Chunk_T)((char *)p - CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* heap_bootstrap
 *
 * Initialize heap bounds using sbrk(0). Must be called exactly once
 * before any allocation occurs. Exits the process on fatal failure. */
static void
heap_bootstrap(void)
{
    s_heap_lo = s_heap_hi = sbrk(0);
    if (s_heap_lo == (void *)-1) {
        fprintf(stderr, "sbrk(0) failed\n");
        exit(-1);
    }
}

/*--------------------------------------------------------------------*/
/* split_for_alloc
 *
 * Split a free block 'c' into:
 *   - a smaller *leading* free block of span 'remain_span'
 *   - a *trailing* allocated block 'alloc' of span 'alloc_span'
 *
 * Inputs:
 *   c          : free block to split
 *   need_units : required *payload* units (does not include header or footer)
 *
 * Computations:
 *   alloc_span  = 2 (header + footer) + need_units
 *   remain_span = span(c) - alloc_span
 *
 * Pre-conditions:
 *   remain_span > 2 (i.e., the split leaves a free block with at least
 *   header and footer units).
 *
 * Post-conditions:
 *   The leading free block remains in the free list with its links preserved.
 *
 * Returns the header pointer of the newly created allocated block. */
static Chunk_T
split_for_alloc(Chunk_T c, size_t need_units)
{
    Chunk_T alloc, prev_f;
    int old_span    = chunk_get_span_units(c);
    int alloc_span  = (int)(2 + need_units);
    int remain_span = old_span - alloc_span;

    assert(c >= (Chunk_T)s_heap_lo && c < (Chunk_T)s_heap_hi);
    assert(chunk_get_status(c) == CHUNK_FREE);
    assert(remain_span > 2);

    prev_f = chunk_get_prev_free(c);

    /* Shrink the leading free block. */
    chunk_set_span_units(c, remain_span);
    chunk_set_status(c, CHUNK_FREE);
    chunk_set_prev_free(c, prev_f);
    
    /* The allocated block begins immediately after the smaller free block. */
    alloc = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);
    chunk_set_span_units(alloc, alloc_span);
    chunk_set_status(alloc, CHUNK_USED);
    chunk_set_prev_free(alloc, NULL);
    chunk_set_next_free(alloc, NULL);

    return alloc;
}

/*--------------------------------------------------------------------*/
/* freelist_detach
 *
 * Remove block 'c' from the doubly-linked free list using O(1) footer-based
 * prev_free pointer. The block is marked as CHUNK_USED afterwards.
 *
 * This function uses the footer's prev_free pointer to locate the previous
 * node in the free list, eliminating the need to traverse the list. */
static void
freelist_detach(Chunk_T c)
{
    assert(chunk_get_status(c) == CHUNK_FREE);

    Chunk_T prev_f, next_f;
    
    prev_f = chunk_get_prev_free(c);
    next_f = chunk_get_next_free(c);

    if (prev_f == NULL)
        s_free_head = next_f;
    else
        chunk_set_next_free(prev_f, next_f);

    if (next_f != NULL)
        chunk_set_prev_free(next_f, prev_f);

    chunk_set_status(c, CHUNK_USED);
    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, NULL);
}

/* Forward declaration */
void heapmgr_free(void *p);

/*--------------------------------------------------------------------*/
/* sys_grow_and_link
 *
 * Request more memory from the system via sbrk() and link the new
 * block into the free list (coalescing when possible).
 *
 * Inputs:
 *   need_units : required *payload* units
 *
 * Actions:
 *   - Compute grow_span = 2 (header + footer) + max(need_units, SYS_MIN_ALLOC_UNITS).
 *   - sbrk(grow_span * CHUNK_UNIT) to obtain one big block.
 *   - Temporarily mark it USED (to avoid free-list invariants while
 *     inserting) and then free it using heapmgr_free() which handles
 *     insertion and coalescing automatically.
 *
 * Returns the head of the free list after insertion/coalescing. */
static Chunk_T
sys_grow_and_link(size_t need_units)
{
    Chunk_T c;
    size_t grow_data = (need_units < SYS_MIN_ALLOC_UNITS) ? SYS_MIN_ALLOC_UNITS : need_units;
    size_t grow_span = 2 + grow_data;  /* header + payload + footer units */

    c = (Chunk_T)sbrk(grow_span * CHUNK_UNIT);
    if (c == (Chunk_T)-1)
        return NULL;

    s_heap_hi = sbrk(0);

    chunk_set_span_units(c, (int)grow_span);
    chunk_set_status(c, CHUNK_USED);   /* will flip to FREE once inserted */
    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, NULL);

    heapmgr_free((void *)((char *)c + CHUNK_UNIT));

    assert(check_heap_validity());
    return s_free_head;
}

/*--------------------------------------------------------------------*/
/* heapmgr_malloc
 *
 * Allocate a block capable of holding 'size' bytes. Zero bytes returns
 * NULL. The allocated region is *uninitialized*. Strategy:
 *  1) Convert 'size' to payload units (excluding header and footer).
 *  2) First-fit search the free list for a block whose payload >= need.
 *     Payload = span - 2 (excludes header and footer).
 *  3) If found:
 *       - split if payload > need_units + 2 (leaves enough for header+footer);
 *       - otherwise detach as exact fit.
 *     Else:
 *       - grow the heap and repeat the same split/detach logic.
 *  4) Return the payload pointer (header + 1 unit). */
void *
heapmgr_malloc(size_t size)
{
    static int booted = FALSE;
    Chunk_T cur;
    size_t need_units;

    if (size == 0)
        return NULL;

    if (!booted) { heap_bootstrap(); booted = TRUE; }

    assert(check_heap_validity());

    need_units = bytes_to_payload_units(size);

    /* First-fit scan: usable payload units = span - 2 (exclude header and footer). */
    for (cur = s_free_head; cur != NULL; cur = chunk_get_next_free(cur)) {
        size_t cur_payload = (size_t)chunk_get_span_units(cur) - 2;

        if (cur_payload >= need_units) {
            if (cur_payload > need_units + 2)
                cur = split_for_alloc(cur, need_units);
            else
                freelist_detach(cur);

            assert(check_heap_validity());
            return (void *)((char *)cur + CHUNK_UNIT);
        }
    }

    /* Need to grow the heap. */
    cur = sys_grow_and_link(need_units);
    if (cur == NULL) {
        assert(check_heap_validity());
        return NULL;
    }

    /* Final split/detach on the grown block. */
    if ((size_t)chunk_get_span_units(cur) - 2 > need_units + 2)
        cur = split_for_alloc(cur, need_units);
    else
        freelist_detach(cur);

    assert(check_heap_validity());
    return (void *)((char *)cur + CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* heapmgr_free
 *
 * Free a previously allocated block pointed to by 'p'. If 'p' is NULL,
 * do nothing. The pointer must have been returned by heapmgr_malloc().
 * 
 * Strategy (O(1) insertion using footer-based doubly-linked list):
 *  1) Validate heap structure (debug only).
 *  2) Map payload pointer to its header.
 *  3) Check physically adjacent blocks (prev and next) for coalescing:
 *     - If prev is free: detach it from free list and merge with current block.
 *     - If next is free: detach it from free list and merge with current block.
 *  4) Insert the (possibly coalesced) block at the head of the free list.
 *
 * This implementation uses O(1) operations by leveraging footer-based
 * prev_free pointers to detach adjacent free blocks without traversal. */
void
heapmgr_free(void *p)
{
    Chunk_T c, prev, next;

    if (p == NULL)
        return;

    assert(check_heap_validity());

    c = header_from_payload(p);
    assert(chunk_get_status(c) != CHUNK_FREE);

    prev = chunk_get_prev_adjacent(c, s_heap_lo, s_heap_hi);
    next = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);

    /* Coalesce with previous free block if adjacent. */
    if (prev != NULL && chunk_get_status(prev) == CHUNK_FREE) {
        freelist_detach(prev);
        chunk_set_span_units(prev, chunk_get_span_units(prev) + chunk_get_span_units(c));
        c = prev;
    }
    /* Coalesce with next free block if adjacent. */
    if (next != NULL && chunk_get_status(next) == CHUNK_FREE) {
        freelist_detach(next);
        chunk_set_span_units(c, chunk_get_span_units(c) + chunk_get_span_units(next));
    }

    /* Insert the (possibly coalesced) block at the head of free list. */
    chunk_set_status(c, CHUNK_FREE);
    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, s_free_head);

    if (s_free_head != NULL)
        chunk_set_prev_free(s_free_head, c);

    s_free_head = c;

    assert(check_heap_validity());
}
