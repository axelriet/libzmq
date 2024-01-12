/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "macros.hpp"
#include "ip.hpp"
#include "sctp.hpp"
#include "err.hpp"
#include "options.hpp"

#if !defined ZMQ_HAVE_WINDOWS
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#ifdef ZMQ_HAVE_VXWORKS
#include <sockLib.h>
#endif
#endif

#if defined ZMQ_HAVE_OPENVMS
#include <ioctl.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if 0

int zmq::tune_tcp_socket (fd_t s_)
{
    //  Disable Nagle's algorithm. We are doing data batching on 0MQ level,
    //  so using Nagle wouldn't improve throughput in anyway, but it would
    //  hurt latency.
    int nodelay = 1;
    const int rc =
      setsockopt (s_, IPPROTO_TCP, TCP_NODELAY,
                  reinterpret_cast<char *> (&nodelay), sizeof (int));
    assert_success_or_recoverable (s_, rc);
    if (rc != 0)
        return rc;

#ifdef ZMQ_HAVE_OPENVMS
    //  Disable delayed acknowledgements as they hurt latency significantly.
    int nodelack = 1;
    rc = setsockopt (s_, IPPROTO_TCP, TCP_NODELACK, (char *) &nodelack,
                     sizeof (int));
    assert_success_or_recoverable (s_, rc);
#endif
    return rc;
}

int zmq::set_tcp_send_buffer (fd_t sockfd_, int bufsize_)
{
    const int rc =
      setsockopt (sockfd_, SOL_SOCKET, SO_SNDBUF,
                  reinterpret_cast<char *> (&bufsize_), sizeof bufsize_);
    assert_success_or_recoverable (sockfd_, rc);
    return rc;
}

int zmq::set_tcp_receive_buffer (fd_t sockfd_, int bufsize_)
{
    const int rc =
      setsockopt (sockfd_, SOL_SOCKET, SO_RCVBUF,
                  reinterpret_cast<char *> (&bufsize_), sizeof bufsize_);
    assert_success_or_recoverable (sockfd_, rc);
    return rc;
}

int zmq::tune_tcp_keepalives (fd_t s_,
                              int keepalive_,
                              int keepalive_cnt_,
                              int keepalive_idle_,
                              int keepalive_intvl_)
{
    // These options are used only under certain #ifdefs below.
    LIBZMQ_UNUSED (keepalive_);
    LIBZMQ_UNUSED (keepalive_cnt_);
    LIBZMQ_UNUSED (keepalive_idle_);
    LIBZMQ_UNUSED (keepalive_intvl_);

    // If none of the #ifdefs apply, then s_ is unused.
    LIBZMQ_UNUSED (s_);

    //  Tuning TCP keep-alives if platform allows it
    //  All values = -1 means skip and leave it for OS
#ifdef ZMQ_HAVE_WINDOWS
    if (keepalive_ != -1) {
        tcp_keepalive keepalive_opts;
        keepalive_opts.onoff = keepalive_;
        keepalive_opts.keepalivetime =
          keepalive_idle_ != -1 ? keepalive_idle_ * 1000 : 7200000;
        keepalive_opts.keepaliveinterval =
          keepalive_intvl_ != -1 ? keepalive_intvl_ * 1000 : 1000;
        DWORD num_bytes_returned;
        const int rc = WSAIoctl (s_, SIO_KEEPALIVE_VALS, &keepalive_opts,
                                 sizeof (keepalive_opts), NULL, 0,
                                 &num_bytes_returned, NULL, NULL);
        assert_success_or_recoverable (s_, rc);
        if (rc == SOCKET_ERROR)
            return rc;
    }
#else
#ifdef ZMQ_HAVE_SO_KEEPALIVE
    if (keepalive_ != -1) {
        int rc =
          setsockopt (s_, SOL_SOCKET, SO_KEEPALIVE,
                      reinterpret_cast<char *> (&keepalive_), sizeof (int));
        assert_success_or_recoverable (s_, rc);
        if (rc != 0)
            return rc;

#ifdef ZMQ_HAVE_TCP_KEEPCNT
        if (keepalive_cnt_ != -1) {
            int rc = setsockopt (s_, IPPROTO_TCP, TCP_KEEPCNT, &keepalive_cnt_,
                                 sizeof (int));
            assert_success_or_recoverable (s_, rc);
            if (rc != 0)
                return rc;
        }
#endif // ZMQ_HAVE_TCP_KEEPCNT

#ifdef ZMQ_HAVE_TCP_KEEPIDLE
        if (keepalive_idle_ != -1) {
            int rc = setsockopt (s_, IPPROTO_TCP, TCP_KEEPIDLE,
                                 &keepalive_idle_, sizeof (int));
            assert_success_or_recoverable (s_, rc);
            if (rc != 0)
                return rc;
        }
#else // ZMQ_HAVE_TCP_KEEPIDLE
#ifdef ZMQ_HAVE_TCP_KEEPALIVE
        if (keepalive_idle_ != -1) {
            int rc = setsockopt (s_, IPPROTO_TCP, TCP_KEEPALIVE,
                                 &keepalive_idle_, sizeof (int));
            assert_success_or_recoverable (s_, rc);
            if (rc != 0)
                return rc;
        }
#endif // ZMQ_HAVE_TCP_KEEPALIVE
#endif // ZMQ_HAVE_TCP_KEEPIDLE

#ifdef ZMQ_HAVE_TCP_KEEPINTVL
        if (keepalive_intvl_ != -1) {
            int rc = setsockopt (s_, IPPROTO_TCP, TCP_KEEPINTVL,
                                 &keepalive_intvl_, sizeof (int));
            assert_success_or_recoverable (s_, rc);
            if (rc != 0)
                return rc;
        }
#endif // ZMQ_HAVE_TCP_KEEPINTVL
    }
#endif // ZMQ_HAVE_SO_KEEPALIVE
#endif // ZMQ_HAVE_WINDOWS

    return 0;
}

