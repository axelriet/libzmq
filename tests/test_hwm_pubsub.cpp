/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <string.h>

#define PRIVATE_EXPERIMENT_MULTICAST "224.0.1.20"

// NOTE: on OSX the endpoint returned by ZMQ_LAST_ENDPOINT may be quite long,
//       ensure we have extra space for that:
#define SOCKET_STRING_LEN (MAX_SOCKET_STRING * 4)

SETUP_TEARDOWN_TESTCONTEXT

int test_defaults (int send_hwm_, int msg_cnt_, const char *endpoint_)
{
    char pub_endpoint[SOCKET_STRING_LEN];
    size_t len = sizeof (pub_endpoint);

    // Set up and bind XPUB socket
    void *pub_socket = test_context_socket (ZMQ_XPUB);

    //
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

    if (zmq_bind (pub_socket, endpoint_) == -1) {
        test_context_socket_close (pub_socket);
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
      zmq_getsockopt (pub_socket, ZMQ_LAST_ENDPOINT, pub_endpoint, &len));

    // Set up and connect SUB socket
    void *sub_socket = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub_socket, pub_endpoint));

    //set a hwm on publisher
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub_socket, ZMQ_SNDHWM, &send_hwm_, sizeof (send_hwm_)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub_socket, ZMQ_SUBSCRIBE, 0, 0));

    // Wait before starting TX operations till 1 subscriber has subscribed
    // (in this test there's 1 subscriber only)
    const char subscription_to_all_topics[] = {1, 0};
    recv_string_expect_success (pub_socket, subscription_to_all_topics, 0);

    // Send until we reach "mute" state
    int send_count = 0;
    while (send_count < msg_cnt_
           && zmq_send (pub_socket, "test message", 13, ZMQ_DONTWAIT) == 13)
        ++send_count;

    TEST_ASSERT_EQUAL_INT (send_hwm_, send_count);
    msleep (SETTLE_TIME);

    // Now receive all sent messages
    int recv_count = 0;
    char dummybuff[64];
    while (13 == zmq_recv (sub_socket, &dummybuff, 64, ZMQ_DONTWAIT)) {
        ++recv_count;
    }

    TEST_ASSERT_EQUAL_INT (send_hwm_, recv_count);

    // Clean up
    test_context_socket_close (sub_socket);
    test_context_socket_close (pub_socket);

    return recv_count;
}

int receive (void *socket_, int *is_termination_)
{
    int recv_count = 0;
    *is_termination_ = 0;

    // Now receive all sent messages
    char buffer[255];
    int len;
    while ((len = zmq_recv (socket_, buffer, sizeof (buffer), 0)) >= 0) {
        ++recv_count;

        if (len == 3 && strncmp (buffer, "end", len) == 0) {
            *is_termination_ = 1;
            return recv_count;
        }
    }

    return recv_count;
}

