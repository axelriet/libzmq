/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#ifdef ZMQ_HAVE_SCTP

#ifdef ZMQ_HAVE_LINUX
#include <poll.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <string>

#include "options.hpp"
#include "SCTP.hpp"
#include "config.hpp"
#include "err.hpp"
#include "random.hpp"
#include "stdint.hpp"

#if 0

#ifndef MSG_ERRQUEUE
#define MSG_ERRQUEUE 0x2000
#endif

zmq::sctp_t::sctp_t (bool receiver_, const options_t &options_) :
    sock (NULL),
    options (options_),
    receiver (receiver_),
    sctp_msgv (NULL),
    sctp_msgv_len (0),
    nbytes_rec (0),
    nbytes_processed (0),
    sctp_msgv_processed (0)
{
#ifndef NDEBUG
    sctp_min_log_level = SCTP_LOG_LEVEL_NORMAL;
#else
    sctp_min_log_level = SCTP_LOG_LEVEL_WARNING;
#endif
}

//  Resolve SCTP socket address.
//  network_ of the form <interface & multicast group decls>:<IP port>
//  e.g. eth0;239.192.0.1:7500
//       link-local;224.250.0.1,224.250.0.2;224.250.0.3:8000
//       ;[fe80::1%en0]:7500
int zmq::sctp_t::init_address (const char *network_,
                                     struct sctp_addrinfo_t **res,
                                     uint16_t *port_number)
{
    char network[256]{};

    //  Parse port number, start from end for IPv6
    const char *port_delim = strrchr (network_, ':');

    if (!port_delim) {
        errno = EINVAL;
        return -1;
    }

    int portNum = atoi (port_delim + 1);

    if (portNum > UINT16_MAX || portNum < 0) {
        errno = EINVAL;
        return -1;
    }

    *port_number = (uint16_t) portNum;

    if (port_delim - network_ >= (int) sizeof (network) - 1) {
        errno = EINVAL;
        return -1;
    }

    memcpy (network, network_, port_delim - network_);

    sctp_error_t *sctp_error = NULL;
    struct sctp_addrinfo_t hints;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;

    if (!sctp_getaddrinfo (network, NULL, res, &sctp_error)) {
        //  Invalid parameters don't set sctp_error_t.
        zmq_assert (sctp_error != NULL);
        if (sctp_error->domain == SCTP_ERROR_DOMAIN_IF &&

            //  NB: cannot catch EAI_BADFLAGS.
            (sctp_error->code != SCTP_ERROR_SERVICE
             && sctp_error->code != SCTP_ERROR_SOCKTNOSUPPORT)) {
            //  User, host, or network configuration or transient error.
            sctp_error_free (sctp_error);
            errno = EINVAL;
            return -1;
        }

        //  Fatal SCTP internal error.
        zmq_assert (false);
    }
    return 0;
}

