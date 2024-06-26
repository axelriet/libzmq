/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "decoder_allocators.hpp"

#include "msg.hpp"

zmq::shared_message_memory_allocator::shared_message_memory_allocator (
  std::size_t bufsize_) :
    _buf (NULL),
    _buf_size (0),
    _max_size (bufsize_),
    _msg_content (NULL),
    _max_counters ((_max_size + msg_t::max_vsm_size - 1) / msg_t::max_vsm_size)
{
}

zmq::shared_message_memory_allocator::shared_message_memory_allocator (
  std::size_t bufsize_, std::size_t max_messages_) :
    _buf (NULL),
    _buf_size (0),
    _max_size (bufsize_),
    _msg_content (NULL),
    _max_counters (max_messages_)
{
}

zmq::shared_message_memory_allocator::~shared_message_memory_allocator ()
{
    deallocate ();
}

unsigned char *zmq::shared_message_memory_allocator::allocate ()
{
    if (_buf) {
        // release reference count to couple lifetime to messages
        zmq::atomic_counter_t *c =
          reinterpret_cast<zmq::atomic_counter_t *> (_buf);

        // if refcnt drops to 0, there are no message using the buffer
        // because either all messages have been closed or only vsm-messages
        // were created
        if (c->sub (1)) {
            // buffer is still in use as message data. "Release" it and create a new one
            // release pointer because we are going to create a new buffer
            release ();
        }
    }

    // if buf != NULL it is not used by any message so we can re-use it for the next run
    if (!_buf) {
        // allocate memory for reference counters together with reception buffer
        std::size_t const allocationsize =
          _max_size + sizeof (zmq::atomic_counter_t)
          + _max_counters * sizeof (zmq::msg_t::content_t);
#ifdef ZMQ_HAVE_CUSTOM_ALLOCATOR
        _buf = static_cast<unsigned char *> (
          zmq::malloc (allocationsize, ZMQ_MSG_ALLOC_HINT_INCOMING));
#ifndef NDEBUG
        _messages_allocated = true;
#endif
#else
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
        _buf = static_cast<unsigned char *> (scalable_malloc (allocationsize));
#else
        _buf = static_cast<unsigned char *> (std::malloc (allocationsize));
#endif
#endif
        alloc_assert (_buf);

        new (_buf) atomic_counter_t (1);
    } else {
        // release reference count to couple lifetime to messages
        zmq::atomic_counter_t *c =
          reinterpret_cast<zmq::atomic_counter_t *> (_buf);
        c->set (1);
    }

    _buf_size = _max_size;
    _msg_content = reinterpret_cast<zmq::msg_t::content_t *> (
      _buf + sizeof (atomic_counter_t) + _max_size);
    return _buf + sizeof (zmq::atomic_counter_t);
}

void zmq::shared_message_memory_allocator::deallocate ()
{
    zmq::atomic_counter_t *c = reinterpret_cast<zmq::atomic_counter_t *> (_buf);
    if (_buf && !c->sub (1)) {
        c->~atomic_counter_t ();
#ifdef ZMQ_HAVE_CUSTOM_ALLOCATOR
        zmq::free (_buf, ZMQ_MSG_ALLOC_HINT_INCOMING);
#else
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
        scalable_free (_buf);
#else
        std::free (_buf);
#endif
#endif
    }
    clear ();
}

unsigned char *zmq::shared_message_memory_allocator::release ()
{
    unsigned char *b = _buf;
    clear ();
    return b;
}

void zmq::shared_message_memory_allocator::clear ()
{
    _buf = NULL;
    _buf_size = 0;
    _msg_content = NULL;
}

void zmq::shared_message_memory_allocator::inc_ref ()
{
    (reinterpret_cast<zmq::atomic_counter_t *> (_buf))->add (1);
}

void ZMQ_CDECL zmq::shared_message_memory_allocator::call_dec_ref (void *,
                                                                   void *hint_)
{
    zmq_assert (hint_);
    unsigned char *buf = static_cast<unsigned char *> (hint_);
    zmq::atomic_counter_t *c = reinterpret_cast<zmq::atomic_counter_t *> (buf);

    if (!c->sub (1)) {
        c->~atomic_counter_t ();
#ifdef ZMQ_HAVE_CUSTOM_ALLOCATOR
        zmq::free (buf, ZMQ_MSG_ALLOC_HINT_INCOMING);
#else
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
        scalable_free (buf);
#else
        std::free (buf);
#endif
#endif
        buf = NULL;
    }
}


std::size_t zmq::shared_message_memory_allocator::size () const
{
    return _buf_size;
}

unsigned char *zmq::shared_message_memory_allocator::data ()
{
    return _buf + sizeof (zmq::atomic_counter_t);
}