int test_blocking (int send_hwm_, int msg_cnt_, const char *endpoint_)
{
    char pub_endpoint[SOCKET_STRING_LEN];
    size_t len = sizeof (pub_endpoint);

    // Set up bind socket
    void *pub_socket = test_context_socket (ZMQ_XPUB);

    //
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

    if (zmq_bind (pub_socket, endpoint_) == -1) {
        test_context_socket_close (pub_socket);
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
      zmq_getsockopt (pub_socket, ZMQ_LAST_ENDPOINT, pub_endpoint, &len));

    // Set up connect socket
    void *sub_socket = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub_socket, pub_endpoint));

    //set a hwm on publisher
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub_socket, ZMQ_SNDHWM, &send_hwm_, sizeof (send_hwm_)));
    int wait = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub_socket, ZMQ_XPUB_NODROP, &wait, sizeof (wait)));
    int timeout_ms = 10;
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (
      sub_socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub_socket, ZMQ_SUBSCRIBE, 0, 0));

    // Wait before starting TX operations till 1 subscriber has subscribed
    // (in this test there's 1 subscriber only)
    const uint8_t subscription_to_all_topics[] = {1};
    recv_array_expect_success (pub_socket, subscription_to_all_topics, 0);

    // Send until we block
    int send_count = 0;
    int recv_count = 0;
    int blocked_count = 0;
    int is_termination = 0;
    while (send_count < msg_cnt_) {
        const int rc = zmq_send (pub_socket, NULL, 0, ZMQ_DONTWAIT);
        if (rc == 0) {
            ++send_count;
        } else if (-1 == rc) {
            // if the PUB socket blocks due to HWM, zmq_errno () should be EAGAIN:
            blocked_count++;
            TEST_ASSERT_FAILURE_ERRNO (EAGAIN, -1);
            recv_count += receive (sub_socket, &is_termination);
        }
    }

    // if send_hwm_ < msg_cnt_, we should block at least once:
    char counts_string[128];
    snprintf (counts_string, sizeof counts_string - 1,
              "sent = %i, received = %i", send_count, recv_count);
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE (0, blocked_count, counts_string);

    // dequeue SUB socket again, to make sure XPUB has space to send the termination message
    recv_count += receive (sub_socket, &is_termination);

    // send termination message
    send_string_expect_success (pub_socket, "end", 0);

    // now block on the SUB side till we get the termination message
    while (is_termination == 0)
        recv_count += receive (sub_socket, &is_termination);

    // remove termination message from the count:
    recv_count--;

    TEST_ASSERT_EQUAL_INT (send_count, recv_count);

    // Clean up
    test_context_socket_close (sub_socket);
    test_context_socket_close (pub_socket);

    return recv_count;
}

// hwm should apply to the messages that have already been received
// with hwm 11024: send 9999 msg, receive 9999, send 1100, receive 1100
void test_reset_hwm ()
{
    const int first_count = 9999;
    const int second_count = 1100;
    int hwm = 11024;
    char my_endpoint[SOCKET_STRING_LEN];

    // Set up bind socket
    void *pub_socket = test_context_socket (ZMQ_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (pub_socket, ZMQ_SNDHWM, &hwm, sizeof (hwm)));
    bind_loopback_ipv4 (pub_socket, my_endpoint, MAX_SOCKET_STRING);

    // Set up connect socket
    void *sub_socket = test_context_socket (ZMQ_SUB);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub_socket, ZMQ_RCVHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sub_socket, my_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (sub_socket, ZMQ_SUBSCRIBE, 0, 0));

    msleep (SETTLE_TIME);

    // Send messages
    int send_count = 0;
    while (send_count < first_count
           && zmq_send (pub_socket, NULL, 0, ZMQ_DONTWAIT) == 0)
        ++send_count;
    TEST_ASSERT_EQUAL_INT (first_count, send_count);

    msleep (SETTLE_TIME);

    // Now receive all sent messages
    int recv_count = 0;
    while (0 == zmq_recv (sub_socket, NULL, 0, ZMQ_DONTWAIT)) {
        ++recv_count;
    }
    TEST_ASSERT_EQUAL_INT (first_count, recv_count);

    msleep (SETTLE_TIME);

    // Send messages
    send_count = 0;
    while (send_count < second_count
           && zmq_send (pub_socket, NULL, 0, ZMQ_DONTWAIT) == 0)
        ++send_count;
    TEST_ASSERT_EQUAL_INT (second_count, send_count);

    msleep (SETTLE_TIME);

    // Now receive all sent messages
    recv_count = 0;
    while (0 == zmq_recv (sub_socket, NULL, 0, ZMQ_DONTWAIT)) {
        ++recv_count;
    }
    TEST_ASSERT_EQUAL_INT (second_count, recv_count);

    // Clean up
    test_context_socket_close (sub_socket);
    test_context_socket_close (pub_socket);
}

