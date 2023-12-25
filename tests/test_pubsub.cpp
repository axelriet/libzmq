/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#if defined ZMQ_HAVE_OPENPGM && defined ZMQ_HAVE_WINDOWS
#include <shlobj_core.h>
#pragma comment(lib, "shell32.lib")
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

#if __cplusplus >= 201103L || defined(_MSC_VER) && _MSC_VER > 1700
#define CODEC_WORKOUT
#endif

#ifdef CODEC_WORKOUT
#include <thread>
#include <chrono>
#include <memchk.h>
#endif

#define PRIVATE_EXPERIMENT_MULTICAST "224.0.1.20"

SETUP_TEARDOWN_TESTCONTEXT

#ifdef CODEC_WORKOUT

void test_encoder_decoder (void *publisher, void *subscriber)
{
    size_t size;
    int retries = 5;
    std::atomic<bool> started (false);
    const int MAX_SIZE = 3000; // 1/2(n*(n+n)) = 4,501,500 msg

    auto sender = std::thread ([publisher, &started] () {
        //
        // Send a bunch of messages of various sizes: N messages
        // of size 0, (N-1) messages of size 1, etc. The message
        // content is filled with LOBYTE(message_size) so we can
        // test the content integrity on the receiving side.
        //

        started = true;

        for (int i = 0; i <= MAX_SIZE; i++) {
            for (int j = 0; j <= (MAX_SIZE - i); j++) {
                zmq_msg_t msg;
                TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_init_size (&msg, i));
                memset (zmq_msg_data (&msg), (int) i, i);
                TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_send (&msg, publisher, 0));
                TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_close (&msg));
            }
        }
    });

    while (!started) {
        msleep (100);
    }

    for (;;) {
        zmq_msg_t msg;
        TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_init (&msg));
        int rc = zmq_msg_recv (&msg, subscriber, ZMQ_DONTWAIT);

        if (rc == -1) {
            //
            // Exit if we don't receive anything for .5s
            //

            if (zmq_errno () == EAGAIN) {
                if (retries--) {
                    msleep (100);
                    continue;
                }
                break;
            } else {
                TEST_ASSERT_SUCCESS_ERRNO (-1);
            }

            retries = 5;
        }

        size = zmq_msg_size (&msg);
        TEST_ASSERT_TRUE_MESSAGE (memchk ((char*)zmq_msg_data (&msg), (int) size, size)
                                    == 0,
                                  "Unexpected message content!");
        TEST_ASSERT_SUCCESS_ERRNO (zmq_msg_close (&msg));
    }

    sender.join ();
}

#endif

void test (const char *address)
{
    size_t len = MAX_SOCKET_STRING;
    char my_endpoint[MAX_SOCKET_STRING]{};

    //  Create a publisher
    void *publisher = test_context_socket (ZMQ_PUB);

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

    if (zmq_bind (publisher, address) == -1) {
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

    //  Subscribe to all messages.
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "", 0));

    //  Wait a bit till the subscription gets to the publisher
    msleep (SETTLE_TIME);

    //  Send three messages
    send_string_expect_success (publisher, "test1", 0);
    send_string_expect_success (publisher, "test2", 0);
    send_string_expect_success (publisher, "test3", 0);

    //  Receive the messages
    recv_string_expect_success (subscriber, "test1", 0);
    recv_string_expect_success (subscriber, "test2", 0);
    recv_string_expect_success (subscriber, "test3", 0);

#ifdef CODEC_WORKOUT
    test_encoder_decoder (publisher, subscriber);
#endif

    //  Clean up.
    test_context_socket_close (publisher);
    test_context_socket_close (subscriber);
}

void test_norm ()
{
#if defined ZMQ_HAVE_NORM
    test ("norm://" PRIVATE_EXPERIMENT_MULTICAST ":6210");
#else
    TEST_IGNORE_MESSAGE ("libzmq without NORM, ignoring test.");
#endif
}

void test_xnorm ()
{
#if defined ZMQ_HAVE_NORM
    test ("xnorm://" PRIVATE_EXPERIMENT_MULTICAST ":6211");
#else
    TEST_IGNORE_MESSAGE ("libzmq without NORM, ignoring test.");
#endif
}

