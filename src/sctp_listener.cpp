/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include <new>

#include <string>
#include <stdio.h>

#include "sctp_address.hpp"
#include "sctp_listener.hpp"
#include "io_thread.hpp"
#include "config.hpp"
#include "err.hpp"
#include "ip.hpp"
#include "sctp.hpp"
#include "socket_base.hpp"
#include "address.hpp"

#ifndef ZMQ_HAVE_WINDOWS
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#ifdef ZMQ_HAVE_VXWORKS
#include <sockLib.h>
#endif
#endif

#ifdef ZMQ_HAVE_OPENVMS
#include <ioctl.h>
#endif

zmq::sctp_listener_t::sctp_listener_t (io_thread_t *io_thread_,
                                     socket_base_t *socket_,
                                     const options_t &options_) :
    stream_listener_base_t (io_thread_, socket_, options_)
{
}

void zmq::sctp_listener_t::in_event ()
{
    const fd_t fd = accept ();

    //  If connection was reset by the peer in the meantime, just ignore it.
    //  TODO: Handle specific errors like ENFILE/EMFILE etc.
    if (fd == retired_fd) {
        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), zmq_errno ());
        return;
    }

#if 0
    int rc = tune_tcp_socket (fd);
    rc = rc
         | tune_tcp_keepalives (
           fd, options.tcp_keepalive, options.tcp_keepalive_cnt,
           options.tcp_keepalive_idle, options.tcp_keepalive_intvl);
    rc = rc | tune_tcp_maxrt (fd, options.tcp_maxrt);
    if (rc != 0) {
        _socket->event_accept_failed (
          make_unconnected_bind_endpoint_pair (_endpoint), zmq_errno ());
        return;
    }
#endif

    //  Create the engine object for this connection.
    create_engine (fd);
}

std::string
zmq::sctp_listener_t::get_socket_name (zmq::fd_t fd_,
                                      socket_end_t socket_end_) const
{
    return zmq::get_socket_name<tcp_address_t> (fd_, socket_end_);
}

int zmq::sctp_listener_t::create_socket (const char *addr_)
{
    _s = sctp_open_socket (addr_, options, true, true, &_address);

    if (_s == retired_fd) {
        return -1;
    }

    //  Allow reusing of the address.
    int flag = 1;
    int rc;

#if 0
    rc =
      usrsctp_setsockopt (_s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     reinterpret_cast<const char *> (&flag), sizeof (int));
    wsa_assert (rc != SOCKET_ERROR);
#endif

    //  Bind the socket to the network interface and port.
    rc =
      usrsctp_bind ((struct socket *) _s, (struct sockaddr *) _address.addr (),
                    _address.addrlen ());

    if (rc != 0) {
        goto error;
    }

    //  Listen for incoming connections.
    rc = usrsctp_listen ((struct socket *) _s, options.backlog);

    if (rc != 0) {
        goto error;
    }

    return 0;

error:
    const int err = errno;
    close ();
    errno = err;
    return -1;
}

int zmq::sctp_listener_t::set_local_address (const char *addr_)
{
    if (options.use_fd != -1) {
        //  in this case, the addr_ passed is not used and ignored, since the
        //  socket was already created by the application
        _s = options.use_fd;
    } else {
        if (create_socket (addr_) == -1)
            return -1;
    }

    _endpoint = get_socket_name (_s, socket_end_local);

    _socket->event_listening (make_unconnected_bind_endpoint_pair (_endpoint),
                              _s);
    return 0;
}

zmq::fd_t zmq::sctp_listener_t::accept ()
{
    //  The situation where connection cannot be accepted due to insufficient
    //  resources is considered valid and treated by ignoring the connection.
    //  Accept one connection and deal with different failure modes.
    zmq_assert (_s != retired_fd);

    sctp_sockstore ss;
    memset (&ss, 0, sizeof (ss));
    socklen_t ss_len = sizeof (ss);

    const fd_t sock = (fd_t) usrsctp_accept (
      (struct socket *) _s, reinterpret_cast<struct sockaddr *> (&ss), &ss_len);

    if (sock == retired_fd) {

        int err_no = errno;

        errno_assert (err_no == EAGAIN || err_no == EWOULDBLOCK || err_no == EINTR
                      || err_no == ECONNABORTED || err_no == EPROTO
                      || err_no == ENOBUFS || err_no == ENOMEM || err_no == EMFILE
                      || err_no == ENFILE);

        return retired_fd;
    }

    make_socket_noninheritable (sock);

    if (!options.tcp_accept_filters.empty ()) {
        bool matched = false;
        for (options_t::tcp_accept_filters_t::size_type
               i = 0,
               size = options.tcp_accept_filters.size ();
             i != size; ++i) {
            if (options.tcp_accept_filters[i].match_address (
                  reinterpret_cast<struct sockaddr *> (&ss), ss_len)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            usrsctp_close ((struct socket*) sock);
            return retired_fd;
        }
    }

    if (zmq::set_nosigpipe (sock)) {
        usrsctp_close ((struct socket *) sock);
        return retired_fd;
    }

#if 0
    // Set the IP Type-Of-Service priority for this client socket
    if (options.tos != 0)
        set_ip_type_of_service (sock, options.tos);

    // Set the protocol-defined priority for this client socket
    if (options.priority != 0)
        set_socket_priority (sock, options.priority);
#endif

    return sock;
}
