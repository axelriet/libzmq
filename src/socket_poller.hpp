/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_SOCKET_POLLER_HPP_INCLUDED__
#define __ZMQ_SOCKET_POLLER_HPP_INCLUDED__

#include "poller.hpp"

#if defined ZMQ_POLL_BASED_ON_POLL && !defined ZMQ_HAVE_WINDOWS
#include <poll.h>
#endif

#if defined ZMQ_HAVE_WINDOWS
#include "windows.hpp"
#elif defined ZMQ_HAVE_VXWORKS
#include <unistd.h>
#include <sys/time.h>
#include <strings.h>
#else
#include <unistd.h>
#endif

#include <vector>

#include "socket_base.hpp"
#include "signaler.hpp"
#include "polling_util.hpp"

namespace zmq
{
class socket_poller_t
{
  public:
    socket_poller_t ();
    ~socket_poller_t ();

    typedef zmq_poller_event_t event_t;

    int add (socket_base_t *socket_, void *user_data_, short events_);
    int modify (const socket_base_t *socket_, short events_);
    int remove (socket_base_t *socket_);

    int add_fd (fd_t fd_, _In_opt_ void *user_data_, short events_);
    int modify_fd (fd_t fd_, short events_);
    int remove_fd (fd_t fd_);
    // Returns the signaler's fd if there is one, otherwise errors.
    int signaler_fd (fd_t *fd_) const;

    int wait (event_t *events_, int n_events_, long timeout_);

    int size () const { return static_cast<int> (_items.size ()); };

    //  Return false if object is not a socket.
    bool check_tag () const;

  private:
    typedef struct item_t
    {
        socket_base_t *socket;
        fd_t fd;
        void *user_data;
        short events;
#if defined ZMQ_POLL_BASED_ON_POLL
        int pollfd_index;
#endif
    } item_t;

    static void zero_trail_events (zmq::socket_poller_t::event_t *events_,
                                   int n_events_,
                                   int found_);
#if defined ZMQ_POLL_BASED_ON_POLL
    int check_events (zmq::socket_poller_t::event_t *events_, int n_events_);
#elif defined ZMQ_POLL_BASED_ON_SELECT
    int check_events (zmq::socket_poller_t::event_t *events_,
                      int n_events_,
                      fd_set &inset_,
                      fd_set &outset_,
                      fd_set &errset_);
#endif
    static int adjust_timeout (zmq::clock_t &clock_,
                               long timeout_,
                               uint64_t &now_,
                               uint64_t &end_,
                               bool &first_pass_);
    static bool is_socket (const item_t &item, const socket_base_t *socket_)
    {
        return item.socket == socket_;
    }
    static bool is_fd (const item_t &item, fd_t fd_)
    {
        return !item.socket && item.fd == fd_;
    }

    int rebuild ();

    //  Used to check whether the object is a socket_poller.
    uint32_t _tag;

    //  Signaler used for thread safe sockets polling
    signaler_t *_signaler;

    //  List of sockets
    typedef std::vector<item_t> items_t;
    items_t _items;

    //  Does the pollset needs rebuilding?
    bool _need_rebuild;

    //  Should the signaler be used for the thread safe polling?
    bool _use_signaler;

    //  Size of the pollset
    int _pollset_size;

#if defined ZMQ_POLL_BASED_ON_POLL
    pollfd *_pollfds;
#elif defined ZMQ_POLL_BASED_ON_SELECT
    resizable_optimized_fd_set_t _pollset_in;
    resizable_optimized_fd_set_t _pollset_out;
    resizable_optimized_fd_set_t _pollset_err;
    zmq::fd_t _max_fd;
#endif

    ZMQ_NON_COPYABLE_NOR_MOVABLE (socket_poller_t)
};
}

#endif
