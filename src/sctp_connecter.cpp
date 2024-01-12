/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include <new>
#include <string>

#include "macros.hpp"
#include "sctp_connecter.hpp"
#include "io_thread.hpp"
#include "err.hpp"
#include "ip.hpp"
#include "sctp.hpp"
#include "address.hpp"
#include "sctp_address.hpp"
#include "session_base.hpp"

#if !defined ZMQ_HAVE_WINDOWS
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#ifdef ZMQ_HAVE_VXWORKS
#include <sockLib.h>
#endif
#ifdef ZMQ_HAVE_OPENVMS
#include <ioctl.h>
#endif
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

zmq::sctp_connecter_t::sctp_connecter_t (class io_thread_t *io_thread_,
                                       class session_base_t *session_,
                                       const options_t &options_,
                                       address_t *addr_,
                                       bool delayed_start_) :
    stream_connecter_base_t (
      io_thread_, session_, options_, addr_, delayed_start_),
    _connect_timer_started (false)
{
    zmq_assert (_addr->protocol == protocol_name::tcp);
}

zmq::sctp_connecter_t::~sctp_connecter_t ()
{
    zmq_assert (!_connect_timer_started);
}

void zmq::sctp_connecter_t::process_term (int linger_)
{
    if (_connect_timer_started) {
        cancel_timer (connect_timer_id);
        _connect_timer_started = false;
    }

    stream_connecter_base_t::process_term (linger_);
}

void zmq::sctp_connecter_t::out_event ()
{
    if (_connect_timer_started) {
        cancel_timer (connect_timer_id);
        _connect_timer_started = false;
    }

    //  TODO this is still very similar to (t)ipc_connecter_t, maybe the
    //  differences can be factored out

    rm_handle ();

    const fd_t fd = connect ();

    if (fd == retired_fd
        && ((options.reconnect_stop & ZMQ_RECONNECT_STOP_CONN_REFUSED)
            && errno == ECONNREFUSED)) {
        send_conn_failed (_session);
        close ();
        terminate ();
        return;
    }

    //  Handle the error condition by attempt to reconnect.
    if (fd == retired_fd || !tune_socket (fd)) {
        close ();
        add_reconnect_timer ();
        return;
    }

    create_engine (fd, get_socket_name<sctp_address_t> (fd, socket_end_local));
}

void zmq::sctp_connecter_t::timer_event (int id_)
{
    if (id_ == connect_timer_id) {
        _connect_timer_started = false;
        rm_handle ();
        close ();
        add_reconnect_timer ();
    } else
        stream_connecter_base_t::timer_event (id_);
}

void zmq::sctp_connecter_t::start_connecting ()
{
    //  Open the connecting socket.
    const int rc = open ();

    //  Connect may succeed in synchronous manner.
    if (rc == 0) {
        _handle = add_fd (_s);
        out_event ();
    }

    //  Connection establishment may be delayed. Poll for its completion.
    else if (rc == -1 && errno == EINPROGRESS) {
        _handle = add_fd (_s);
        set_pollout (_handle);
        _socket->event_connect_delayed (
          make_unconnected_connect_endpoint_pair (_endpoint), zmq_errno ());

        //  add userspace connect timeout
        add_connect_timer ();
    }

    //  Handle any other error condition by eventual reconnect.
    else {
        if (_s != retired_fd)
            close ();
        add_reconnect_timer ();
    }
}

void zmq::sctp_connecter_t::add_connect_timer ()
{
    if (options.connect_timeout > 0) {
        add_timer (options.connect_timeout, connect_timer_id);
        _connect_timer_started = true;
    }
}

int zmq::sctp_connecter_t::open ()
{
    zmq_assert (_s == retired_fd);

    //  Resolve the address
    if (_addr->resolved.sctp_addr != NULL) {
        LIBZMQ_DELETE (_addr->resolved.sctp_addr);
    }

    _addr->resolved.sctp_addr = new (std::nothrow) sctp_address_t ();
    alloc_assert (_addr->resolved.sctp_addr);
    _s = sctp_open_socket (_addr->address.c_str (), options, false, true,
                          _addr->resolved.sctp_addr);
    if (_s == retired_fd) {
        //  TODO we should emit some event in this case!

        LIBZMQ_DELETE (_addr->resolved.sctp_addr);
        return -1;
    }
    zmq_assert (_addr->resolved.sctp_addr != NULL);

    // Set the socket to non-blocking mode so that we get async connect().
    unblock_socket (_s);

    const sctp_address_t *const sctp_addr = _addr->resolved.sctp_addr;

    int rc;

    // Set a source address for conversations
    if (sctp_addr->has_src_addr ()) {
        //  Allow reusing of the address, to connect to different servers
        //  using the same source port on the client.
        int flag = 1;
        rc = usrsctp_setsockopt ((struct socket *) _s, SOL_SOCKET, SO_REUSEADDR,
                                 &flag, sizeof (int));
        errno_assert (rc == 0);

        rc = usrsctp_bind ((struct socket *) _s,
                           (struct sockaddr *) sctp_addr->src_addr (),
                           sctp_addr->src_addrlen ());

        if (rc == -1) {
            return -1;
        }
    }

    //  Connect to the remote peer.
    rc = usrsctp_connect ((struct socket *) _s,
                          (struct sockaddr *) sctp_addr->addr (),
                          sctp_addr->addrlen ());

    //  Connect was successful immediately.

    if (rc == 0) {
        return 0;
    }

    //  Translate error codes indicating asynchronous connect has been
    //  launched to a uniform EINPROGRESS.

    if (errno == EINTR) {
        errno = EINPROGRESS;
    }

    return -1;
}

zmq::fd_t zmq::sctp_connecter_t::connect ()
{
    int err = 0;

#if defined ZMQ_HAVE_HPUX || defined ZMQ_HAVE_VXWORKS
    int len = sizeof err;
#else
    socklen_t len = sizeof err;
#endif

    const int rc =
      usrsctp_getsockopt ((struct socket *) _s, SOL_SOCKET, SO_ERROR,
                               reinterpret_cast<char *> (&err), &len);

    if (rc == -1) {
        err = errno;
    }

    if (err != 0) {
        errno = err;
        errno_assert (errno != ENOPROTOOPT && errno != ENOTSOCK
                      && errno != ENOBUFS);
        return retired_fd;
    }

    //  Return the newly connected socket.

    const fd_t result = _s;
    _s = retired_fd;

    return result;
}

bool zmq::sctp_connecter_t::tune_socket (const fd_t /*fd_*/)
{
#if 0
    const int rc = tune_tcp_socket (fd_)
                   | tune_tcp_keepalives (
                     fd_, options.tcp_keepalive, options.tcp_keepalive_cnt,
                     options.tcp_keepalive_idle, options.tcp_keepalive_intvl)
                   | tune_tcp_maxrt (fd_, options.tcp_maxrt);
    return rc == 0;
#endif
    return true;
}
