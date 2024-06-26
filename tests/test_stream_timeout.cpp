/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_monitoring.hpp"
#include "testutil_unity.hpp"

#include <stdlib.h>
#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

static void test_stream_handshake_timeout_accept ()
{
    char my_endpoint[MAX_SOCKET_STRING];

    //  We use this socket in raw mode, to make a connection and send nothing
    void *stream = test_context_socket (ZMQ_STREAM);

    int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (stream, ZMQ_LINGER, &zero, sizeof (zero)));

    //  We'll be using this socket to test TCP stream handshake timeout
    void *dealer = test_context_socket (ZMQ_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer, ZMQ_LINGER, &zero, sizeof (zero)));
    int val, tenth = 100;
    size_t vsize = sizeof (val);

    // check for the expected default handshake timeout value - 30 sec
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (dealer, ZMQ_HANDSHAKE_IVL, &val, &vsize));
    TEST_ASSERT_EQUAL (sizeof (val), vsize);
    TEST_ASSERT_EQUAL_INT (30000, val);
    // make handshake timeout faster - 1/10 sec
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer, ZMQ_HANDSHAKE_IVL, &tenth, sizeof (tenth)));
    vsize = sizeof (val);
    // make sure zmq_setsockopt changed the value
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (dealer, ZMQ_HANDSHAKE_IVL, &val, &vsize));
    TEST_ASSERT_EQUAL (sizeof (val), vsize);
    TEST_ASSERT_EQUAL_INT (tenth, val);

    //  Create and connect a socket for collecting monitor events on dealer
    void *dealer_mon = test_context_socket (ZMQ_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_socket_monitor (
      dealer, "inproc://monitor-dealer",
      ZMQ_EVENT_CONNECTED | ZMQ_EVENT_DISCONNECTED | ZMQ_EVENT_ACCEPTED));

    //  Connect to the inproc endpoint so we'll get events
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_connect (dealer_mon, "inproc://monitor-dealer"));

    // bind dealer socket to accept connection from non-sending stream socket
    bind_loopback_ipv4 (dealer, my_endpoint, sizeof my_endpoint);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (stream, my_endpoint));

    // we should get ZMQ_EVENT_ACCEPTED and then ZMQ_EVENT_DISCONNECTED
    int event = get_monitor_event (dealer_mon, NULL, NULL);
    TEST_ASSERT_EQUAL_INT (ZMQ_EVENT_ACCEPTED, event);
    event = get_monitor_event (dealer_mon, NULL, NULL);
    TEST_ASSERT_EQUAL_INT (ZMQ_EVENT_DISCONNECTED, event);

    test_context_socket_close (dealer);
    test_context_socket_close (dealer_mon);
    test_context_socket_close (stream);
}

static void test_stream_handshake_timeout_connect ()
{
    char my_endpoint[MAX_SOCKET_STRING];

    //  We use this socket in raw mode, to accept a connection and send nothing
    void *stream = test_context_socket (ZMQ_STREAM);

    int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (stream, ZMQ_LINGER, &zero, sizeof (zero)));

    bind_loopback_ipv4 (stream, my_endpoint, sizeof my_endpoint);

    //  We'll be using this socket to test TCP stream handshake timeout
    void *dealer = test_context_socket (ZMQ_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer, ZMQ_LINGER, &zero, sizeof (zero)));
    int val, tenth = 100;
    size_t vsize = sizeof (val);

    // check for the expected default handshake timeout value - 30 sec
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (dealer, ZMQ_HANDSHAKE_IVL, &val, &vsize));
    TEST_ASSERT_EQUAL (sizeof (val), vsize);
    TEST_ASSERT_EQUAL_INT (30000, val);
    // make handshake timeout faster - 1/10 sec
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer, ZMQ_HANDSHAKE_IVL, &tenth, sizeof (tenth)));
    vsize = sizeof (val);
    // make sure zmq_setsockopt changed the value
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (dealer, ZMQ_HANDSHAKE_IVL, &val, &vsize));
    TEST_ASSERT_EQUAL (sizeof (val), vsize);
    TEST_ASSERT_EQUAL_INT (tenth, val);

    //  Create and connect a socket for collecting monitor events on dealer
    void *dealer_mon = test_context_socket (ZMQ_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_socket_monitor (
      dealer, "inproc://monitor-dealer",
      ZMQ_EVENT_CONNECTED | ZMQ_EVENT_DISCONNECTED | ZMQ_EVENT_ACCEPTED));

    //  Connect to the inproc endpoint so we'll get events
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_connect (dealer_mon, "inproc://monitor-dealer"));

    // connect dealer socket to non-sending stream socket
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (dealer, my_endpoint));

    // we should get ZMQ_EVENT_CONNECTED and then ZMQ_EVENT_DISCONNECTED
    int event = get_monitor_event (dealer_mon, NULL, NULL);
    TEST_ASSERT_EQUAL_INT (ZMQ_EVENT_CONNECTED, event);
    event = get_monitor_event (dealer_mon, NULL, NULL);
    TEST_ASSERT_EQUAL_INT (ZMQ_EVENT_DISCONNECTED, event);

    test_context_socket_close (dealer);
    test_context_socket_close (dealer_mon);
    test_context_socket_close (stream);
}

int ZMQ_CDECL main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_handshake_timeout_accept);
    RUN_TEST (test_stream_handshake_timeout_connect);
    return UNITY_END ();
}
