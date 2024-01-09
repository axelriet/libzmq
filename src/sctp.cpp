/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "ip.hpp"
#include "sctp.hpp"
#include "sctp_address.hpp"

#if defined ZMQ_HAVE_SCTP

zmq::fd_t zmq::sctp_open_socket (const char *address_,
                                 const zmq::options_t &options_,
                                 zmq::sctp_address_t *out_sctp_addr_)
{
    LIBZMQ_UNUSED (options_);

    //
    //  Convert the textual address into address structure.
    //

    const int rc = out_sctp_addr_->resolve (address_);

    if (rc != 0) {
        return retired_fd;
    }

    //
    //  Create the socket.
    //

    const fd_t s = (zmq_fd_t) usrsctp_socket (
      out_sctp_addr_->family (), SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, 0);

    if (s == 0) {
        return retired_fd;
    }

    return s;
}

#endif