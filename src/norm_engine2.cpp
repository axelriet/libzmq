#include "precompiled.hpp"

#include "platform.hpp"

#if defined ZMQ_HAVE_NORM

#include "norm_engine.hpp"
#ifdef ZMQ_USE_NORM_SOCKET_WRAPPER
#include "ip.hpp"
#endif

#include "session_base.hpp"
#include "v2_protocol.hpp"

void zmq::norm_engine2_t::send_data ()
{
    //
    // Here we write as much as is available or we can
    //

    while (zmq_output_ready && norm_tx_ready) {
        if (0 == tx_len) {
            //
            // Our tx_buffer needs data to send
            // Get more data from encoder
            //

            size_t space = BUFFER_SIZE;
            unsigned char *bufPtr = (unsigned char *) tx_buffer;
            tx_len = (unsigned int) zmq_encoder.encode (&bufPtr, space);

            if (0 == tx_len) {
                if (!tx_first_msg) {
                    //
                    // We don't need to mark eom/flush until a message is sent
                    //
                    //
                    // A prior message was completely written to stream, so
                    // mark end-of-message and possibly flush (to force packet transmission,
                    // even if it's not a full segment so message gets delivered quickly)
                    // NormStreamMarkEom(norm_tx_stream);  // the flush below marks eom
                    // Note NORM_FLUSH_ACTIVE makes NORM fairly chatty for low duty cycle messaging
                    // but makes sure content is delivered quickly.  Positive acknowledgements
                    // with flush override would make NORM more succinct here
                    //

                    NormStreamFlush (norm_tx_stream, true, NORM_FLUSH_ACTIVE);
                }

                //
                // Need to pull and load a new message to send
                //

                if (-1 == zmq_session->pull_msg (&tx_msg)) {
                    //
                    // We need to wait for "restart_output()" to be called by ZMQ
                    //

                    zmq_output_ready = false;
                    break;
                }

                if (tx_first_msg) {
                    tx_first_msg = false;
                }

                zmq_encoder.load_msg (&tx_msg);

                //
                // Should we write message size header for NORM to use? Or expect NORM
                // receiver to decode ZMQ message framing format(s)?
                // OK - we need to use a byte to denote when the ZMQ frame is the _first_
                //      frame of a message so it can be decoded properly when a receiver
                //      'syncs' mid-stream.  We key off the the state of the 'more_flag'
                //      I.e.,If  more_flag _was_ false previously, this is the first
                //      frame of a ZMQ message.
                //

                if (tx_more_bit) {
                    tx_buffer[0] =
                      (char) -1; // this is not first frame of message
                } else {
                    tx_buffer[0] = 0x00; // this is first frame of message
                }

                tx_more_bit = (0 != (tx_msg.flags () & msg_t::more));

                //
                // Go ahead an get a first chunk of the message
                //

                bufPtr++;
                space--;
                tx_len = 1 + (unsigned int) zmq_encoder.encode (&bufPtr, space);
                tx_index = 0;
            }
        }

        //
        // Do we have data in our tx_buffer pending
        //

        if (tx_index < tx_len) {
            //
            // We have data in our tx_buffer to send, so write it to the stream
            //

            tx_index += NormStreamWrite (norm_tx_stream, tx_buffer + tx_index,
                                         tx_len - tx_index);

            if (tx_index < tx_len) {
                //
                // NORM stream buffer full, wait for NORM_TX_QUEUE_VACANCY
                //

                norm_tx_ready = false;
                break;
            }
            tx_len = 0; // all buffered data was written
        }
    } // end while (zmq_output_ready && norm_tx_ready)
} // end zmq::norm_engine2_t::send_data()

void zmq::norm_engine2_t::recv_data (NormObjectHandle object)
{
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
} // end zmq::norm_engine2_t::recv_data()

#endif // ZMQ_HAVE_NORM