#if defined ZMQ_HAVE_OPENPGM
#if defined ZMQ_HAVE_WINDOWS
int GetAdapterIpAddress (
  _Out_writes_bytes_ (ipAddressBufferSizeBytes) char *ipAddressBuffer,
  size_t ipAddressBufferSizeBytes,
  _Out_writes_bytes_opt_ (adapterNameBufferSizeBytes) char *adapterNameBuffer =
    nullptr,
  size_t adapterNameBufferSizeBytes = 0)
{
    *ipAddressBuffer = 0;

    if ((adapterNameBuffer != nullptr) && (adapterNameBufferSizeBytes > 0)) {
        *adapterNameBuffer = 0;
    }

    if (ipAddressBufferSizeBytes < 8) {
        return -1;
    }

    //
    // Alocate memory for up to 8 adapters
    //

    ULONG bufferSizeBytes = 8 * sizeof (IP_ADAPTER_INFO);
    void *buffer = malloc (bufferSizeBytes);
    PIP_ADAPTER_INFO pAdapterInfo = (IP_ADAPTER_INFO *) buffer;

    //
    // Retrieve information for all adapters
    //

    int result = GetAdaptersInfo (pAdapterInfo, &bufferSizeBytes);

    if (result == ERROR_BUFFER_OVERFLOW) {
        //
        // Buffer was too small
        //

        free (buffer);
        buffer = malloc (bufferSizeBytes);

        if (!buffer) {
            return -1;
        }

        //
        // Try again
        //

        pAdapterInfo = (IP_ADAPTER_INFO *) buffer;
        result = GetAdaptersInfo (pAdapterInfo, &bufferSizeBytes);
    }

    if (result != ERROR_SUCCESS) {
        free (buffer);
        return -1;
    }

    //
    // Find the best(?) Ethernet adapter
    //

    result = -1;
    int bestAdapterScore = 0;

    while (pAdapterInfo && bestAdapterScore < 4) {
        if ((pAdapterInfo->Type == IF_TYPE_ETHERNET_CSMACD)
            || (pAdapterInfo->Type == IF_TYPE_IEEE80211)) {
            char c = pAdapterInfo->IpAddressList.IpAddress.String[0];

            if ((c != 0) && (c != '0')) {
                int currentAdapterScore = 1; // Has IP address!

                if (pAdapterInfo->Type == IF_TYPE_ETHERNET_CSMACD) {
                    ++currentAdapterScore; // Is Ethernet?
                }

                c = pAdapterInfo->GatewayList.IpAddress.String[0];

                if ((c != 0) && (c != '0')) {
                    ++currentAdapterScore; // Has gateway?
                }

                c = pAdapterInfo->DhcpServer.IpAddress.String[0];

                if ((c != 0) && (c != '0')) {
                    ++currentAdapterScore; // Has DHCP server?
                }

                if (currentAdapterScore > bestAdapterScore) {
                    result = 0;
                    bestAdapterScore = currentAdapterScore;

                    memset (ipAddressBuffer, 0, ipAddressBufferSizeBytes);
                    strcpy_s (ipAddressBuffer, ipAddressBufferSizeBytes,
                              pAdapterInfo->IpAddressList.IpAddress.String);

                    if ((adapterNameBuffer != nullptr)
                        && (adapterNameBufferSizeBytes > 0)) {
                        memset (adapterNameBuffer, 0,
                                adapterNameBufferSizeBytes);
                        strncpy_s (adapterNameBuffer,
                                   adapterNameBufferSizeBytes,
                                   pAdapterInfo->Description,
                                   _countof (pAdapterInfo->Description));
                    }
                }
            }
        }

        pAdapterInfo = pAdapterInfo->Next;
    }

    free (buffer);
    return result;
}
#else
#define NETWORK_ADAPTER "eth0"
#endif
#endif

void test_epgm ()
{
#if defined ZMQ_HAVE_OPENPGM
#ifdef ZMQ_HAVE_WINDOWS
    char network[64];
    char ip_address[16];
    TEST_ASSERT_EQUAL_INT (
      0, GetAdapterIpAddress (ip_address, _countof (ip_address)));
    sprintf_s (network, _countof (network),
               "epgm://%s;" PRIVATE_EXPERIMENT_MULTICAST ":6211", ip_address);
    test (network);
#else
#ifdef NETWORK_ADAPTER
    test ("epgm://" NETWORK_ADAPTER ";" PRIVATE_EXPERIMENT_MULTICAST ":6211");
#else
    TEST_IGNORE_MESSAGE (
      "libzmq with OpenPGM, but NETWORK_ADAPTER wasn't set, ignoring test.");
#endif
#endif
#else
    TEST_IGNORE_MESSAGE ("libzmq without OpenPGM, ignoring test.");
#endif
}

void test_pgm ()
{
#if defined ZMQ_HAVE_OPENPGM
#ifdef ZMQ_HAVE_WINDOWS
    if (!IsUserAnAdmin ()) {
        TEST_IGNORE_MESSAGE (
          "libzmq with OpenPGM, but user is not an admin, ignoring test.");
    } else {
        char network[64];
        char ip_address[16];
        TEST_ASSERT_EQUAL_INT (
          0, GetAdapterIpAddress (ip_address, _countof (ip_address)));
        sprintf_s (network, _countof (network),
                   "pgm://%s;" PRIVATE_EXPERIMENT_MULTICAST ":6212",
                   ip_address);
        test (network);
    }
#else
#ifdef NETWORK_ADAPTER
    test ("pgm://" NETWORK_ADAPTER ";" PRIVATE_EXPERIMENT_MULTICAST ":6212");
#else
    TEST_IGNORE_MESSAGE (
      "libzmq with OpenPGM, but NETWORK_ADAPTER wasn't set, ignoring test.");
#endif
#endif
#else
    TEST_IGNORE_MESSAGE ("libzmq without OpenPGM, ignoring test.");
#endif
}