void test_defaults_large (const char *bind_endpoint_)
{
    // send 1000 msg on hwm 1000, receive 1000
    TEST_ASSERT_EQUAL_INT (1000, test_defaults (1000, 1000, bind_endpoint_));
}

void test_defaults_small (const char *bind_endpoint_)
{
    // send 1000 msg on hwm 100, receive 100
    TEST_ASSERT_EQUAL_INT (100, test_defaults (100, 100, bind_endpoint_));
}

void test_blocking (const char *bind_endpoint_)
{
    // send 6000 msg on hwm 2000, drops above hwm, only receive hwm:
    TEST_ASSERT_EQUAL_INT (6000, test_blocking (2000, 6000, bind_endpoint_));
}

#define DEFINE_REGULAR_TEST_CASES(name, bind_endpoint)                         \
    void test_defaults_large_##name ()                                         \
    {                                                                          \
        test_defaults_large (bind_endpoint);                                   \
    }                                                                          \
                                                                               \
    void test_defaults_small_##name ()                                         \
    {                                                                          \
        test_defaults_small (bind_endpoint);                                   \
    }                                                                          \
                                                                               \
    void test_blocking_##name ()                                               \
    {                                                                          \
        test_blocking (bind_endpoint);                                         \
    }

#define RUN_REGULAR_TEST_CASES(name)                                           \
    RUN_TEST (test_defaults_large_##name);                                     \
    RUN_TEST (test_defaults_small_##name);                                     \
    RUN_TEST (test_blocking_##name)

DEFINE_REGULAR_TEST_CASES (tcp, "tcp://127.0.0.1:*")
DEFINE_REGULAR_TEST_CASES (inproc, "inproc://a")

#if !defined(ZMQ_HAVE_GNU)
DEFINE_REGULAR_TEST_CASES (ipc, "ipc://*")
#endif

#if defined ZMQ_HAVE_WS
DEFINE_REGULAR_TEST_CASES (ws, "ws://localhost:6515")
#endif
#if defined ZMQ_HAVE_WSS
DEFINE_REGULAR_TEST_CASES (wss, "wss://localhost:6516")
#endif
#if defined ZMQ_HAVE_VMCI
DEFINE_REGULAR_TEST_CASES (vmci, "vmci://*:*")
#endif
#if defined ZMQ_HAVE_VSOCK
DEFINE_REGULAR_TEST_CASES (vsock, "vsock://2:2223")
#endif
#if defined ZMQ_HAVE_HVSOCKET
DEFINE_REGULAR_TEST_CASES (hyperv, "hyperv://loopback:3334")
#endif
#if defined ZMQ_HAVE_NORM
DEFINE_REGULAR_TEST_CASES (norm, "norm://" PRIVATE_EXPERIMENT_MULTICAST ":6310")
#endif

int ZMQ_CDECL main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();

    RUN_REGULAR_TEST_CASES (tcp);
    RUN_REGULAR_TEST_CASES (inproc);

#if !defined(ZMQ_HAVE_GNU)
    RUN_REGULAR_TEST_CASES (ipc);
#endif

#if defined ZMQ_HAVE_WS
    RUN_REGULAR_TEST_CASES (ws);
#endif
#if defined ZMQ_HAVE_WSS
    RUN_REGULAR_TEST_CASES (wss);
#endif
#if defined ZMQ_HAVE_VMCI
    RUN_REGULAR_TEST_CASES (vmci);
#endif
#if defined ZMQ_HAVE_VSOCK
    RUN_REGULAR_TEST_CASES (vsock);
#endif
#if defined ZMQ_HAVE_HVSOCKET
    RUN_REGULAR_TEST_CASES (hyperv);
#endif
#if 0 // Norm hangs on this test :(
#if defined ZMQ_HAVE_NORM
    RUN_REGULAR_TEST_CASES (norm);
#endif
#endif
    RUN_TEST (test_reset_hwm);
    return UNITY_END ();
}
