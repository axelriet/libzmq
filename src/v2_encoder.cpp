/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "v2_protocol.hpp"
#include "v2_encoder.hpp"
#include "msg.hpp"
#include "likely.hpp"
#include "wire.hpp"

#include <limits.h>

zmq::v2_encoder_t::v2_encoder_t (size_t bufsize_) :
    encoder_base_t<v2_encoder_t> (bufsize_)
{
    //  Write 0 bytes to the batch and go to message_ready state.
    next_step (NULL, 0, &v2_encoder_t::message_ready, true);
}

zmq::v2_encoder_t::~v2_encoder_t ()
{
}

void zmq::v2_encoder_t::message_ready ()
{
    //  Encode flags.
    size_t size = in_progress ()->sizep ();
    size_t header_size = 2; // flags byte + size byte
    unsigned char &protocol_flags = _tmp_buf[0];
    protocol_flags = 0;
    if (in_progress ()->flagsp () & msg_t::more)
        protocol_flags |= v2_protocol_t::more_flag;
    if (in_progress ()->sizep () > UCHAR_MAX)
        protocol_flags |= v2_protocol_t::large_flag;
    if (in_progress ()->flagsp () & msg_t::command)
        protocol_flags |= v2_protocol_t::command_flag;
    if (in_progress ()->is_subscribe () || in_progress ()->is_cancel ())
        ++size;

    //  Encode the message length. For messages less then 256 bytes,
    //  the length is encoded as 8-bit unsigned integer. For larger
    //  messages, 64-bit unsigned integer in network byte order is used.
    if (unlikely (size > UCHAR_MAX)) {
        put_uint64 (_tmp_buf + 1, size);
        header_size = 9; // flags byte + size 8 bytes
    } else {
        _tmp_buf[1] = static_cast<uint8_t> (size);
    }

    //  Encode the subscribe/cancel byte. This is done in the encoder as
    //  opposed to when the subscribe message is created to allow different
    //  protocol behaviour on the wire in the v3.1 and legacy encoders.
    //  It results in the work being done multiple times in case the sub
    //  is sending the subscription/cancel to multiple pubs, but it cannot
    //  be avoided. This processing can be moved to xsub once support for
    //  ZMTP < 3.1 is dropped.
    if (in_progress ()->is_subscribe ())
        _tmp_buf[header_size++] = 1;
    else if (in_progress ()->is_cancel ())
        _tmp_buf[header_size++] = 0;

    next_step (_tmp_buf, header_size, &v2_encoder_t::size_ready, false);
}

void zmq::v2_encoder_t::size_ready ()
{
    //  Write message body into the buffer.
    next_step (in_progress ()->datap (), in_progress ()->sizep (),
               &v2_encoder_t::message_ready, true);
}
