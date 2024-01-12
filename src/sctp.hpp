/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_SCTP_HPP_INCLUDED__
#define __ZMQ_SCTP_HPP_INCLUDED__

#include "fd.hpp"
#include "sctp_address.hpp"

namespace zmq
{
class sctp_address_t;
struct options_t;

#if 0
//  Tunes the supplied SCTP socket for the best latency.
int tune_sctp_socket (fd_t s_);

//  Sets the socket send buffer size.
int set_sctp_send_buffer (fd_t sockfd_, int bufsize_);

//  Sets the socket receive buffer size.
int set_sctp_receive_buffer (fd_t sockfd_, int bufsize_);

//  Tunes SCTP keep-alives
int tune_sctp_keepalives (fd_t s_,
                         int keepalive_,
                         int keepalive_cnt_,
                         int keepalive_idle_,
                         int keepalive_intvl_);

//  Tunes SCTP max retransmit timeout
int sctp_tcp_maxrt (fd_t sockfd_, int timeout_);
#endif

//  Writes data to the socket. Returns the number of bytes actually
//  written (even zero is to be considered to be a success). In case
//  of error or orderly shutdown by the other peer -1 is returned.
int sctp_write (fd_t s_, const void *data_, size_t size_);

//  Reads data from the socket (up to 'size' bytes).
//  Returns the number of bytes actually read or -1 on error.
//  Zero indicates the peer has closed the connection.
int sctp_read (fd_t s_, void *data_, size_t size_);

void sctp_tune_loopback_fast_path (fd_t socket_);

#if 0
void sctp_tcp_busy_poll (fd_t socket_, int busy_poll_);
#endif

//  Resolves the given address_ string, opens a socket and sets socket options
//  according to the passed options_. On success, returns the socket
//  descriptor and assigns the resolved address to out_tcp_addr_. In case of
//  an error, retired_fd is returned, and the value of out_tcp_addr_ is undefined.
//  errno is set to an error code describing the cause of the error.
fd_t sctp_open_socket (const char *address_,
                      const options_t &options_,
                      bool local_,
                      bool fallback_to_ipv4_,
                      sctp_address_t *out_tcp_addr_);
}

#endif
