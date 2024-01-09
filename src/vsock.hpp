/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_VSOCK_HPP_INCLUDED__
#define __ZMQ_VSOCK_HPP_INCLUDED__

#include <string>

#include "platform.hpp"
#include "fd.hpp"
#include "ctx.hpp"

#if defined ZMQ_HAVE_VSOCK

#if defined ZMQ_HAVE_WINDOWS
#include "windows.hpp"
#else
#include <sys/time.h>
#endif

namespace zmq
{
fd_t vsock_open_socket (const char *address_,
                        const options_t &options_,
                        vsock_address_t *out_vsock_addr_);
}

#endif

#endif