//  Create, bind and connect SCTP socket.
int zmq::sctp_t::init (bool udp_encapsulation_, const char *network_)
{
    //  Can not open transport before destroying old one.
    zmq_assert (sock == NULL);
    zmq_assert (options.rate > 0);

    //  Zero counter used in msgrecv.
    nbytes_rec = 0;
    nbytes_processed = 0;
    sctp_msgv_processed = 0;

    uint16_t port_number;
    struct sctp_addrinfo_t *res = NULL;
    sa_family_t sa_family;

    sctp_error_t *sctp_error = NULL;

    if (init_address (network_, &res, &port_number) < 0) {
        goto err_abort;
    }

    zmq_assert (res != NULL);

    //  Pick up detected IP family.
    sa_family = res->ai_send_addrs[0].gsr_group.ss_family;

    //  Create IP/SCTP or UDP/SCTP socket.
    if (udp_encapsulation_) {
        if (!SCTP (&sock, sa_family, SOCK_SEQPACKET, IPPROTO_UDP,
                         &sctp_error)) {
            //  Invalid parameters don't set sctp_error_t.
            zmq_assert (sctp_error != NULL);
            if (sctp_error->domain == SCTP_ERROR_DOMAIN_SOCKET
                && (sctp_error->code != SCTP_ERROR_BADF
                    && sctp_error->code != SCTP_ERROR_FAULT
                    && sctp_error->code != SCTP_ERROR_NOPROTOOPT
                    && sctp_error->code != SCTP_ERROR_FAILED))

                //  User, host, or network configuration or transient error.
                goto err_abort;

            //  Fatal SCTP internal error.
            zmq_assert (false);
        }

        //  All options are of data type int
        const int encapsulation_port = port_number;
        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_UDP_ENCAP_UCAST_PORT,
                             &encapsulation_port, sizeof (encapsulation_port)))
            goto err_abort;
        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_UDP_ENCAP_MCAST_PORT,
                             &encapsulation_port, sizeof (encapsulation_port)))
            goto err_abort;
    } else {
        if (!SCTP (&sock, sa_family, SOCK_SEQPACKET, IPPROTO_SCTP,
                         &sctp_error)) {
            //  Invalid parameters don't set sctp_error_t.
            zmq_assert (sctp_error != NULL);
            if (sctp_error->domain == SCTP_ERROR_DOMAIN_SOCKET
                && (sctp_error->code != SCTP_ERROR_BADF
                    && sctp_error->code != SCTP_ERROR_FAULT
                    && sctp_error->code != SCTP_ERROR_NOPROTOOPT
                    && sctp_error->code != SCTP_ERROR_FAILED))

                //  User, host, or network configuration or transient error.
                goto err_abort;

            //  Fatal SCTP internal error.
            zmq_assert (false);
        }
    }

    {
        const int rcvbuf = (int) options.rcvbuf;
        if (rcvbuf >= 0) {
            if (!sctp_setsockopt (sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf,
                                 sizeof (rcvbuf)))
                goto err_abort;
        }

        const int sndbuf = (int) options.sndbuf;
        if (sndbuf >= 0) {
            if (!sctp_setsockopt (sock, SOL_SOCKET, SO_SNDBUF, &sndbuf,
                                 sizeof (sndbuf)))
                goto err_abort;
        }

        const int max_tpdu = (int) options.multicast_maxtpdu;
        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_MTU, &max_tpdu,
                             sizeof (max_tpdu)))
            goto err_abort;
    }

    if (receiver) {
        const int recv_only = 1, rxw_max_tpdu = (int) options.multicast_maxtpdu,
                  rxw_sqns = compute_sqns (rxw_max_tpdu),
                  peer_expiry = sctp_secs (300), spmr_expiry = sctp_msecs (250),
                  nak_bo_ivl = sctp_msecs (50), nak_rpt_ivl = sctp_msecs (200),
                  nak_rdata_ivl = sctp_msecs (200), nak_data_retries = 50,
                  nak_ncf_retries = 50;

        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_RECV_ONLY, &recv_only,
                             sizeof (recv_only))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_RXW_SQNS, &rxw_sqns,
                                sizeof (rxw_sqns))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_PEER_EXPIRY,
                                &peer_expiry, sizeof (peer_expiry))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_SPMR_EXPIRY,
                                &spmr_expiry, sizeof (spmr_expiry))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_NAK_BO_IVL, &nak_bo_ivl,
                                sizeof (nak_bo_ivl))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_NAK_RPT_IVL,
                                &nak_rpt_ivl, sizeof (nak_rpt_ivl))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_NAK_RDATA_IVL,
                                &nak_rdata_ivl, sizeof (nak_rdata_ivl))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_NAK_DATA_RETRIES,
                                &nak_data_retries, sizeof (nak_data_retries))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_NAK_NCF_RETRIES,
                                &nak_ncf_retries, sizeof (nak_ncf_retries)))
            goto err_abort;
    } else {
        //
        // Rate in kbps must be converted to B/sec. Options.rate MUST be <= 17179869
        // in order to fit in a 32-bit integer: 17,179,869 * 1000 / 8 = 2,147,483,625
        //

        const int send_only = 1, max_rte = (int) ((options.rate * 1000.0) / 8),
                  txw_max_tpdu = (int) options.multicast_maxtpdu,
                  txw_sqns = compute_sqns (txw_max_tpdu),
                  ambient_spm = sctp_secs (30),
                  heartbeat_spm[] = {
                    sctp_msecs (100), sctp_msecs (100),  sctp_msecs (100),
                    sctp_msecs (100), sctp_msecs (1300), sctp_secs (7),
                    sctp_secs (16),   sctp_secs (25),    sctp_secs (30)};

        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_SEND_ONLY, &send_only,
                             sizeof (send_only))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_ODATA_MAX_RTE, &max_rte,
                                sizeof (max_rte))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_TXW_SQNS, &txw_sqns,
                                sizeof (txw_sqns))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_AMBIENT_SPM,
                                &ambient_spm, sizeof (ambient_spm))
            || !sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_HEARTBEAT_SPM,
                                &heartbeat_spm, sizeof (heartbeat_spm)))
            goto err_abort;
    }

    //  SCTP transport GSI.
    struct sctp_sockaddr_t addr;

    memset (&addr, 0, sizeof (addr));
    addr.sa_port = port_number;
    addr.sa_addr.sport = DEFAULT_DATA_SOURCE_PORT;

    //  Create random GSI.
    uint32_t buf[2];
    buf[0] = generate_random ();
    buf[1] = generate_random ();
    if (!sctp_gsi_create_from_data (&addr.sa_addr.gsi, (uint8_t *) buf, 8))
        goto err_abort;


    //  Bind a transport to the specified network devices.
    struct sctp_interface_req_t if_req;
    memset (&if_req, 0, sizeof (if_req));
    if_req.ir_interface = res->ai_recv_addrs[0].gsr_interface;
    if_req.ir_scope_id = 0;
    if (AF_INET6 == sa_family) {
        struct sockaddr_in6 sa6;
        memcpy (&sa6, &res->ai_recv_addrs[0].gsr_group, sizeof (sa6));
        if_req.ir_scope_id = sa6.sin6_scope_id;
    }
    if (!sctp_bind3 (sock, &addr, sizeof (addr), &if_req, sizeof (if_req),
                    &if_req, sizeof (if_req), &sctp_error)) {
        //  Invalid parameters don't set sctp_error_t.
        zmq_assert (sctp_error != NULL);
        if ((sctp_error->domain == SCTP_ERROR_DOMAIN_SOCKET
             || sctp_error->domain == SCTP_ERROR_DOMAIN_IF)
            && (sctp_error->code != SCTP_ERROR_INVAL
                && sctp_error->code != SCTP_ERROR_BADF
                && sctp_error->code != SCTP_ERROR_FAULT))

            //  User, host, or network configuration or transient error.
            goto err_abort;

        //  Fatal SCTP internal error.
        zmq_assert (false);
    }

    //  Join IP multicast groups.
    for (unsigned i = 0; i < res->ai_recv_addrs_len; i++) {
        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_JOIN_GROUP,
                             &res->ai_recv_addrs[i], sizeof (struct group_req)))
            goto err_abort;
    }
    if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_SEND_GROUP,
                         &res->ai_send_addrs[0], sizeof (struct group_req)))
        goto err_abort;

    sctp_freeaddrinfo (res);
    res = NULL;

    //  Set IP level parameters.
    {
        // Multicast loopback
        const int multicast_loop = options.multicast_loop;
        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_MULTICAST_LOOP,
                             &multicast_loop, sizeof (multicast_loop)))
            goto err_abort;

        const int multicast_hops = options.multicast_hops;
        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_MULTICAST_HOPS,
                             &multicast_hops, sizeof (multicast_hops)))
            goto err_abort;

        //  Expedited Forwarding PHB for network elements, no ECN.
        //  Ignore return value due to varied runtime support.
        const int dscp = 0x2e << 2;
        if (AF_INET6 != sa_family)
            sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_TOS, &dscp, sizeof (dscp));

        const int nonblocking = 1;
        if (!sctp_setsockopt (sock, IPPROTO_SCTP, SCTP_NOBLOCK, &nonblocking,
                             sizeof (nonblocking)))
            goto err_abort;
    }

    //  Connect SCTP transport to start state machine.
    if (!sctp_connect (sock, &sctp_error)) {
        //  Invalid parameters don't set sctp_error_t.
        zmq_assert (sctp_error != NULL);
        goto err_abort;
    }

    //  For receiver transport preallocate sctp_msgv array.
    if (receiver) {
        zmq_assert (options.in_batch_size > 0);
        size_t max_tsdu_size = get_max_tsdu_size ();
        sctp_msgv_len = (int) options.in_batch_size / max_tsdu_size;
        if ((int) options.in_batch_size % max_tsdu_size)
            sctp_msgv_len++;
        zmq_assert (sctp_msgv_len);

        sctp_msgv =
          (sctp_msgv_t *) std::malloc (sizeof (sctp_msgv_t) * sctp_msgv_len);
        alloc_assert (sctp_msgv);
    }

    return 0;

