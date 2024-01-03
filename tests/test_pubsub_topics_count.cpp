/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include <string.h>

#define PRIVATE_EXPERIMENT_MULTICAST "224.0.1.20"

SETUP_TEARDOWN_TESTCONTEXT

void settle_subscriptions (void *skt)
{
    //  To kick the application thread, do a dummy getsockopt - users here
    //  should use the monitor and the other sockets in a poll.
    unsigned long int dummy;
    size_t dummy_size = sizeof (dummy);
    msleep (SETTLE_TIME);
    zmq_getsockopt (skt, ZMQ_EVENTS, &dummy, &dummy_size);
}

int get_subscription_count (void *skt)
{
    int num_subs = 0;
    size_t num_subs_len = sizeof (num_subs);

    settle_subscriptions (skt);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (skt, ZMQ_TOPICS_COUNT, &num_subs, &num_subs_len));

    return num_subs;
}

void _test_independent_topic_prefixes (const char *endpoint_,
                                       bool multicast_ = false)
{
    //  Create a publisher
    void *publisher = test_context_socket (ZMQ_PUB);

    size_t len = MAX_SOCKET_STRING;
    char my_endpoint[MAX_SOCKET_STRING];

    // Bind publisher. It is possible that the
    // library is built to support some transport that
    // is not available on the test system. That is OK,
    // for example the test system cannot possibly run
    // in a Hyper-V hosted VM *and* a KVM-hosted VM at
    // the same time but the library can be built with
    // support for both Hyper-V and VSock transports,
    // so it is OK to ignore the test if the AF isn't
    // supported _on the test system_.
    //

    if (zmq_bind (publisher, endpoint_) == -1) {
        test_context_socket_close (publisher);
        if (zmq_errno () == EAFNOSUPPORT) {
            TEST_IGNORE_MESSAGE (
              "Address family not supported on this system, ignoring test.");
        } else {
            test_assert_success_message_errno_helper (
              -1, NULL, "zmq_bind (publisher, address)", __LINE__);
        }
    }

    //  Retrieve the effective endpoint
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (publisher, ZMQ_LAST_ENDPOINT, my_endpoint, &len));

    //  Create a subscriber
    void *subscriber = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (subscriber, my_endpoint));

    //  Subscribe to 3 topics
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      subscriber, ZMQ_SUBSCRIBE, "topicprefix1", strlen ("topicprefix1")));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      subscriber, ZMQ_SUBSCRIBE, "topicprefix2", strlen ("topicprefix2")));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      subscriber, ZMQ_SUBSCRIBE, "topicprefix3", strlen ("topicprefix3")));
    TEST_ASSERT_EQUAL_INT (3, get_subscription_count (subscriber));
    TEST_ASSERT_EQUAL_INT (multicast_ ? 1 : 3,
                           get_subscription_count (publisher));

    // Remove first subscription and check subscriptions went 3 -> 2
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      subscriber, ZMQ_UNSUBSCRIBE, "topicprefix3", strlen ("topicprefix3")));
    TEST_ASSERT_EQUAL_INT (2, get_subscription_count (subscriber));
    TEST_ASSERT_EQUAL_INT (multicast_ ? 1 : 2,
                           get_subscription_count (publisher));

    // Remove other 2 subscriptions and check we're back to 0 subscriptions
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      subscriber, ZMQ_UNSUBSCRIBE, "topicprefix1", strlen ("topicprefix1")));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      subscriber, ZMQ_UNSUBSCRIBE, "topicprefix2", strlen ("topicprefix2")));
    TEST_ASSERT_EQUAL_INT (0, get_subscription_count (subscriber));
    TEST_ASSERT_EQUAL_INT (multicast_ ? 1 : 0,
                           get_subscription_count (publisher));

    //  Clean up.
    test_context_socket_close (publisher);
    test_context_socket_close (subscriber);
}

void _test_nested_topic_prefixes (const char *endpoint_,
                                  bool multicast_ = false)
{
    //  Create a publisher
    void *publisher = test_context_socket (ZMQ_PUB);

    size_t len = MAX_SOCKET_STRING;
    char my_endpoint[MAX_SOCKET_STRING];

    // Bind publisher. It is possible that the
    // library is built to support some transport that
    // is not available on the test system. That is OK,
    // for example the test system cannot possibly run
    // in a Hyper-V hosted VM *and* a KVM-hosted VM at
    // the same time but the library can be built with
    // support for both Hyper-V and VSock transports,
    // so it is OK to ignore the test if the AF isn't
    // supported _on the test system_.
    //

    if (zmq_bind (publisher, endpoint_) == -1) {
        test_context_socket_close (publisher);
        if (zmq_errno () == EAFNOSUPPORT) {
            TEST_IGNORE_MESSAGE (
              "Address family not supported on this system, ignoring test.");
        } else {
            test_assert_success_message_errno_helper (
              -1, NULL, "zmq_bind (publisher, address)", __LINE__);
        }
    }

    //  Retrieve the effective endpoint
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (publisher, ZMQ_LAST_ENDPOINT, my_endpoint, &len));

    //  Create a subscriber
    void *subscriber = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (subscriber, my_endpoint));

    //  Subscribe to 3 (nested) topics
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "a", strlen ("a")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "ab", strlen ("ab")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "abc", strlen ("abc")));

    // Even if the subscriptions are nested one into the other, the number of subscriptions
    // received on the subscriber/publisher socket will be 3:
    TEST_ASSERT_EQUAL_INT (3, get_subscription_count (subscriber));
    TEST_ASSERT_EQUAL_INT (multicast_ ? 1 : 3,
                           get_subscription_count (publisher));

    //  Subscribe to other 3 (nested) topics
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "xyz", strlen ("xyz")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "xy", strlen ("xy")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "x", strlen ("x")));

    TEST_ASSERT_EQUAL_INT (6, get_subscription_count (subscriber));
    TEST_ASSERT_EQUAL_INT (multicast_ ? 1 : 6,
                           get_subscription_count (publisher));

    //  Clean up.
    test_context_socket_close (publisher);
    test_context_socket_close (subscriber);
}

