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
        // A chunk starts with:
        // 
        //    uint32_t chunk size (total bytes in the chunk)
        //    uint32_t offset of first message in the chunk
        //    message data
        //

check_for_more:

        uint32_t offset = 0xffffffff;
        const size_t bfsz = (_out_batch_size) - (2 * sizeof (uint32_t));
        unsigned char *bf = _send_buffer.get () + (2 * sizeof (uint32_t));

        size_t bytes = _encoder.encode (&bf, bfsz);

        while (bytes < bfsz) {
            if (!_send_more && (offset == 0xffffffff)) {
                offset = static_cast<uint32_t> (bytes);
            }

            const int rc = zmq_session->pull_msg (&_outgoing_msg);

            if (rc == -1) {
                zmq_output_ready = false;
                break;
            }

            _send_more = (_outgoing_msg.flagsp () & msg_t::more) != 0;
            _encoder.load_msg (&_outgoing_msg);

            bf = _send_buffer.get () + (2 * sizeof (uint32_t)) + bytes;
            bytes += _encoder.encode (&bf, bfsz - bytes);
        }

        //
        //  Bail out if there are no data to write.
        //

        if (bytes == 0) {
            return;
        }

        _write_size = (2 * sizeof (uint32_t)) + bytes;

        //
        //  Put offset information in the buffer.
        //

        put_uint32 (_send_buffer.get (), _write_size);
        put_uint32 (_send_buffer.get () + sizeof (uint32_t), offset);
    }

    const size_t bytes_written = NormStreamWrite (
      norm_tx_stream, reinterpret_cast<char *>(_send_buffer.get ()), (unsigned int) _write_size);

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

        _write_size -= bytes_written;
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

    const int rc = peer_state->ProcessInput (object);

    if (rc == -1) {
        if (errno == EAGAIN) {
            zmq_input_ready = false;
        } else {
            zmq_assert (false);
        }
    }

    zmq_session->flush ();
}

void zmq::norm_engine2_t::object_aborted (NormObjectHandle object)
{
    PeerStreamState *peer_state =
      (PeerStreamState *) NormObjectGetUserData (object);

    if (peer_state) {
        delete peer_state;
        NormObjectSetUserData (object, NULL);
    }
}

int zmq::norm_engine2_t::PeerStreamState::ProcessInput (
  NormObjectHandle object)
{
    int rc;
    uint16_t offset;
    size_t decoded_bytes;

    while (true) {

        if (_retry_push) {
            _retry_push = false;
            goto retry_push;
        }

        if (_chunk_size > 0) {
            goto continue_chunk;
        }

        _read_size = _in_batch_size;

        if (NormStreamRead (object, (char *) _receive_buffer.get (),
                            (unsigned int *) &_read_size)) {

            if (_read_size < (2 * sizeof (uint32_t))) {
                _joined = false;
                return 0;
            }

            _receive_pointer = _receive_buffer.get ();

continue_chunk:

            //
            // Read the chunk size and the offset of the fist message in the chunk.
            //

            _chunk_size = get_uint32 (_receive_pointer);
            _receive_pointer += sizeof (uint32_t);
            _read_size -= sizeof (uint32_t);
            _chunk_size -= sizeof (uint32_t);

            offset = get_uint32 (_receive_pointer);
            _receive_pointer += sizeof (uint32_t);
            _read_size -= sizeof (uint32_t);
            _chunk_size -= sizeof (uint32_t);

            if (!_joined) {
                //
                // If there is no beginning of the message in current packet,
                // ignore the data.
                //

                if (offset == 0xffffffff) {
                    continue;
                }

                zmq_assert (offset <= _read_size);

                //
                // We have to move data to the beginning of the first message.
                //

                if (offset) {
                    _receive_pointer += offset;
                    _read_size -= offset;
                    _chunk_size -= offset;
                }

                //
                // Mark the stream as joined.
                //

                _joined = true;
            }

            while (_read_size > 0 && _chunk_size > 0) {
                //
                // Finish decoding any partial message, or begin decoding a new one
                //

                decoded_bytes = 0;
                rc = _decoder.decode (_receive_pointer, std::min(_read_size, _chunk_size), decoded_bytes);

                if (rc == -1) {
                    _joined = false;
                    return -1;
                }

                if (decoded_bytes == 0) {
                    break;
                } else {
                    _receive_pointer += decoded_bytes;
                    _read_size -= decoded_bytes;
                    _chunk_size -= decoded_bytes;
                }

                if (rc == 0) {
                    //
                    // Partial message
                    //

                    return 0;
                }

                //
                // Push the message to the session
                //

retry_push:     
                rc = _session->push_msg (_decoder.msg ());

                if (rc == -1) {
                    errno_assert (errno == EAGAIN);
                    _retry_push = true;
                    return -1;
                }
            }

            if (_read_size > _chunk_size) {
                goto continue_chunk;
            }
        } else {
            return 0;
        }
    }
}

#endif // ZMQ_HAVE_NORM