err_abort:
    if (sock != NULL) {
        sctp_close (sock, FALSE);
        sock = NULL;
    }
    if (res != NULL) {
        sctp_freeaddrinfo (res);
        res = NULL;
    }
    if (sctp_error != NULL) {
        sctp_error_free (sctp_error);
        sctp_error = NULL;
    }
    errno = EINVAL;
    return -1;
}

zmq::sctp_t::~sctp_t ()
{
    if (sctp_msgv)
        std::free (sctp_msgv);
    if (sock)
        sctp_close (sock, TRUE);
}

//  Get receiver fds. receive_fd_ is signaled for incoming packets,
//  waiting_pipe_fd_ is signaled for state driven events and data.
void zmq::sctp_t::get_receiver_fds (fd_t *receive_fd_,
                                          fd_t *waiting_pipe_fd_)
{
    socklen_t socklen;
    bool rc;

    zmq_assert (receive_fd_);
    zmq_assert (waiting_pipe_fd_);

    socklen = sizeof (*receive_fd_);
    rc =
      sctp_getsockopt (sock, IPPROTO_SCTP, SCTP_RECV_SOCK, receive_fd_, &socklen);
    zmq_assert (rc);
    zmq_assert (socklen == sizeof (*receive_fd_));

    socklen = sizeof (*waiting_pipe_fd_);
    rc = sctp_getsockopt (sock, IPPROTO_SCTP, SCTP_PENDING_SOCK, waiting_pipe_fd_,
                         &socklen);
    zmq_assert (rc);
    zmq_assert (socklen == sizeof (*waiting_pipe_fd_));
}

