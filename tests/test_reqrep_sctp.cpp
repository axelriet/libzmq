/* SPDX-License-Identifier: MPL-2.0 */

#include <string>
#include <sstream>

#include "testutil.hpp"
#include "testutil_unity.hpp"

SETUP_TEARDOWN_TESTCONTEXT

void test_reqrep_sctp ()
{
    std::string endpoint = "sctp://localhost:5780";

    void *sb = test_context_socket (ZMQ_DEALER);
    int rc = zmq_bind (sb, endpoint.c_str ());
    if (rc < 0 && (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT)) {
        test_context_socket_close_zero_linger (sb);
        TEST_IGNORE_MESSAGE ("SCTP not supported");
    }
    TEST_ASSERT_SUCCESS_ERRNO (rc);

    void *sc = test_context_socket (ZMQ_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sc, endpoint.c_str ()));

    bounce (sb, sc);

    test_context_socket_close_zero_linger (sc);
    test_context_socket_close_zero_linger (sb);
}

int ZMQ_CDECL main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_reqrep_sctp);
    return UNITY_END ();
}
