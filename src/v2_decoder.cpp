/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include <stdlib.h>
#include <string.h>
#include <cmath>

#include "v2_protocol.hpp"
#include "v2_decoder.hpp"
#include "likely.hpp"
#include "wire.hpp"
#include "err.hpp"

zmq::v2_decoder_t::v2_decoder_t (size_t bufsize_,
                                 int64_t maxmsgsize_,
                                 bool zero_copy_) :
    decoder_base_t<v2_decoder_t, shared_message_memory_allocator> (bufsize_),
    _msg_flags (0),
    _zero_copy (zero_copy_),
    _max_msg_size (maxmsgsize_)
{
    int rc = _in_progress.init ();
    errno_assert (rc == 0);

    //  At the beginning, read one byte and go to flags_ready state.
    next_step (_tmpbuf, 1, &v2_decoder_t::flags_ready);
}

zmq::v2_decoder_t::~v2_decoder_t ()
{
    const int rc = _in_progress.close ();
    errno_assert (rc == 0);
}

int zmq::v2_decoder_t::flags_ready (unsigned char const *)
{
    _msg_flags = 0;
    if (_tmpbuf[0] & v2_protocol_t::more_flag)
        _msg_flags |= msg_t::more;
    if (_tmpbuf[0] & v2_protocol_t::command_flag)
        _msg_flags |= msg_t::command;

    //  The payload length is either one or eight bytes,
    //  depending on whether the 'large' bit is set.
    if (_tmpbuf[0] & v2_protocol_t::large_flag)
        next_step (_tmpbuf, 8, &v2_decoder_t::eight_byte_size_ready);
    else
        next_step (_tmpbuf, 1, &v2_decoder_t::one_byte_size_ready);

    return 0;
}

int zmq::v2_decoder_t::one_byte_size_ready (unsigned char const *read_from_)
{
    return size_ready (_tmpbuf[0], read_from_);
}

int zmq::v2_decoder_t::eight_byte_size_ready (unsigned char const *read_from_)
{
    //  The payload size is encoded as 64-bit unsigned integer.
    //  The most significant byte comes first.
    const uint64_t msg_size = get_uint64 (_tmpbuf);

    return size_ready (msg_size, read_from_);
}

int zmq::v2_decoder_t::size_ready (uint64_t msg_size_,
                                   unsigned char const *read_pos_)
{
    //  Message size must not exceed the maximum allowed size.
    if (_max_msg_size >= 0)
        if (unlikely (msg_size_ > static_cast<uint64_t> (_max_msg_size))) {
            errno = EMSGSIZE;
            return -1;
        }

    //  Message size must fit into size_t data type.
    if (unlikely (msg_size_ != static_cast<size_t> (msg_size_))) {
        errno = EMSGSIZE;
        return -1;
    }

    int rc = _in_progress.close ();
    assert (rc == 0);

    // the current message can exceed the current buffer. We have to copy the buffer
    // data into a new message and complete it in the next receive.

    shared_message_memory_allocator &allocator = get_allocator ();
    if (unlikely (!_zero_copy
                  || msg_size_ > static_cast<size_t> (
                       allocator.data () + allocator.size () - read_pos_))) {
        // a new message has started, but the size would exceed the pre-allocated arena
        // this happens every time when a message does not fit completely into the buffer
        rc = _in_progress.init_size (static_cast<size_t> (msg_size_));
    } else {
        // construct message using n bytes from the buffer as storage
        // increase buffer ref count
        // if the message will be a large message, pass a valid refcnt memory location as well
        rc =
          _in_progress.init (const_cast<unsigned char *> (read_pos_),
                             static_cast<size_t> (msg_size_),
                             shared_message_memory_allocator::call_dec_ref,
                             allocator.buffer (), allocator.provide_content ());

        // For small messages, data has been copied and refcount does not have to be increased
        if (_in_progress.is_zcmsg ()) {
            allocator.advance_content ();
            allocator.inc_ref ();
        }
    }

    if (unlikely (rc)) {
        errno_assert (errno == ENOMEM);
        rc = _in_progress.init ();
        errno_assert (rc == 0);
        errno = ENOMEM;
        return -1;
    }

    _in_progress.set_flags (_msg_flags);
    // this sets read_pos to
    // the message data address if the data needs to be copied
    // for small message / messages exceeding the current buffer
    // or
    // to the current start address in the buffer because the message
    // was constructed to use n bytes from the address passed as argument
    next_step (_in_progress.datap (), _in_progress.sizep (),
               &v2_decoder_t::message_ready);

    return 0;
}

int zmq::v2_decoder_t::message_ready (unsigned char const *)
{
    //  Message is completely read. Signal this to the caller
    //  and prepare to decode next message.
    next_step (_tmpbuf, 1, &v2_decoder_t::flags_ready);
    return 1;
}