void test_independent_and_nested_topic_prefixes_inproc ()
{
    _test_independent_topic_prefixes ("inproc://test_pubsub_1");
    _test_nested_topic_prefixes ("inproc://test_pubsub_2");
}

void test_independent_and_nested_topic_prefixes_tcp ()
{
    _test_independent_topic_prefixes ("tcp://localhost:7213");
    _test_nested_topic_prefixes ("tcp://localhost:7214");
}

void test_independent_and_nested_topic_prefixes_ipc ()
{
#if defined ZMQ_HAVE_IPC
    _test_independent_topic_prefixes ("ipc://test_pubsub_3");
    _test_nested_topic_prefixes ("ipc://test_pubsub_4");
#else
    TEST_IGNORE_MESSAGE ("libzmq without IPC, ignoring test.");
#endif
}

void test_independent_and_nested_topic_prefixes_ws ()
{
#if defined ZMQ_HAVE_WS
    _test_independent_topic_prefixes ("ws://localhost:7215");
    _test_nested_topic_prefixes ("ws://localhost:7215");
#else
    TEST_IGNORE_MESSAGE ("libzmq without WebSockets, ignoring test.");
#endif
}

void test_independent_and_nested_topic_prefixes_wss ()
{
#if defined ZMQ_HAVE_WSS
    _test_independent_topic_prefixes ("wss://localhost:7216");
    _test_nested_topic_prefixes ("wss://localhost:7216");
#else
    TEST_IGNORE_MESSAGE ("libzmq without WSS WebSockets, ignoring test.");
#endif
}

void test_independent_and_nested_topic_prefixes_vmci ()
{
#if defined ZMQ_HAVE_VMCI
    _test_independent_topic_prefixes ("vmci://*:*");
    _test_nested_topic_prefixes ("vmci://*:*");
#else
    TEST_IGNORE_MESSAGE ("libzmq without VMCI, ignoring test.");
#endif
}

void test_independent_and_nested_topic_prefixes_vsock ()
{
#if defined ZMQ_HAVE_VSOCK
    _test_independent_topic_prefixes ("vsock://2:7777");
    _test_nested_topic_prefixes ("vsock://2:7778");
#else
    TEST_IGNORE_MESSAGE ("libzmq without VSOCK, ignoring test.");
#endif
}

void test_independent_and_nested_topic_prefixes_hvsocket ()
{
#if defined ZMQ_HAVE_HVSOCKET
    _test_independent_topic_prefixes ("hyperv://loopback:8888");
    _test_nested_topic_prefixes ("hyperv://loopback:8889");
#else
    TEST_IGNORE_MESSAGE ("libzmq without HVSOCKET, ignoring test.");
#endif
}

void test_independent_and_nested_topic_prefixes_norm ()
{
#if defined ZMQ_HAVE_NORM
    _test_independent_topic_prefixes (
      "norm://" PRIVATE_EXPERIMENT_MULTICAST ":8310", true);
    _test_nested_topic_prefixes ("norm://" PRIVATE_EXPERIMENT_MULTICAST ":8311",
                                 true);
#else
    TEST_IGNORE_MESSAGE ("libzmq without NORM, ignoring test.");
#endif
}

int ZMQ_CDECL main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();

    RUN_TEST (test_independent_and_nested_topic_prefixes_inproc);
    RUN_TEST (test_independent_and_nested_topic_prefixes_tcp);
    RUN_TEST (test_independent_and_nested_topic_prefixes_ipc);
    RUN_TEST (test_independent_and_nested_topic_prefixes_ws);
    RUN_TEST (test_independent_and_nested_topic_prefixes_wss);
    RUN_TEST (test_independent_and_nested_topic_prefixes_vmci);
    RUN_TEST (test_independent_and_nested_topic_prefixes_vsock);
    RUN_TEST (test_independent_and_nested_topic_prefixes_hvsocket);
    RUN_TEST (test_independent_and_nested_topic_prefixes_norm);

    return UNITY_END ();
}
