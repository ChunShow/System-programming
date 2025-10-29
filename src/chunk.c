/*--------------------------------------------------------------------*/
/* chunk.c                                                            */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "chunk.h"

/* Internal header layout
 * - status: CHUNK_FREE or CHUNK_USED
 * - span:   total units, including the header itself
 * - next:   next-free pointer for the doubly-linked free list
 * 
 * Footer layout (at the end of each block):
 * - status: same as header
 * - span:   same as header
 * - next:   prev-free pointer (points to previous free block)
 */
struct Chunk {
    int     status;
    int     span;
    Chunk_T next;
};

/* ----------------------- Getters / Setters ------------------------ */
int   chunk_get_status(Chunk_T c)                 { return c->status; }
void  chunk_set_status(Chunk_T c, int status)     { 
    c->status = status; 
    /* Update footer as well */
    Chunk_T footer = (Chunk_T)((char *)c + (c->span - 1) * CHUNK_UNIT);
    footer->status = status;
}

int   chunk_get_span_units(Chunk_T c)             { return c->span; }
void  chunk_set_span_units(Chunk_T c, int span_u) { 
    c->span = span_u; 
    /* Update footer as well */
    Chunk_T footer = (Chunk_T)((char *)c + (span_u - 1) * CHUNK_UNIT);
    footer->span = span_u;
}

Chunk_T chunk_get_next_free(Chunk_T c)            { return c->next; }
void    chunk_set_next_free(Chunk_T c, Chunk_T n) { c->next = n; }

/* Footer-based prev functions */
Chunk_T chunk_get_prev_free(Chunk_T c) {
    Chunk_T footer = (Chunk_T)((char *)c + (c->span - 1) * CHUNK_UNIT);
    return footer->next;
}

void chunk_set_prev_free(Chunk_T c, Chunk_T p) {
    Chunk_T footer = (Chunk_T)((char *)c + (c->span - 1) * CHUNK_UNIT);
    footer->next = p;
}

/* chunk_get_adjacent:
 * Compute the next physical block header by jumping 'span' units
 * (1 header + payload) forward from 'c'. If that address falls at or
 * beyond 'end', return NULL to indicate there is no next block. */
Chunk_T
chunk_get_adjacent(Chunk_T c, void *start, void *end)
{
    Chunk_T n;

    assert((void *)c >= start);

    n = c + c->span;                /* span includes the header itself */

    if ((void *)n >= end)
        return NULL;

    return n;
}

/*--------------------------------------------------------------------*/
/* chunk_get_prev_adjacent
 *
 * Return the physically previous adjacent block's header (if any) by walking
 * backward from 'c'. Returns NULL if 'c' is the first block.
 * 'start' and 'end' are the inclusive start and exclusive end addresses
 * of the heap region. */
Chunk_T
chunk_get_prev_adjacent(Chunk_T c, void *start, void *end)
{
    Chunk_T prev_footer, prev;
    
    assert((void *)c >= start);
    if ((void *)c == start)
        return NULL;

    /* Walk backward by looking at the footer of the previous block */
    prev_footer = (Chunk_T)((char *)c - CHUNK_UNIT);
    assert((void *)prev_footer > start);

    /* Get the actual header by walking back by the span stored in the footer */
    prev = (Chunk_T)((char *)c - prev_footer->span * CHUNK_UNIT);
    assert((void *)prev >= start);

    return prev;
}

#ifndef NDEBUG
/* chunk_is_valid:
 * Minimal per-block validity checks used by the heap validator:
 *  - c must lie within [start, end)
 *  - span must be positive (non-zero) */
int
chunk_is_valid(Chunk_T c, void *start, void *end)
{
    assert(c     != NULL);
    assert(start != NULL);
    assert(end   != NULL);

    if (c < (Chunk_T)start) { fprintf(stderr, "Bad heap start\n"); return 0; }
    if (c >= (Chunk_T)end)  { fprintf(stderr, "Bad heap end\n");   return 0; }
    if (c->span <= 1)       { fprintf(stderr, "Non-positive span\n"); return 0; }
    return 1;
}
#endif