void test_inproc ()
{
    test ("inproc://test_pubsub");
}

void test_tcp ()
{
    test ("tcp://localhost:6213");
}

void test_ipc ()
{
#if defined ZMQ_HAVE_IPC
    test ("ipc://test_pubsub");
#else
    TEST_IGNORE_MESSAGE ("libzmq without IPC, ignoring test.");
#endif
}

void test_ws ()
{
#if defined ZMQ_HAVE_WS
    test ("ws://localhost:6215");
#else
    TEST_IGNORE_MESSAGE ("libzmq without WebSockets, ignoring test.");
#endif
}

void test_wss ()
{
#if defined ZMQ_HAVE_WSS
    test ("wss://localhost:6216");
#else
    TEST_IGNORE_MESSAGE ("libzmq without WSS WebSockets, ignoring test.");
#endif
}

void test_vmci ()
{
#if defined ZMQ_HAVE_VMCI
    test ("vmci://*:*");
#else
    TEST_IGNORE_MESSAGE ("libzmq without VMCI, ignoring test.");
#endif
}

void test_vsock ()
{
#if defined ZMQ_HAVE_VSOCK
    test ("vsock://2:2222");
#else
    TEST_IGNORE_MESSAGE ("libzmq without VSOCK, ignoring test.");
#endif
}

void test_hvsocket ()
{
#if defined ZMQ_HAVE_HVSOCKET
    test (
      "hyperv://e0e16197-dd56-4a10-9195-5ee7a155a838:*"); // Loopback, any port.
    test (
      "hyperv://loopback:3333"); // Loopback, specific port with VSOCK template.

    //
    // The following tests are machine and/or VM specifics are are meant
    // to illustrate the possible connection string formats that can be used.
    //
    // Also note that Hyper-V requires the caller to be admin.
    //

#if 0

    test ("hyperv://0:*"); // VM/container index (first one), any port.
    test ("hyperv://0:6666"); // VM/container index (first one), specific port with VSOCK template.
    test ("hyperv://0:44622b22-7665-4499-b2e3-16d5f9bc14d3"); // VM/container index (first one), explicit (registered) service id.
    test ("hyperv://0:NMBus"); // VM/container index (first one), explicit (registered) service id by "ElementName"

    test ("hyperv://WinDev2311Eval:*"); // Symbolic VM/container name, any port.
    test ("hyperv://WinDev2311Eval:6666"); // Symbolic VM/container name, specific port with VSOCK template.
    test ("hyperv://WinDev2311Eval:44622b22-7665-4499-b2e3-16d5f9bc14d3"); // Symbolic VM/container name, explicit (registered) service id.
    test ("hyperv://WinDev2311Eval:NMBus"); // Symbolic VM/container name, explicit (registered) service id by "ElementName"

    test ("hyperv://af5f35e3-fd7a-4573-9449-e47223939979:*"); // Explicit VM/container id, any port.
    test ("hyperv://af5f35e3-fd7a-4573-9449-e47223939979:6666"); // Explicit VM/container id, specific port with VSOCK template.
    test ("hyperv://af5f35e3-fd7a-4573-9449-e47223939979:44622b22-7665-4499-b2e3-16d5f9bc14d3"); // Explicit VM/container id, explicit (registered) service id.
    test ("hyperv://af5f35e3-fd7a-4573-9449-e47223939979:NMBus"); // Explicit VM/container id, explicit (registered) service id by "ElementName"

#endif

    //
    // The address parser also understands the following symbolic addresses
    //
    //      broadcast - all partitions (VMs/containers)
    //      children - all child partitions (VMs/containers)
    //      loopback - as demonstraded above. Moral equivalent to localhost.
    //      parent - the parent partition (host OS)
    //      silohost - the silo host partition (utility VM)
    //
    // Assuming a ServiceId has been registered with the name "NMBus" on both
    // the host and the guest OSes, and the VM name is "SomeVM" the following
    // connection string can be used:
    //
    // From the host:  hv://SomeVM:NMBus
    // From the guest: hv://parent:NMBus
    //
    // Using a numeric index as address carries the risk of collision with
    // an actual VM name that might happen to be a number. This variant is
    // not meant for production but in test environments it's useful to be
    // able to specify a VM/container by index, e.g. the first one.
    //
    // The port number might also conflight with a registered service id.
    //

#else
    TEST_IGNORE_MESSAGE ("libzmq without HVSOCKET, ignoring test.");
#endif
}

int ZMQ_CDECL main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();

    RUN_TEST (test_inproc);
    RUN_TEST (test_tcp);

    RUN_TEST (test_ipc);

    RUN_TEST (test_pgm);
    RUN_TEST (test_epgm);
    RUN_TEST (test_norm);
    RUN_TEST (test_xnorm);

    RUN_TEST (test_vmci);
    RUN_TEST (test_vsock);
    RUN_TEST (test_hvsocket);

    RUN_TEST (test_ws);
    RUN_TEST (test_wss);

    return UNITY_END ();
}
