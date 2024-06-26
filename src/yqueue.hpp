/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_YQUEUE_HPP_INCLUDED__
#define __ZMQ_YQUEUE_HPP_INCLUDED__

#include <stdlib.h>
#include <stddef.h>

#include "err.hpp"
#include "atomic_ptr.hpp"
#include "platform.hpp"

#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
#include <tbb/scalable_allocator.h>
#endif

namespace zmq
{
//  yqueue is an efficient queue implementation. The main goal is
//  to minimise number of allocations/deallocations needed. Thus yqueue
//  allocates/deallocates elements in batches of N.
//
//  yqueue allows one thread to use push/back function and another one
//  to use pop/front functions. However, user must ensure that there's no
//  pop on the empty queue and that both threads don't access the same
//  element in unsynchronised manner.
//
//  T is the type of the object in the queue.
//  N is granularity of the queue (how many pushes have to be done till
//  actual memory allocation is required).
#if defined HAVE_POSIX_MEMALIGN
// ALIGN is the memory alignment size to use in the case where we have
// posix_memalign available. Default value is 64, this alignment will
// prevent two queue chunks from occupying the same CPU cache line on
// architectures where cache lines are <= 64 bytes (e.g. most things
// except POWER). It is detected at build time to try to account for other
// platforms like POWER and s390x.
template <typename T, int N, size_t ALIGN = ZMQ_CACHELINE_SIZE> class yqueue_t
#else
template <typename T, int N> class yqueue_t
#endif
{
  public:
    //  Create the queue.
    inline yqueue_t ()
    {
        _begin_chunk = allocate_chunk ();
        alloc_assert (_begin_chunk);
        _begin_pos = 0;
        _back_chunk = NULL;
        _back_pos = 0;
        _end_chunk = _begin_chunk;
        _end_pos = 0;
    }

    //  Destroy the queue.
    inline ~yqueue_t ()
    {
        while (true) {
            if (_begin_chunk == _end_chunk) {
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
                scalable_aligned_free (_begin_chunk);
#else
#if _MSC_VER
                _aligned_free (_begin_chunk);
#else
                std::free (_begin_chunk);
#endif
#endif
                break;
            }
            chunk_t *o = _begin_chunk;
            _begin_chunk = _begin_chunk->next;
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
            scalable_aligned_free (o);
#else
#if _MSC_VER
            _aligned_free (o);
#else
            std::free (o);
#endif
#endif
        }

        chunk_t *sc = _spare_chunk.xchg (NULL);
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
        scalable_aligned_free (sc);
#else
#if _MSC_VER
        _aligned_free (sc);
#else
        std::free (sc);
#endif
#endif
    }

    //  Returns reference to the front element of the queue.
    //  If the queue is empty, behaviour is undefined.
    inline T &front () { return _begin_chunk->values[_begin_pos]; }

    //  Returns reference to the back element of the queue.
    //  If the queue is empty, behaviour is undefined.
    inline T &back () { return _back_chunk->values[_back_pos]; }

    //  Adds an element to the back end of the queue.
    inline void push ()
    {
        _back_chunk = _end_chunk;
        _back_pos = _end_pos;

        if (++_end_pos != N)
            return;

        chunk_t *sc = _spare_chunk.xchg (NULL);
        if (sc) {
            _end_chunk->next = sc;
            sc->prev = _end_chunk;
        } else {
            _end_chunk->next = allocate_chunk ();
            alloc_assert (_end_chunk->next);
            _end_chunk->next->prev = _end_chunk;
        }
        _end_chunk = _end_chunk->next;
        _end_pos = 0;
    }

    //  Removes element from the back end of the queue. In other words
    //  it rollbacks last push to the queue. Take care: Caller is
    //  responsible for destroying the object being unpushed.
    //  The caller must also guarantee that the queue isn't empty when
    //  unpush is called. It cannot be done automatically as the read
    //  side of the queue can be managed by different, completely
    //  unsynchronised thread.
    inline void unpush ()
    {
        //  First, move 'back' one position backwards.
        if (_back_pos)
            --_back_pos;
        else {
            _back_pos = N - 1;
            _back_chunk = _back_chunk->prev;
        }

        //  Now, move 'end' position backwards. Note that obsolete end chunk
        //  is not used as a spare chunk. The analysis shows that doing so
        //  would require free and atomic operation per chunk deallocated
        //  instead of a simple free.
        if (_end_pos)
            --_end_pos;
        else {
            _end_pos = N - 1;
            _end_chunk = _end_chunk->prev;
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
            scalable_aligned_free (_end_chunk->next);
#else
#if _MSC_VER
            _aligned_free (_end_chunk->next);
#else
            std::free (_end_chunk->next);
#endif
#endif
            _end_chunk->next = NULL;
        }
    }

    //  Removes an element from the front end of the queue.
    inline void pop ()
    {
        if (++_begin_pos == N) {
            chunk_t *o = _begin_chunk;
            _begin_chunk = _begin_chunk->next;
            _begin_chunk->prev = NULL;
            _begin_pos = 0;

            //  'o' has been more recently used than _spare_chunk,
            //  so for cache reasons we'll get rid of the spare and
            //  use 'o' as the spare.
            chunk_t *cs = _spare_chunk.xchg (o);
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
            scalable_aligned_free (cs);
#else
#if _MSC_VER
            _aligned_free (cs);
#else
            std::free (cs);
#endif
#endif
        }
    }

  private:
    //  Individual memory chunk to hold N elements.
    struct chunk_t
    {
        T values[N];
        chunk_t *prev;
        chunk_t *next;
    };

    static inline chunk_t *allocate_chunk ()
    {
#if defined HAVE_POSIX_MEMALIGN
        void *pv;
        if (posix_memalign (&pv, ALIGN, sizeof (chunk_t)) == 0)
            return (chunk_t *) pv;
        return NULL;
#else
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
        return static_cast<chunk_t *> (
          scalable_aligned_malloc (sizeof (chunk_t), ZMQ_CACHELINE_SIZE));
#else
#if _MSC_VER
        return static_cast<chunk_t *> (
          _aligned_malloc (sizeof (chunk_t), ZMQ_CACHELINE_SIZE));
#else
        return static_cast<chunk_t *> (std::malloc (sizeof (chunk_t)));
#endif
#endif
#endif
    }

    //  Back position may point to invalid memory if the queue is empty,
    //  while begin & end positions are always valid. Begin position is
    //  accessed exclusively be queue reader (front/pop), while back and
    //  end positions are accessed exclusively by queue writer (back/push).
    chunk_t *_begin_chunk;
    int _begin_pos;
    chunk_t *_back_chunk;
    int _back_pos;
    chunk_t *_end_chunk;
    int _end_pos;

    //  People are likely to produce and consume at similar rates.  In
    //  this scenario holding onto the most recently freed chunk saves
    //  us from having to call malloc/free.
    atomic_ptr_t<chunk_t> _spare_chunk;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (yqueue_t)
};
}

#endif