//  Get fds and store them into user allocated memory.
//  send_fd is for non-blocking send wire notifications.
//  receive_fd_ is for incoming back-channel protocol packets.
//  rdata_notify_fd_ is raised for waiting repair transmissions.
//  pending_notify_fd_ is for state driven events.
void zmq::sctp_t::get_sender_fds (fd_t *send_fd_,
                                        fd_t *receive_fd_,
                                        fd_t *rdata_notify_fd_,
                                        fd_t *pending_notify_fd_)
{
    socklen_t socklen;
    bool rc;

    zmq_assert (send_fd_);
    zmq_assert (receive_fd_);
    zmq_assert (rdata_notify_fd_);
    zmq_assert (pending_notify_fd_);

    socklen = sizeof (*send_fd_);
    rc = sctp_getsockopt (sock, IPPROTO_SCTP, SCTP_SEND_SOCK, send_fd_, &socklen);
    zmq_assert (rc);
    zmq_assert (socklen == sizeof (*receive_fd_));

    socklen = sizeof (*receive_fd_);
    rc =
      sctp_getsockopt (sock, IPPROTO_SCTP, SCTP_RECV_SOCK, receive_fd_, &socklen);
    zmq_assert (rc);
    zmq_assert (socklen == sizeof (*receive_fd_));

    socklen = sizeof (*rdata_notify_fd_);
    rc = sctp_getsockopt (sock, IPPROTO_SCTP, SCTP_REPAIR_SOCK, rdata_notify_fd_,
                         &socklen);
    zmq_assert (rc);
    zmq_assert (socklen == sizeof (*rdata_notify_fd_));

    socklen = sizeof (*pending_notify_fd_);
    rc = sctp_getsockopt (sock, IPPROTO_SCTP, SCTP_PENDING_SOCK,
                         pending_notify_fd_, &socklen);
    zmq_assert (rc);
    zmq_assert (socklen == sizeof (*pending_notify_fd_));
}

