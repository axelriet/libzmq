/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_COMMAND_HPP_INCLUDED__
#define __ZMQ_COMMAND_HPP_INCLUDED__

#include <string>
#include "stdint.hpp"
#include "endpoint.hpp"
#include "platform.hpp"

namespace zmq
{
class object_t;
class own_t;
struct i_engine;
class pipe_t;
class socket_base_t;

//  This structure defines the commands that can be sent between threads.

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier
__declspec (align (ZMQ_CACHELINE_SIZE))
#endif
struct command_t
{
    //  Object to process the command.
    zmq::object_t *destination;

    enum type_t
    {
        stop,
        plug,
        own,
        attach,
        bind,
        activate_read,
        activate_write,
        hiccup,
        pipe_term,
        pipe_term_ack,
        pipe_hwm,
        term_req,
        term,
        term_ack,
        term_endpoint,
        reap,
        reaped,
        inproc_connected,
        conn_failed,
        pipe_peer_stats,
        pipe_stats_publish,
        done
    } type;

    union args_t
    {
        //  Sent to I/O thread to let it know that it should
        //  terminate itself.
        struct
        {
        } stop;

        //  Sent to I/O object to make it register with its I/O thread.
        struct
        {
        } plug;

        //  Sent to socket to let it know about the newly created object.
        struct
        {
            zmq::own_t *object;
        } own;

        //  Attach the engine to the session. If engine is NULL, it informs
        //  session that the connection have failed.
        struct
        {
            struct i_engine *engine;
        } attach;

        //  Sent from session to socket to establish pipe(s) between them.
        //  Caller have used inc_seqnum beforehand sending the command.
        struct
        {
            zmq::pipe_t *pipe;
        } bind;

        //  Sent by pipe writer to inform dormant pipe reader that there
        //  are messages in the pipe.
        struct
        {
        } activate_read;

        //  Sent by pipe reader to inform pipe writer about how many
        //  messages it has read so far.
        struct
        {
            uint64_t msgs_read;
        } activate_write;

        //  Sent by pipe reader to writer after creating a new inpipe.
        //  The parameter is actually of type pipe_t::upipe_t, however,
        //  its definition is private so we'll have to do with void*.
        struct
        {
            void *pipe;
        } hiccup;

        //  Sent by pipe reader to pipe writer to ask it to terminate
        //  its end of the pipe.
        struct
        {
        } pipe_term;

        //  Pipe writer acknowledges pipe_term command.
        struct
        {
        } pipe_term_ack;

        //  Sent by one of pipe to another part for modify hwm
        struct
        {
            int inhwm;
            int outhwm;
        } pipe_hwm;

        //  Sent by I/O object ot the socket to request the shutdown of
        //  the I/O object.
        struct
        {
            zmq::own_t *object;
        } term_req;

        //  Sent by socket to I/O object to start its shutdown.
        struct
        {
            int linger;
        } term;

        //  Sent by I/O object to the socket to acknowledge it has
        //  shut down.
        struct
        {
        } term_ack;

        //  Sent by session_base (I/O thread) to socket (application thread)
        //  to ask to disconnect the endpoint.
        struct
        {
            std::string *endpoint;
        } term_endpoint;

        //  Transfers the ownership of the closed socket
        //  to the reaper thread.
        struct
        {
            zmq::socket_base_t *socket;
        } reap;

        //  Closed socket notifies the reaper that it's already deallocated.
        struct
        {
        } reaped;

        //  Send application-side pipe count and ask to send monitor event
        struct
        {
            uint64_t queue_count;
            zmq::own_t *socket_base;
            endpoint_uri_pair_t *endpoint_pair;
        } pipe_peer_stats;

        //  Collate application thread and I/O thread pipe counts and endpoints
        //  and send as event
        struct
        {
            uint64_t outbound_queue_count;
            uint64_t inbound_queue_count;
            endpoint_uri_pair_t *endpoint_pair;
        } pipe_stats_publish;

        //  Sent by reaper thread to the term thread when all the sockets
        //  are successfully deallocated.
        struct
        {
        } done;

    } args;
#ifdef _MSC_VER
};
#pragma warning(pop)
#else
}
#ifdef HAVE_POSIX_MEMALIGN
__attribute__ ((aligned (ZMQ_CACHELINE_SIZE)))
#endif
;
#endif
}
#endif
