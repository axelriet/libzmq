/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_SCTP_ADDRESS_HPP_INCLUDED__
#define __ZMQ_SCTP_ADDRESS_HPP_INCLUDED__

#include <string>

#include "platform.hpp"
#include "ctx.hpp"

#if defined(ZMQ_HAVE_SCTP)

#include "../external/usrsctp/usrsctplib/usrsctp.h"
#pragma comment (lib, "../external/usrsctp/usrsctplib/usrsctp.lib")

namespace zmq
{
class sctp_address_t
{
  public:
    sctp_address_t ();
    sctp_address_t (ctx_t *parent_);
    sctp_address_t (const sockaddr *sa, socklen_t sa_len, ctx_t *parent_);

    //  This function sets up the address for VSOCK transport.
    int resolve (const char *path_);

    //  The opposite to resolve()
    int to_string (std::string &addr_) const;

#if defined ZMQ_HAVE_WINDOWS
    unsigned short family () const;
#else
    sa_family_t family () const;
#endif
    const sockaddr *addr () const;
    socklen_t addrlen () const;

  private:
    struct sockaddr_conn address;
    ctx_t *parent;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (sctp_address_t)
};
}

#endif

#endif