//  Send one APDU, transmit window owned memory.
//  data_len_ must be less than one TPDU.
size_t zmq::sctp_t::send (unsigned char *data_, size_t data_len_)
{
    size_t nbytes = 0;

    const int status = sctp_send (sock, data_, data_len_, &nbytes);

    //  We have to write all data as one packet.
    if (nbytes > 0) {
        zmq_assert (status == SCTP_IO_STATUS_NORMAL);
        zmq_assert (nbytes == data_len_);
    } else {
        zmq_assert (status == SCTP_IO_STATUS_RATE_LIMITED
                    || status == SCTP_IO_STATUS_WOULD_BLOCK);

        if (status == SCTP_IO_STATUS_RATE_LIMITED)
            errno = ENOMEM;
        else
            errno = EBUSY;
    }

    //  Save return value.
    last_tx_status = status;

    return nbytes;
}

long zmq::sctp_t::get_rx_timeout ()
{
    if (last_rx_status != SCTP_IO_STATUS_RATE_LIMITED
        && last_rx_status != SCTP_IO_STATUS_TIMER_PENDING)
        return -1;

    struct timeval tv;
    socklen_t optlen = sizeof (tv);
    const bool rc = sctp_getsockopt (sock, IPPROTO_SCTP,
                                    last_rx_status == SCTP_IO_STATUS_RATE_LIMITED
                                      ? SCTP_RATE_REMAIN
                                      : SCTP_TIME_REMAIN,
                                    &tv, &optlen);
    zmq_assert (rc);

    const long timeout = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);

    return timeout;
}

long zmq::sctp_t::get_tx_timeout ()
{
    if (last_tx_status != SCTP_IO_STATUS_RATE_LIMITED)
        return -1;

    struct timeval tv;
    socklen_t optlen = sizeof (tv);
    const bool rc =
      sctp_getsockopt (sock, IPPROTO_SCTP, SCTP_RATE_REMAIN, &tv, &optlen);
    zmq_assert (rc);

    const long timeout = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);

    return timeout;
}

//  Return max TSDU size without fragmentation from current SCTP transport.
size_t zmq::sctp_t::get_max_tsdu_size ()
{
    int max_tsdu = 0;
    socklen_t optlen = sizeof (max_tsdu);

    bool rc = sctp_getsockopt (sock, IPPROTO_SCTP, SCTP_MSS, &max_tsdu, &optlen);
    zmq_assert (rc);
    zmq_assert (optlen == sizeof (max_tsdu));
    return (size_t) max_tsdu;
}

