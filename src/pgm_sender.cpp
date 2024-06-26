/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#if defined ZMQ_HAVE_OPENPGM

#include <stdlib.h>

#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
#include <tbb/scalable_allocator.h>
#endif

#include "io_thread.hpp"
#include "pgm_sender.hpp"
#include "session_base.hpp"
#include "err.hpp"
#include "wire.hpp"
#include "stdint.hpp"
#include "macros.hpp"

zmq::pgm_sender_t::pgm_sender_t (io_thread_t *parent_,
                                 const options_t &options_) :
    io_object_t (parent_),
    has_tx_timer (false),
    has_rx_timer (false),
    session (NULL),
    encoder (0),
    more (false),
    pgm_socket (false, options_),
    options (options_),
    handle (static_cast<handle_t> (NULL)),
    uplink_handle (static_cast<handle_t> (NULL)),
    rdata_notify_handle (static_cast<handle_t> (NULL)),
    pending_notify_handle (static_cast<handle_t> (NULL)),
    out_buffer (NULL),
    _out_batch_size (0),
    write_size (0)
{
    int rc = msg.init ();
    errno_assert (rc == 0);
}

int zmq::pgm_sender_t::init (bool udp_encapsulation_, const char *network_)
{
    const int rc = pgm_socket.init (udp_encapsulation_, network_);

    if (rc != 0) {
        return rc;
    }

    _out_batch_size = pgm_socket.get_max_tsdu_size ();

#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
    out_buffer = (unsigned char *) scalable_malloc (_out_batch_size);
#else
    out_buffer = (unsigned char *) std::malloc (_out_batch_size);
#endif

    alloc_assert (out_buffer);

    return rc;
}

void zmq::pgm_sender_t::plug (io_thread_t *io_thread_, session_base_t *session_)
{
    LIBZMQ_UNUSED (io_thread_);
    //  Allocate 2 fds for PGM socket.
    fd_t downlink_socket_fd = retired_fd;
    fd_t uplink_socket_fd = retired_fd;
    fd_t rdata_notify_fd = retired_fd;
    fd_t pending_notify_fd = retired_fd;

    session = session_;

    //  Fill fds from PGM transport and add them to the poller.
    pgm_socket.get_sender_fds (&downlink_socket_fd, &uplink_socket_fd,
                               &rdata_notify_fd, &pending_notify_fd);

    handle = add_fd (downlink_socket_fd);
    uplink_handle = add_fd (uplink_socket_fd);
    rdata_notify_handle = add_fd (rdata_notify_fd);
    pending_notify_handle = add_fd (pending_notify_fd);

    //  Set POLLIN. We will never want to stop polling for uplink = we never
    //  want to stop processing NAKs.
    set_pollin (uplink_handle);
    set_pollin (rdata_notify_handle);
    set_pollin (pending_notify_handle);

    //  Set POLLOUT for downlink_socket_handle.
    set_pollout (handle);
}

void zmq::pgm_sender_t::unplug ()
{
    if (has_rx_timer) {
        cancel_timer (rx_timer_id);
        has_rx_timer = false;
    }

    if (has_tx_timer) {
        cancel_timer (tx_timer_id);
        has_tx_timer = false;
    }

    rm_fd (handle);
    rm_fd (uplink_handle);
    rm_fd (rdata_notify_handle);
    rm_fd (pending_notify_handle);
    session = NULL;
}

void zmq::pgm_sender_t::terminate ()
{
    unplug ();
    delete this;
}

void zmq::pgm_sender_t::restart_output ()
{
    set_pollout (handle);
    out_event ();
}

bool zmq::pgm_sender_t::restart_input ()
{
    zmq_assert (false);
    return true;
}

const zmq::endpoint_uri_pair_t &zmq::pgm_sender_t::get_endpoint () const
{
    return _empty_endpoint;
}

zmq::pgm_sender_t::~pgm_sender_t ()
{
    int rc = msg.close ();
    errno_assert (rc == 0);

    if (out_buffer) {
#ifdef ZMQ_HAVE_TBB_SCALABLE_ALLOCATOR
        scalable_free (out_buffer);
#else
        std::free (out_buffer);
#endif
        out_buffer = NULL;
    }
}

void zmq::pgm_sender_t::in_event ()
{
    if (has_rx_timer) {
        cancel_timer (rx_timer_id);
        has_rx_timer = false;
    }

    //  In-event on sender side means NAK or SPMR receiving from some peer.
    pgm_socket.process_upstream ();
    if (errno == ENOMEM || errno == EBUSY) {
        const long timeout = pgm_socket.get_rx_timeout ();
        if (timeout >= 0) {
            add_timer (timeout, rx_timer_id);
            has_rx_timer = true;
        }
    }
}

void zmq::pgm_sender_t::out_event ()
{
    //  POLLOUT event from send socket. If write buffer is empty,
    //  try to read new data from the encoder.

    if (write_size == 0) {
        //  First two bytes (sizeof uint16_t) are used to store message
        //  offset in following steps. Note that by passing our buffer to
        //  the get data function we prevent it from returning its own buffer.

#if defined(ZMQ_GREEDY_MSG_CLUBBING)
    check_for_more:
#endif

        unsigned char *bf = out_buffer + sizeof (uint16_t);
        size_t bfsz = _out_batch_size - sizeof (uint16_t);
        uint16_t offset = 0xffff;

        size_t bytes = encoder.encode (&bf, bfsz);
        while (bytes < bfsz) {
            if (!more && offset == 0xffff)
                offset = static_cast<uint16_t> (bytes);
            int rc = session->pull_msg (&msg);
            if (rc == -1)
                break;
            more = msg.flagsp () & msg_t::more;
            encoder.load_msg (&msg);
            bf = out_buffer + sizeof (uint16_t) + bytes;
            bytes += encoder.encode (&bf, bfsz - bytes);
        }

        //  If there are no data to write stop polling for output.
        if (bytes == 0) {
            reset_pollout (handle);
            return;
        }

        write_size = sizeof (uint16_t) + bytes;

        //  Put offset information in the buffer.
        put_uint16 (out_buffer, offset);
    }

    if (has_tx_timer) {
        cancel_timer (tx_timer_id);
        set_pollout (handle);
        has_tx_timer = false;
    }

    //  Send the data.
    const size_t nbytes = pgm_socket.send (out_buffer, write_size);

    //  We can write either all data or 0 which means rate limit reached.
    if (nbytes == write_size) {
        write_size = 0;
#if defined(ZMQ_GREEDY_MSG_CLUBBING)
        goto check_for_more;
#endif
    } else {
        zmq_assert (nbytes == 0);

        if (errno == ENOMEM) {
            // Stop polling handle and wait for tx timeout
            const long timeout = pgm_socket.get_tx_timeout ();
            if (timeout >= 0) {
                add_timer (timeout, tx_timer_id);
                reset_pollout (handle);
                has_tx_timer = true;
            }
        } else
            errno_assert (errno == EBUSY);
    }
}

void zmq::pgm_sender_t::timer_event (int token)
{
    //  Timer cancels on return by poller_base.
    if (token == rx_timer_id) {
        has_rx_timer = false;
        in_event ();
    } else if (token == tx_timer_id) {
        // Restart polling handle and retry sending
        has_tx_timer = false;
        set_pollout (handle);
        out_event ();
    } else
        zmq_assert (false);
}

#endif
