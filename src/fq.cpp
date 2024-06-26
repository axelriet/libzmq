/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "fq.hpp"
#include "pipe.hpp"
#include "err.hpp"
#include "msg.hpp"

zmq::fq_t::fq_t () : _active (0), _current (0), _more (false)
{
}

zmq::fq_t::~fq_t ()
{
    zmq_assert (_pipes.empty ());
}

void zmq::fq_t::attach (pipe_t *pipe_)
{
    _pipes.push_back (pipe_);
    _pipes.swap (_active, _pipes.size () - 1);
    _active++;
}

void zmq::fq_t::pipe_terminated (pipe_t *pipe_)
{
    const pipes_t::size_type index = _pipes.index (pipe_);

    //  Remove the pipe from the list; adjust number of active pipes
    //  accordingly.
    if (index < _active) {
        _active--;
        _pipes.swap (index, _active);
        if (_current == _active)
            _current = 0;
    }
    _pipes.erase (pipe_);
}

void zmq::fq_t::activated (pipe_t *pipe_)
{
    //  Move the pipe to the list of active pipes.
    _pipes.swap (_pipes.index (pipe_), _active);
    _active++;
}

int zmq::fq_t::recv (msg_t *msg_)
{
    return recvpipe (msg_, NULL);
}

int zmq::fq_t::recvpipe (msg_t *msg_, pipe_t **pipe_)
{
    //  Deallocate old content of the message.
    int rc = msg_->close ();
    errno_assert (rc == 0);

    //  Round-robin over the pipes to get the next message.
    while (_active > 0) {
        //  Try to fetch new message. If we've already read part of the message
        //  subsequent part should be immediately available.
        const bool fetched = _pipes[_current]->read (msg_);

        //  Note that when message is not fetched, current pipe is deactivated
        //  and replaced by another active pipe. Thus we don't have to increase
        //  the 'current' pointer.
        if (fetched) {
            if (pipe_)
                *pipe_ = _pipes[_current];
            _more = (msg_->flagsp () & msg_t::more) != 0;
            if (!_more) {
                _current = (_current + 1) % _active;
            }
            return 0;
        }

        //  Check the atomicity of the message.
        //  If we've already received the first part of the message
        //  we should get the remaining parts without blocking.
        zmq_assert (!_more);

        _active--;
        _pipes.swap (_current, _active);
        if (_current == _active)
            _current = 0;
    }

    //  No message is available. Initialise the output parameter
    //  to be a 0-byte message.
    rc = msg_->init ();
    errno_assert (rc == 0);
    errno = EAGAIN;
    return -1;
}

bool zmq::fq_t::has_in ()
{
    //  There are subsequent parts of the partly-read message available.
    if (_more)
        return true;

    //  Note that messing with current doesn't break the fairness of fair
    //  queueing algorithm. If there are no messages available current will
    //  get back to its original value. Otherwise it'll point to the first
    //  pipe holding messages, skipping only pipes with no messages available.
    while (_active > 0) {
        if (_pipes[_current]->check_read ())
            return true;

        //  Deactivate the pipe.
        _active--;
        _pipes.swap (_current, _active);
        if (_current == _active)
            _current = 0;
    }

    return false;
}
