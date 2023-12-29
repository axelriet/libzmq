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
    if (intermediate_buffer.get() == NULL) {
        intermediate_buffer.reset (
          new (std::nothrow) unsigned char[_out_batch_size]);
        alloc_assert (intermediate_buffer.get ());
    }


    if (write_size == 0) {
        //
        //  First two bytes (sizeof uint16_t) are used to store message
        //  offset in following steps. Note that by passing our buffer to
        //  the get data function we prevent it from returning its own buffer.
        //

check_for_more:

        unsigned char *bf = intermediate_buffer.get () + sizeof (uint16_t);
        size_t bfsz = (_out_batch_size) - sizeof (uint16_t);
        uint16_t offset = 0xffff;

        size_t bytes = encoder.encode (&bf, bfsz);

        while (bytes < bfsz) {
            if (!more && (offset == 0xffff)) {
                offset = static_cast<uint16_t> (bytes);
            }

            int rc = zmq_session->pull_msg (&msg);

            if (rc == -1) {
                zmq_output_ready = false;
                break;
            }

            more = msg.flagsp () & msg_t::more;
            encoder.load_msg (&msg);

            bf = intermediate_buffer.get () + sizeof (uint16_t) + bytes;
            bytes += encoder.encode (&bf, bfsz - bytes);
        }

        //
        //  Bail out if there are no data to write.
        //

        if (bytes == 0) {
            return;
        }

        write_size = sizeof (uint16_t) + bytes;

        //
        //  Put offset information in the buffer.
        //

        put_uint16 (intermediate_buffer.get (), offset);
    }

    size_t nbytes = NormStreamWrite (
      norm_tx_stream, (char *) intermediate_buffer.get (), write_size);

    if (nbytes == write_size) {
        //
        // The whole chunk of clubbed messages was written, flush the stream.
        //

        NormStreamFlush (norm_tx_stream, true, NORM_FLUSH_ACTIVE);
        write_size = 0;
#if defined (ZMQ_GREEDY_MSG_CLUBBING)
        goto check_for_more;
#endif
    } else {
        //
        // NORM stream buffer full, wait for NORM_TX_QUEUE_VACANCY.
        //

        write_size = (write_size - nbytes);
        norm_tx_ready = false;
    }
}

