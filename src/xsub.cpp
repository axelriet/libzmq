/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include <string.h>

#include "macros.hpp"
#include "xsub.hpp"
#include "err.hpp"

zmq::xsub_t::xsub_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    socket_base_t (parent_, tid_, sid_),
    _verbose_unsubs (false),
    _has_message (false),
    _more_send (false),
    _more_recv (false),
    _process_subscribe (false),
    _only_first_subscribe (false)
{
    options.type = ZMQ_XSUB;

    //  When socket is being closed down we don't want to wait till pending
    //  subscription commands are sent to the wire.
    options.linger.store (0);

    const int rc = _message.init ();
    errno_assert (rc == 0);
}

zmq::xsub_t::~xsub_t ()
{
    const int rc = _message.close ();
    errno_assert (rc == 0);
}

void zmq::xsub_t::xattach_pipe (pipe_t *pipe_,
                                bool subscribe_to_all_,
                                bool locally_initiated_)
{
    LIBZMQ_UNUSED (subscribe_to_all_);
    LIBZMQ_UNUSED (locally_initiated_);

    zmq_assert (pipe_);
    _fq.attach (pipe_);
    _dist.attach (pipe_);

    //  Send all the cached subscriptions to the new upstream peer.
    _subscriptions.apply (send_subscription, pipe_);
    pipe_->flush ();
}

void zmq::xsub_t::xread_activated (pipe_t *pipe_)
{
    _fq.activated (pipe_);
}

void zmq::xsub_t::xwrite_activated (pipe_t *pipe_)
{
    _dist.activated (pipe_);
}

void zmq::xsub_t::xpipe_terminated (pipe_t *pipe_)
{
    _fq.pipe_terminated (pipe_);
    _dist.pipe_terminated (pipe_);
}

void zmq::xsub_t::xhiccuped (pipe_t *pipe_)
{
    //  Send all the cached subscriptions to the hiccuped pipe.
    _subscriptions.apply (send_subscription, pipe_);
    pipe_->flush ();
}

int zmq::xsub_t::xsetsockopt (int option_,
                              _In_reads_bytes_opt_ (optvallen_)
                                const void *optval_,
                              _When_ (optval_ == NULL, _In_range_ (0, 0))
                                const size_t optvallen_)
{
    if (option_ == ZMQ_ONLY_FIRST_SUBSCRIBE) {
        if (optvallen_ != sizeof (int)
            || *static_cast<const int *> (optval_) < 0) {
            errno = EINVAL;
            return -1;
        }
        _only_first_subscribe = (*static_cast<const int *> (optval_) != 0);
        return 0;
    }
#ifdef ZMQ_BUILD_DRAFT_API
    else if (option_ == ZMQ_XSUB_VERBOSE_UNSUBSCRIBE) {
        _verbose_unsubs = (*static_cast<const int *> (optval_) != 0);
        return 0;
    }
#endif
    errno = EINVAL;
    return -1;
}

int zmq::xsub_t::xgetsockopt (int option_, void *optval_, size_t *optvallen_)
{
    if (option_ == ZMQ_TOPICS_COUNT) {
        // make sure to use a multi-thread safe function to avoid race conditions with I/O threads
        // where subscriptions are processed:
#ifdef ZMQ_USE_RADIX_TREE
        uint64_t num_subscriptions = _subscriptions.size ();
#else
        uint64_t num_subscriptions = _subscriptions.num_prefixes ();
#endif

        return do_getsockopt<int> (optval_, optvallen_,
                                   (int) num_subscriptions);
    }

    // room for future options here

    errno = EINVAL;
    return -1;
}

