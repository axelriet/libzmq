/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __SCTP_SOCKET_HPP_INCLUDED__
#define __SCTP_SOCKET_HPP_INCLUDED__

#if defined ZMQ_HAVE_SCTP

#ifdef ZMQ_HAVE_WINDOWS
#define __SCTP_WININT_H__
#endif

//#include <sctp/sctp.h>

#if defined(ZMQ_HAVE_OSX) || defined(ZMQ_HAVE_NETBSD)
#include <sctp/in.h>
#endif

#include "fd.hpp"
#include "options.hpp"

namespace zmq
{
//  Encapsulates SCTP socket.
class sctp_t
{
  public:
    //  If receiver_ is true SCTP transport is not generating SPM packets.
//    sctp_t (bool receiver_, const options_t &options_);

    //  Closes the transport.
//    ~sctp_t ();

    //  Initialize SCTP network structures (GSI, GSRs).
    //int init (bool udp_encapsulation_, const char *network_);

    //  Resolve SCTP socket address.
    //static int init_address (const char *network_,
    //                         struct sctp_addrinfo_t **addr,
    //                         uint16_t *port_number);

    //   Get receiver fds and store them into user allocated memory.
//    void get_receiver_fds (fd_t *receive_fd_, fd_t *waiting_pipe_fd_);

    //   Get sender and receiver fds and store it to user allocated
    //   memory. Receive fd is used to process NAKs from peers.
  //  void get_sender_fds (fd_t *send_fd_,
  //                       fd_t *receive_fd_,
  //                       fd_t *rdata_notify_fd_,
  //                       fd_t *pending_notify_fd_);

    //  Send data as one APDU, transmit window owned memory.
  //  size_t send (unsigned char *data_, size_t data_len_);

    //  Returns max tsdu size without fragmentation.
//    size_t get_max_tsdu_size ();

    //  Receive data from sctp socket.
//    ssize_t receive (void **data_, const sctp_tsi_t **tsi_);

//    long get_rx_timeout ();
//    long get_tx_timeout ();

    //  POLLIN on sender side should mean NAK or SPMR receiving.
    //  process_upstream function is used to handle such a situation.
//    void process_upstream ();

  private:
    //  Compute size of the buffer based on rate and recovery interval.
//    int compute_sqns (int tpdu_);

    //  SCTP transport.
//    sctp_sock_t *sock;

    int last_rx_status, last_tx_status;

    //  Associated socket options.
    options_t options;

    //  true when SCTP should create receiving side.
    bool receiver;

    //  Array of sctp_msgv_t structures to store received data
    //  from the socket (sctp_transport_recvmsgv).
//    sctp_msgv_t *sctp_msgv;

    //  Size of sctp_msgv array.
    size_t sctp_msgv_len;

    // How many bytes were read from sctp socket.
    size_t nbytes_rec;

    //  How many bytes were processed from last sctp socket read.
    size_t nbytes_processed;

    //  How many messages from sctp_msgv were already sent up.
    size_t sctp_msgv_processed;
};
}
#endif

#endif