void zmq::norm_engine2_t::recv_data (NormObjectHandle object)
{
#if 0
    if (NORM_OBJECT_INVALID != object) {
        //
        // Call result of NORM_RX_OBJECT_UPDATED notification
        // This is a rx_ready indication for a new or existing rx stream
        // First, determine if this is a stream we already know
        //

        zmq_assert (NORM_OBJECT_STREAM == NormObjectGetType (object));
        //
        // Since there can be multiple senders (publishers), we keep
        // state for each separate rx stream.
        //

        NormRxStreamState *rxState =
          (NormRxStreamState *) NormObjectGetUserData (object);

        if (NULL == rxState) {
            //
            // This is a new stream, so create rxState with zmq decoder, etc
            //

            rxState = new (std::nothrow)
              NormRxStreamState (object, options.maxmsgsize, options.zero_copy,
                                 options.in_batch_size);
            errno_assert (rxState);

            if (!rxState->Init ()) {
                errno_assert (false);
                delete rxState;
                return;
            }

            NormObjectSetUserData (object, rxState);
        } else if (!rxState->IsRxReady ()) {
            //
            // Existing non-ready stream, so remove from pending
            // list to be promoted to rx_ready_list ...
            //

            rx_pending_list.Remove (*rxState);
        }

        if (!rxState->IsRxReady ()) {
            //
            // TBD - prepend up front for immediate service?
            //

            rxState->SetRxReady (true);
            rx_ready_list.Append (*rxState);
        }
    }
    //
    // This loop repeats until we've read all data available from "rx ready" inbound streams
    // and pushed any accumulated messages we can up to the zmq session.
    //

    while (!rx_ready_list.IsEmpty ()
           || (zmq_input_ready && !msg_ready_list.IsEmpty ())) {
        //
        // Iterate through our rx_ready streams, reading data into the decoder
        // (This services incoming "rx ready" streams in a round-robin fashion)
        //

        NormRxStreamState::List::Iterator rxIterator (rx_ready_list);
        NormRxStreamState *rxState;

        while (NULL != (rxState = rxIterator.GetNextItem ())) {
            switch (rxState->Decode ()) {
                case 1: // msg completed
                    //
                    // Complete message decoded, move this stream to msg_ready_list
                    // to push the message up to the session below.  Note the stream
                    // will be returned to the "rx_ready_list" after that's done
                    //

                    rx_ready_list.Remove (*rxState);
                    msg_ready_list.Append (*rxState);
                    continue;

                case -1: // decoding error (shouldn't happen w/ NORM, but ...)
                    //
                    // We need to re-sync this stream (decoder buffer was reset)
                    //

                    rxState->SetSync (false);
                    break;

                default: // 0 - need more data
                    break;
            }

            //
            // Get more data from this stream
            //

            NormObjectHandle stream = rxState->GetStreamHandle ();

            //
            // First, make sure we're in sync ...
            //

            while (!rxState->InSync ()) {
                //
                // seek NORM message start
                //

                if (!NormStreamSeekMsgStart (stream)) {
                    //
                    // Need to wait for more data
                    //

                    break;
                }

                //
                // read message 'flag' byte to see if this it's a 'final' frame
                //

                char syncFlag;
                unsigned int numBytes = 1;

                if (!NormStreamRead (stream, &syncFlag, &numBytes)) {
                    //
                    // broken stream (can happen on late-joining subscriber)
                    //

                    continue;
                }

                if (0 == numBytes) {
                    //
                    // This probably shouldn't happen either since we found msg start
                    // Need to wait for more data
                    //

                    break;
                }

                if (0 == syncFlag) {
                    rxState->SetSync (true);
                }

                //
                // else keep seeking ...
                //
            } // end while(!rxState->InSync())

            if (!rxState->InSync ()) {
                //
                // Need more data for this stream, so remove from "rx ready"
                // list and iterate to next "rx ready" stream
                //

                rxState->SetRxReady (false);

                //
                // Move from rx_ready_list to rx_pending_list
                //

                rx_ready_list.Remove (*rxState);
                rx_pending_list.Append (*rxState);
                continue;
            }

            //
            // Now we're actually ready to read data from the NORM stream to the zmq_decoder
            // the underlying zmq_decoder->get_buffer() call sets how much is needed.
            //

            unsigned int numBytes = (unsigned int) rxState->GetBytesNeeded ();

            if (!NormStreamRead (stream, rxState->AccessBuffer (), &numBytes)) {
                //
                // broken NORM stream, so re-sync
                //

                rxState->Init (); // TBD - check result

                //
                // This will retry syncing, and getting data from this stream
                // since we don't increment the "it" iterator
                //

                continue;
            }
            rxState->IncrementBufferCount (numBytes);
            if (0 == numBytes) {
                //
                // All the data available has been read
                // Need to wait for NORM_RX_OBJECT_UPDATED for this stream
                //

                rxState->SetRxReady (false);

                //
                // Move from rx_ready_list to rx_pending_list
                //

                rx_ready_list.Remove (*rxState);
                rx_pending_list.Append (*rxState);
            }
        } // end while(NULL != (rxState = iterator.GetNextItem()))

        if (zmq_input_ready) {
            //
            // At this point, we've made a pass through the "rx_ready" stream list
            // Now make a pass through the "msg_pending" list (if the zmq session
            // ready for more input).  This may possibly return streams back to
            // the "rx ready" stream list after their pending message is handled
            //

            NormRxStreamState::List::Iterator readyIterator (msg_ready_list);
            NormRxStreamState *rxStateReady;

            while (NULL != (rxStateReady = readyIterator.GetNextItem ())) {
                msg_t *msg = rxStateReady->AccessMsg ();
                int rc = zmq_session->push_msg (msg);

                if (-1 == rc) {
                    if (EAGAIN == errno) {
                        //
                        // need to wait until session calls "restart_input()"
                        //

                        zmq_input_ready = false;
                        break;
                    } else {
                        //
                        // session rejected message?
                        // TBD - handle this better
                        //

                        zmq_assert (false);
                    }
                }

                //
                // else message was accepted.
                //

                msg_ready_list.Remove (*rxStateReady);

                if (
                  rxStateReady
                    ->IsRxReady ()) { // Move back to "rx_ready" list to read more data
                    rx_ready_list.Append (*rxStateReady);
                } else { // Move back to "rx_pending" list until NORM_RX_OBJECT_UPDATED
                    msg_ready_list.Append (*rxStateReady);
                }
            } // end while(NULL != (rxState = iterator.GetNextItem()))
        }     // end if (zmq_input_ready)
    } // end while ((!rx_ready_list.empty() || (zmq_input_ready && !msg_ready_list.empty()))

    //
    // Alert zmq of the messages we have pushed up
    //

    zmq_session->flush ();
#endif
}

#endif // ZMQ_HAVE_NORM
