/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "sctp_address.hpp"

#if defined(ZMQ_HAVE_SCTP)

#include <climits>
#include <string>
#include <sstream>

#include "err.hpp"

zmq::sctp_address_t::sctp_address_t ()
{
    memset (&address, 0, sizeof address);
}

zmq::sctp_address_t::sctp_address_t (ctx_t *parent_) : parent (parent_)
{
    memset (&address, 0, sizeof address);
}

zmq::sctp_address_t::sctp_address_t (const sockaddr *sa,
                                       socklen_t sa_len,
                                       ctx_t *parent_) :
    parent (parent_)
{
    zmq_assert (sa && sa_len > 0);

    memset (&address, 0, sizeof (address));

    if (sa->sa_family == parent->get_sctp_socket_family ()) {
        zmq_assert (sa_len <= sizeof (address));
        memcpy (&address, sa, sa_len);
    }
}

int zmq::sctp_address_t::resolve (const char *path_)
{
    memset (&address, 0, sizeof (address));

    //
    //  Find the ':' at end that separates address from the port number.
    //

    const char *delimiter = strrchr (path_, ':');

    if (!delimiter) {
        errno = EINVAL;
        return -1;
    }

    //
    //  Separate the address/port.
    //

    std::string addr_str (path_, delimiter - path_);
    std::string port_str (delimiter + 1);

    if (!addr_str.length ()) {
        //
        // Address cannot be empty.
        //

        errno = EINVAL;
        return -1;

    } else if (addr_str != "*") {
        //
        // Test for well-known aliases first.
        //

        if (addr_str == "any") {
        }
    }

    uint16_t port = 0;

    if (!port_str.length ()) {
        //
        // Port cannot be empty.
        //

        errno = EINVAL;
        return -1;

    } else if (port_str != "*") {
        //
        // Numeric interpretation.
        //

        char *end = NULL;
        const char *begin = port_str.c_str ();
        const unsigned long l = strtoul (begin, &end, 10);

        if ((l == 0 && end == begin) || (l == ULONG_MAX && errno == ERANGE)
            || l > USHRT_MAX) {
            errno = EINVAL;
            return -1;
        }

        port = static_cast<uint16_t> (l);
    }

    address.sconn_family =
      static_cast<unsigned short> (parent->get_sctp_socket_family ());

    address.sconn_port = port;

    address.sconn_addr = 0;

    return 0;
}

int zmq::sctp_address_t::to_string (std::string &addr_) const
{
    if (address.sconn_family != parent->get_sctp_socket_family ()) {
        addr_.clear ();
        return -1;
    }

    std::stringstream s;

    s << protocol_name::sctp << "://" << address.sconn_addr;

    if (address.sconn_port != 0) {
        s << ":" << address.sconn_port;
    }

    addr_ = s.str ();

    return 0;
}

const sockaddr *zmq::sctp_address_t::addr () const
{
    return reinterpret_cast<const sockaddr *> (&address);
}

socklen_t zmq::sctp_address_t::addrlen () const
{
    return static_cast<socklen_t> (sizeof address);
}

#if defined ZMQ_HAVE_WINDOWS
unsigned short zmq::sctp_address_t::family () const
#else
sa_family_t zmq::sctp_address_t::family () const
#endif
{
    return AF_CONN;
}

#endif