//  sctp_recvmsgv is called to fill the sctp_msgv array up to  sctp_msgv_len.
//  In subsequent calls data from sctp_msgv structure are returned.
ssize_t zmq::sctp_t::receive (void **raw_data_, const sctp_tsi_t **tsi_)
{
    size_t raw_data_len = 0;

    //  We just sent all data from sctp_transport_recvmsgv up
    //  and have to return 0 that another engine in this thread is scheduled.
    if (nbytes_rec == nbytes_processed && nbytes_rec > 0) {
        //  Reset all the counters.
        nbytes_rec = 0;
        nbytes_processed = 0;
        sctp_msgv_processed = 0;
        errno = EAGAIN;
        return 0;
    }

    //  If we have are going first time or if we have processed all sctp_msgv_t
    //  structure previously read from the sctp socket.
    if (nbytes_rec == nbytes_processed) {
        //  Check program flow.
        zmq_assert (sctp_msgv_processed == 0);
        zmq_assert (nbytes_processed == 0);
        zmq_assert (nbytes_rec == 0);

        //  Receive a vector of Application Protocol Domain Unit's (APDUs)
        //  from the transport.
        sctp_error_t *sctp_error = NULL;

        const int status = sctp_recvmsgv (sock, sctp_msgv, sctp_msgv_len,
                                         MSG_ERRQUEUE, &nbytes_rec, &sctp_error);

        //  Invalid parameters.
        zmq_assert (status != SCTP_IO_STATUS_ERROR);

        last_rx_status = status;

        //  In a case when no ODATA/RDATA fired POLLIN event (SPM...)
        //  sctp_recvmsg returns SCTP_IO_STATUS_TIMER_PENDING.
        if (status == SCTP_IO_STATUS_TIMER_PENDING) {
            zmq_assert (nbytes_rec == 0);

            //  In case if no RDATA/ODATA caused POLLIN 0 is
            //  returned.
            nbytes_rec = 0;
            errno = EBUSY;
            return 0;
        }

        //  Send SPMR, NAK, ACK is rate limited.
        if (status == SCTP_IO_STATUS_RATE_LIMITED) {
            zmq_assert (nbytes_rec == 0);

            //  In case if no RDATA/ODATA caused POLLIN 0 is returned.
            nbytes_rec = 0;
            errno = ENOMEM;
            return 0;
        }

        //  No peers and hence no incoming packets.
        if (status == SCTP_IO_STATUS_WOULD_BLOCK) {
            zmq_assert (nbytes_rec == 0);

            //  In case if no RDATA/ODATA caused POLLIN 0 is returned.
            nbytes_rec = 0;
            errno = EAGAIN;
            return 0;
        }

        //  Data loss.
        if (status == SCTP_IO_STATUS_RESET) {
            struct sctp_sk_buff_t *skb = sctp_msgv[0].msgv_skb[0];

            //  Save lost data TSI.
            *tsi_ = &skb->tsi;
            nbytes_rec = 0;

            //  In case of dala loss -1 is returned.
            errno = EINVAL;
            sctp_free_skb (skb);
            return -1;
        }

        zmq_assert (status == SCTP_IO_STATUS_NORMAL);
    } else {
        zmq_assert (sctp_msgv_processed <= sctp_msgv_len);
    }

    // Zero byte payloads are valid in SCTP, but not 0MQ protocol.
    zmq_assert (nbytes_rec > 0);

    // Only one APDU per sctp_msgv_t structure is allowed.
    zmq_assert (sctp_msgv[sctp_msgv_processed].msgv_len == 1);

    struct sctp_sk_buff_t *skb = sctp_msgv[sctp_msgv_processed].msgv_skb[0];

    //  Take pointers from sctp_msgv_t structure.
    *raw_data_ = skb->data;
    raw_data_len = skb->len;

    //  Save current TSI.
    *tsi_ = &skb->tsi;

    //  Move the the next sctp_msgv_t structure.
    sctp_msgv_processed++;
    zmq_assert (sctp_msgv_processed <= sctp_msgv_len);
    nbytes_processed += raw_data_len;

    return raw_data_len;
}

void zmq::sctp_t::process_upstream ()
{
    sctp_msgv_t dummy_msg;

    size_t dummy_bytes = 0;
    sctp_error_t *sctp_error = NULL;

    const int status = sctp_recvmsgv (sock, &dummy_msg, 1, MSG_ERRQUEUE,
                                     &dummy_bytes, &sctp_error);

    //  Invalid parameters.
    zmq_assert (status != SCTP_IO_STATUS_ERROR);

    //  No data should be returned.
    zmq_assert (dummy_bytes == 0
                && (status == SCTP_IO_STATUS_TIMER_PENDING
                    || status == SCTP_IO_STATUS_RATE_LIMITED
                    || status == SCTP_IO_STATUS_WOULD_BLOCK));

    last_rx_status = status;

    if (status == SCTP_IO_STATUS_TIMER_PENDING)
        errno = EBUSY;
    else if (status == SCTP_IO_STATUS_RATE_LIMITED)
        errno = ENOMEM;
    else
        errno = EAGAIN;
}

int zmq::sctp_t::compute_sqns (int tpdu_)
{
    //  Convert rate into B/ms.
    uint64_t rate = uint64_t (options.rate) / 8;

    //  Compute the size of the buffer in bytes.
    uint64_t size = uint64_t (options.recovery_ivl) * rate;

    //  Translate the size into number of packets.
    uint64_t sqns = size / tpdu_;

    //  Buffer should be able to hold at least one packet.
    if (sqns == 0)
        sqns = 1;

    return (int) sqns;
}

#endif
#endif