int zmq::tune_tcp_maxrt (fd_t sockfd_, int timeout_)
{
    if (timeout_ <= 0)
        return 0;

    LIBZMQ_UNUSED (sockfd_);

#if defined(ZMQ_HAVE_WINDOWS) && defined(TCP_MAXRT)
    // msdn says it's supported in >= Vista, >= Windows Server 2003
    timeout_ /= 1000; // in seconds
    const int rc =
      setsockopt (sockfd_, IPPROTO_TCP, TCP_MAXRT,
                  reinterpret_cast<char *> (&timeout_), sizeof (timeout_));
    assert_success_or_recoverable (sockfd_, rc);
    return rc;
// FIXME: should be ZMQ_HAVE_TCP_USER_TIMEOUT
#elif defined(TCP_USER_TIMEOUT)
    int rc = setsockopt (sockfd_, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout_,
                         sizeof (timeout_));
    assert_success_or_recoverable (sockfd_, rc);
    return rc;
#else
    return 0;
#endif
}

#endif

int zmq::sctp_write (fd_t s_, const void *data_, size_t size_)
{
    ssize_t nbytes =
      usrsctp_sendv ((struct socket *) s_, static_cast<const char *> (data_),
                     size_, NULL, 0, NULL, 0, 0, 0);

    //  Several errors are OK. When speculative write is being done we may not
    //  be able to write a single byte from the socket. Also, SIGSTOP issued
    //  by a debugging tool can result in EINTR error.
    if (nbytes == -1
        && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return 0;

    //  Report peer failure.
    if (nbytes == -1) {
        errno_assert (errno != EACCES && errno != EDESTADDRREQ
                      && errno != EFAULT && errno != EISCONN
                      && errno != EMSGSIZE && errno != ENOMEM
                      && errno != ENOTSOCK && errno != EOPNOTSUPP);
        return -1;
    }

    return static_cast<int> (nbytes);
}

int zmq::sctp_read (fd_t s_, void *data_, size_t size_)
{
    const ssize_t rc = usrsctp_recvv ((struct socket *) s_,
                                     static_cast<char *> (data_), size_, NULL, NULL, NULL, NULL, NULL, NULL);

    //  Several errors are OK. When speculative read is being done we may not
    //  be able to read a single byte from the socket. Also, SIGSTOP issued
    //  by a debugging tool can result in EINTR error.
    if (rc == -1) {
        errno_assert (errno != EFAULT && errno != ENOMEM && errno != ENOTSOCK);
        if (errno == EWOULDBLOCK || errno == EINTR)
            errno = EAGAIN;
    }

    return static_cast<int> (rc);
}


void zmq::sctp_tune_loopback_fast_path (const fd_t socket_)
{
#if defined ZMQ_HAVE_WINDOWS && defined SIO_LOOPBACK_FAST_PATH
    int sio_loopback_fastpath = 1;
    DWORD number_of_bytes_returned = 0;

    const int rc = WSAIoctl (
      socket_, SIO_LOOPBACK_FAST_PATH, &sio_loopback_fastpath,
      sizeof sio_loopback_fastpath, NULL, 0, &number_of_bytes_returned, 0, 0);

    if (SOCKET_ERROR == rc) {
        const DWORD last_error = ::WSAGetLastError ();

        if (WSAEOPNOTSUPP == last_error) {
            // This system is not Windows 8 or Server 2012, and the call is not supported.
        } else {
            wsa_assert (false);
        }
    }
#else
    LIBZMQ_UNUSED (socket_);
#endif
}

#if 0

void zmq::tune_tcp_busy_poll (fd_t socket_, int busy_poll_)
{
#if defined(ZMQ_HAVE_BUSY_POLL)
    if (busy_poll_ > 0) {
        const int rc =
          setsockopt (socket_, SOL_SOCKET, SO_BUSY_POLL,
                      reinterpret_cast<char *> (&busy_poll_), sizeof (int));
        assert_success_or_recoverable (socket_, rc);
    }
#else
    LIBZMQ_UNUSED (socket_);
    LIBZMQ_UNUSED (busy_poll_);
#endif
}

#endif

zmq::fd_t zmq::sctp_open_socket (const char *address_,
                                const zmq::options_t &options_,
                                bool local_,
                                bool fallback_to_ipv4_,
                                zmq::sctp_address_t *out_tcp_addr_)
{
    //  Convert the textual address into address structure.
    int rc = out_tcp_addr_->resolve (address_, local_, options_.ipv6);

    if (rc != 0) {
        return retired_fd;
    }

    //  Create the socket.
    fd_t s = open_socket (out_tcp_addr_->family (), SOCK_STREAM, IPPROTO_SCTP);

    //  IPv6 address family not supported, try automatic downgrade to IPv4.
    if (s == retired_fd && fallback_to_ipv4_
        && out_tcp_addr_->family () == AF_INET6 && errno == EAFNOSUPPORT
        && options_.ipv6) {
        rc = out_tcp_addr_->resolve (address_, local_, false);
        if (rc != 0) {
            return retired_fd;
        }
        s = open_socket (AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    }

    if (s == retired_fd) {
        return retired_fd;
    }

    //  On some systems, IPv4 mapping in IPv6 sockets is disabled by default.
    //  Switch it on in such cases.
    if (out_tcp_addr_->family () == AF_INET6)
        enable_ipv4_mapping (s);

    // Set the IP Type-Of-Service priority for this socket
    if (options_.tos != 0)
        set_ip_type_of_service (s, options_.tos);

    // Set the protocol-defined priority for this socket
    if (options_.priority != 0)
        set_socket_priority (s, options_.priority);

    // Set the socket to loopback fast path if configured.
    if (options_.loopback_fastpath)
        sctp_tune_loopback_fast_path (s);

    // Bind the socket to a device if applicable
    if (!options_.bound_device.empty ())
        if (bind_to_device (s, options_.bound_device) == -1)
            goto setsockopt_error;

#if 0
    //  Set the socket buffer limits for the underlying socket.
    if (options_.sndbuf >= 0)
        set_tcp_send_buffer (s, options_.sndbuf);
    if (options_.rcvbuf >= 0)
        set_tcp_receive_buffer (s, options_.rcvbuf);

    //  This option removes several delays caused by scheduling, interrupts and context switching.
    if (options_.busy_poll)
        tune_tcp_busy_poll (s, options_.busy_poll);
#endif

    return s;

setsockopt_error:
#ifdef ZMQ_HAVE_WINDOWS
    rc = closesocket (s);
    wsa_assert (rc != SOCKET_ERROR);
#else
    rc = ::close (s);
    errno_assert (rc == 0);
#endif
    return retired_fd;
}
