#include "precompiled.hpp"

#include "platform.hpp"
#include "wire.hpp"

#if defined ZMQ_HAVE_NORM

#include "norm_engine.hpp"
#ifdef ZMQ_USE_NORM_SOCKET_WRAPPER
#include "ip.hpp"
#endif

#include "session_base.hpp"
#include "v2_protocol.hpp"

void zmq::norm_engine2_t::send_data ()
{
    if (_send_buffer.get() == NULL) {
        _send_buffer.reset (
          new (std::nothrow) unsigned char[_out_batch_size]);
        alloc_assert (_send_buffer.get ());
    }

    if (_write_size == 0) {
        //
        //  First two bytes (sizeof uint16_t) are used to store message
        //  offset in following steps. Note that by passing our buffer to
        //  the get data function we prevent it from returning its own buffer.
        //

check_for_more:

        unsigned char *bf = _send_buffer.get () + sizeof (uint16_t);
        const size_t bfsz = (_out_batch_size) - sizeof (uint16_t);
        uint16_t offset = 0xffff;

        size_t bytes = _encoder.encode (&bf, bfsz);

        while (bytes < bfsz) {
            if (!_send_more && (offset == 0xffff)) {
                offset = static_cast<uint16_t> (bytes);
            }

            const int rc = zmq_session->pull_msg (&_outgoing_msg);

            if (rc == -1) {
                zmq_output_ready = false;
                break;
            }

            _send_more = (_outgoing_msg.flagsp () & msg_t::more) != 0;
            _encoder.load_msg (&_outgoing_msg);

            bf = _send_buffer.get () + sizeof (uint16_t) + bytes;
            bytes += _encoder.encode (&bf, bfsz - bytes);
        }

        //
        //  Bail out if there are no data to write.
        //

        if (bytes == 0) {
            return;
        }

        _write_size = sizeof (uint16_t) + bytes;

        //
        //  Put offset information in the buffer.
        //

        put_uint16 (_send_buffer.get (), offset);
    }

    const size_t bytes_written = NormStreamWrite (
      norm_tx_stream, reinterpret_cast<char *>(_send_buffer.get ()), _write_size);

    if (bytes_written == _write_size) {
        //
        // The whole chunk of clubbed messages was written, flush the stream.
        //

        NormStreamFlush (norm_tx_stream, true, NORM_FLUSH_ACTIVE);
        _write_size = 0;
#if defined (ZMQ_GREEDY_MSG_CLUBBING)
        goto check_for_more;
#endif
    } else {
        //
        // NORM stream buffer full, wait for NORM_TX_QUEUE_VACANCY.
        //

        _write_size = (_write_size - bytes_written);
        norm_tx_ready = false;
    }
}

void zmq::norm_engine2_t::recv_data (NormObjectHandle object)
{
    PeerStreamState *peer_state =
      (PeerStreamState *) NormObjectGetUserData (object);

    if (!peer_state) {
        peer_state = new (std::nothrow) PeerStreamState (zmq_session, _in_batch_size, _max_message_size);
        alloc_assert (peer_state);
        NormObjectSetUserData (object, peer_state);
    }

    peer_state->ProcessInput (object);
}

int zmq::norm_engine2_t::PeerStreamState::ProcessInput (
  NormObjectHandle object)
{
    while (true) {
        _read_size = this->_in_batch_size;

        if (NormStreamRead (object, (char *) _receive_buffer.get (),
                            (unsigned int *) &_read_size)
            && (_read_size >= sizeof (uint16_t))) {

            unsigned char *bf = _receive_buffer.get ();

            //
            // Read the offset of the fist message in the current packet.
            //

            uint16_t offset = get_uint16 (bf);
            bf += sizeof (uint16_t);
            _read_size -= sizeof (uint16_t);

            if (!_joined) {
                //
                // If there is no beginning of the message in current packet,
                // ignore the data.
                //

                if (offset == 0xffff) {
                    continue;
                }

                zmq_assert (offset <= _read_size);

                //
                // We have to move data to the beginning of the first message.
                //

                bf += offset;
                _read_size -= offset;

                //
                // Mark the stream as joined.
                //

                _joined = true;
            }

            while (_read_size > 0) {
                //
                // Finish decoding any partial message, or begin decoding a new one
                //

                size_t decoded_bytes = 0;
                int rc = _decoder.decode (bf, _read_size, decoded_bytes);

                if (rc == -1) {
                    return -1;
                }

                bf += decoded_bytes;
                _read_size -= decoded_bytes;

                if (rc == 0) {
                    break;
                }

                //
                // Push the message in the session
                //

                rc = _session->push_msg (_decoder.msg ());

                if (rc == -1) {
                    errno_assert (errno == EAGAIN);
                    return -1;
                }
            }
        } else {
            return 0;
        }
    }
}

#endif // ZMQ_HAVE_NORM
