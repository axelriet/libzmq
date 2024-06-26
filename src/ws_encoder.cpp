/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "ws_protocol.hpp"
#include "ws_encoder.hpp"
#include "msg.hpp"
#include "likely.hpp"
#include "wire.hpp"
#include "random.hpp"

#include <limits.h>

zmq::ws_encoder_t::ws_encoder_t (size_t bufsize_, bool must_mask_) :
    encoder_base_t<ws_encoder_t> (bufsize_), _must_mask (must_mask_)
{
    //  Write 0 bytes to the batch and go to message_ready state.
    next_step (NULL, 0, &ws_encoder_t::message_ready, true);
    _masked_msg.init ();
}

zmq::ws_encoder_t::~ws_encoder_t ()
{
    _masked_msg.close ();
}

void zmq::ws_encoder_t::message_ready ()
{
    int offset = 0;

    _is_binary = false;

    if (in_progress ()->is_ping ())
        _tmp_buf[offset++] = 0x80 | zmq::ws_protocol_t::opcode_ping;
    else if (in_progress ()->is_pong ())
        _tmp_buf[offset++] = 0x80 | zmq::ws_protocol_t::opcode_pong;
    else if (in_progress ()->is_close_cmd ())
        _tmp_buf[offset++] = 0x80 | zmq::ws_protocol_t::opcode_close;
    else {
        _tmp_buf[offset++] = 0x82; // Final | binary
        _is_binary = true;
    }

    _tmp_buf[offset] = _must_mask ? 0x80 : 0x00;

    size_t size = in_progress ()->sizep ();
    if (_is_binary)
        size++;
    //  TODO: create an opcode for subscribe/cancel
    if (in_progress ()->is_subscribe () || in_progress ()->is_cancel ())
        size++;

    if (size <= 125)
        _tmp_buf[offset++] |= static_cast<unsigned char> (size & 127);
    else if (size <= 0xFFFF) {
        _tmp_buf[offset++] |= 126;
        _tmp_buf[offset++] = static_cast<unsigned char> ((size >> 8) & 0xFF);
        _tmp_buf[offset++] = static_cast<unsigned char> (size & 0xFF);
    } else {
        _tmp_buf[offset++] |= 127;
        put_uint64 (_tmp_buf + offset, size);
        offset += 8;
    }

    if (_must_mask) {
        const uint32_t random = generate_random ();
        put_uint32 (_tmp_buf + offset, random);
        put_uint32 (_mask, random);
        offset += 4;
    }

    int mask_index = 0;
    if (_is_binary) {
        //  Encode flags.
        unsigned char protocol_flags = 0;
        if (in_progress ()->flagsp () & msg_t::more)
            protocol_flags |= ws_protocol_t::more_flag;
        if (in_progress ()->flagsp () & msg_t::command)
            protocol_flags |= ws_protocol_t::command_flag;

        _tmp_buf[offset++] =
          _must_mask ? protocol_flags ^ _mask[mask_index++] : protocol_flags;
    }

    //  Encode the subscribe/cancel byte.
    //  TODO: remove once there is an opcode for subscribe/cancel
    if (in_progress ()->is_subscribe ())
        _tmp_buf[offset++] = _must_mask ? 1 ^ _mask[mask_index++] : 1;
    else if (in_progress ()->is_cancel ())
        _tmp_buf[offset++] = _must_mask ? 0 ^ _mask[mask_index++] : 0;

    next_step (_tmp_buf, offset, &ws_encoder_t::size_ready, false);
}

void zmq::ws_encoder_t::size_ready ()
{
    if (_must_mask) {
        assert (in_progress () != &_masked_msg);
        const size_t size = in_progress ()->sizep ();

        unsigned char *src =
          static_cast<unsigned char *> (in_progress ()->datap ());
        unsigned char *dest = src;

        //  If msg is shared or data is constant we cannot mask in-place, allocate a new msg for it
        if (in_progress ()->flagsp () & msg_t::shared
            || in_progress ()->is_cmsg ()) {
            _masked_msg.close ();
            _masked_msg.init_size (size);
            dest = static_cast<unsigned char *> (_masked_msg.datap ());
        }

        int mask_index = 0;
        if (_is_binary)
            ++mask_index;
        //  TODO: remove once there is an opcode for subscribe/cancel
        if (in_progress ()->is_subscribe () || in_progress ()->is_cancel ())
            ++mask_index;
        for (size_t i = 0; i < size; ++i, mask_index++)
            dest[i] = src[i] ^ _mask[mask_index % 4];

        next_step (dest, size, &ws_encoder_t::message_ready, true);
    } else {
        next_step (in_progress ()->datap (), in_progress ()->sizep (),
                   &ws_encoder_t::message_ready, true);
    }
}