int zmq::xsub_t::xsend (msg_t *msg_)
{
    size_t size = msg_->sizep ();
    unsigned char *data = static_cast<unsigned char *> (msg_->datap ());

    const bool first_part = !_more_send;
    _more_send = (msg_->flagsp () & msg_t::more) != 0;

    if (first_part) {
        _process_subscribe = !_only_first_subscribe;
    } else if (!_process_subscribe) {
        //  User message sent upstream to XPUB socket
        return _dist.send_to_all (msg_);
    }

    if (msg_->is_subscribe () || (size > 0 && *data == 1)) {
        //  Process subscribe message
        //  This used to filter out duplicate subscriptions,
        //  however this is already done on the XPUB side and
        //  doing it here as well breaks ZMQ_XPUB_VERBOSE
        //  when there are forwarding devices involved.
        if (!msg_->is_subscribe ()) {
            data = data + 1;
            size = size - 1;
        }
        _subscriptions.add (data, size);
        _process_subscribe = true;
        return _dist.send_to_all (msg_);
    }
    if (msg_->is_cancel () || (size > 0 && *data == 0)) {
        //  Process unsubscribe message
        if (!msg_->is_cancel ()) {
            data = data + 1;
            size = size - 1;
        }
        _process_subscribe = true;
        const bool rm_result = _subscriptions.rm (data, size);
        if (rm_result || _verbose_unsubs)
            return _dist.send_to_all (msg_);
    } else
        //  User message sent upstream to XPUB socket
        return _dist.send_to_all (msg_);

    int rc = msg_->close ();
    errno_assert (rc == 0);
    rc = msg_->init ();
    errno_assert (rc == 0);

    return 0;
}

bool zmq::xsub_t::xhas_out ()
{
    //  Subscription can be added/removed anytime.
    return true;
}

int zmq::xsub_t::xrecv (msg_t *msg_)
{
    //  If there's already a message prepared by a previous call to zmq_poll,
    //  return it straight ahead.
    if (_has_message) {
        const int rc = msg_->move (_message);
        errno_assert (rc == 0);
        _has_message = false;
        _more_recv = (msg_->flagsp () & msg_t::more) != 0;
        return 0;
    }

    //  TODO: This can result in infinite loop in the case of continuous
    //  stream of non-matching messages which breaks the non-blocking recv
    //  semantics.
    while (true) {
        //  Get a message using fair queueing algorithm.
        int rc = _fq.recv (msg_);

        //  If there's no message available, return immediately.
        //  The same when error occurs.
        if (rc != 0)
            return -1;

        //  Check whether the message matches at least one subscription.
        //  Non-initial parts of the message are passed
        if (_more_recv || !options.filter || match (msg_)) {
            _more_recv = (msg_->flagsp () & msg_t::more) != 0;
            return 0;
        }

        //  Message doesn't match. Pop any remaining parts of the message
        //  from the pipe.
        while (msg_->flagsp () & msg_t::more) {
            rc = _fq.recv (msg_);
            errno_assert (rc == 0);
        }
    }
}

bool zmq::xsub_t::xhas_in ()
{
    //  There are subsequent parts of the partly-read message available.
    if (_more_recv)
        return true;

    //  If there's already a message prepared by a previous call to zmq_poll,
    //  return straight ahead.
    if (_has_message)
        return true;

    //  TODO: This can result in infinite loop in the case of continuous
    //  stream of non-matching messages.
    while (true) {
        //  Get a message using fair queueing algorithm.
        int rc = _fq.recv (&_message);

        //  If there's no message available, return immediately.
        //  The same when error occurs.
        if (rc != 0) {
            errno_assert (errno == EAGAIN);
            return false;
        }

        //  Check whether the message matches at least one subscription.
        if (!options.filter || match (&_message)) {
            _has_message = true;
            return true;
        }

        //  Message doesn't match. Pop any remaining parts of the message
        //  from the pipe.
        while (_message.flagsp () & msg_t::more) {
            rc = _fq.recv (&_message);
            errno_assert (rc == 0);
        }
    }
}

bool zmq::xsub_t::match (msg_t *msg_)
{
    const bool matching = _subscriptions.check (
      static_cast<unsigned char *> (msg_->datap ()), msg_->sizep ());

    return matching ^ options.invert_matching;
}

void zmq::xsub_t::send_subscription (
  _In_reads_bytes_opt_ (size_) unsigned char *data_,
  _When_ (data_ == NULL, _In_range_ (0, 0)) size_t size_,
  _In_ void *arg_)
{
    pipe_t *pipe = static_cast<pipe_t *> (arg_);

    //  Create the subscription message.
    msg_t msg;
    const int rc = msg.init_subscribe (size_, data_);
    errno_assert (rc == 0);

    //  Send it to the pipe.
    const bool sent = pipe->write (&msg);
    //  If we reached the SNDHWM, and thus cannot send the subscription, drop
    //  the subscription message instead. This matches the behaviour of
    //  zmq_setsockopt(ZMQ_SUBSCRIBE, ...), which also drops subscriptions
    //  when the SNDHWM is reached.
    if (!sent)
        msg.close ();
}
