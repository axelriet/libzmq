/* SPDX-License-Identifier: MPL-2.0 */

#define __STDC_LIMIT_MACROS // to define SIZE_MAX with older compilers
#include "testutil.hpp"
#include "testutil_unity.hpp"

void setUp ()
{
}

void tearDown ()
{
}

void handler (int timer_id_, void *arg_)
{
    (void) timer_id_; //  Stop 'unused' compiler warnings
    *(static_cast<bool *> (arg_)) = true;
}

int sleep_and_execute (void *timers_)
{
    int timeout = zmq_timers_timeout (timers_);

    //  Sleep methods are inaccurate, so we sleep in a loop until time arrived
    while (timeout > 0) {
        msleep (timeout);
        timeout = zmq_timers_timeout (timers_);
    }

    return zmq_timers_execute (timers_);
}

void test_null_timer_pointers ()
{
    void *timers = NULL;

    TEST_ASSERT_FAILURE_ERRNO (EFAULT, zmq_timers_destroy (&timers));

//  TODO this currently triggers an access violation
#if 0
  TEST_ASSERT_FAILURE_ERRNO(EFAULT, zmq_timers_destroy (NULL));
#endif

    const size_t dummy_interval = 100;
    const int dummy_timer_id = 1;

    TEST_ASSERT_FAILURE_ERRNO (
      EFAULT, zmq_timers_add (timers, dummy_interval, &handler, NULL));
    TEST_ASSERT_FAILURE_ERRNO (
      EFAULT, zmq_timers_add (&timers, dummy_interval, &handler, NULL));

    TEST_ASSERT_FAILURE_ERRNO (EFAULT,
                               zmq_timers_cancel (timers, dummy_timer_id));
    TEST_ASSERT_FAILURE_ERRNO (EFAULT,
                               zmq_timers_cancel (&timers, dummy_timer_id));

    TEST_ASSERT_FAILURE_ERRNO (
      EFAULT, zmq_timers_set_interval (timers, dummy_timer_id, dummy_interval));
    TEST_ASSERT_FAILURE_ERRNO (
      EFAULT,
      zmq_timers_set_interval (&timers, dummy_timer_id, dummy_interval));

    TEST_ASSERT_FAILURE_ERRNO (EFAULT,
                               zmq_timers_reset (timers, dummy_timer_id));
    TEST_ASSERT_FAILURE_ERRNO (EFAULT,
                               zmq_timers_reset (&timers, dummy_timer_id));

    TEST_ASSERT_FAILURE_ERRNO (EFAULT, zmq_timers_timeout (timers));
    TEST_ASSERT_FAILURE_ERRNO (EFAULT, zmq_timers_timeout (&timers));

    TEST_ASSERT_FAILURE_ERRNO (EFAULT, zmq_timers_execute (timers));
    TEST_ASSERT_FAILURE_ERRNO (EFAULT, zmq_timers_execute (&timers));
}

void test_corner_cases ()
{
    void *timers = zmq_timers_new ();
    TEST_ASSERT_NOT_NULL (timers);

    const size_t dummy_interval = SIZE_MAX;
    const int dummy_timer_id = 1;

    //  attempt to cancel non-existent timer
    TEST_ASSERT_FAILURE_ERRNO (EINVAL,
                               zmq_timers_cancel (timers, dummy_timer_id));

    //  attempt to set interval of non-existent timer
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zmq_timers_set_interval (timers, dummy_timer_id, dummy_interval));

    //  attempt to reset non-existent timer
    TEST_ASSERT_FAILURE_ERRNO (EINVAL,
                               zmq_timers_reset (timers, dummy_timer_id));

    //  attempt to add NULL handler
    TEST_ASSERT_FAILURE_ERRNO (
      EFAULT, zmq_timers_add (timers, dummy_interval, NULL, NULL));

    const int timer_id = TEST_ASSERT_SUCCESS_ERRNO (
      zmq_timers_add (timers, dummy_interval, handler, NULL));

    //  attempt to cancel timer twice
    //  TODO should this case really be an error? canceling twice could be allowed
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_cancel (timers, timer_id));

    TEST_ASSERT_FAILURE_ERRNO (EINVAL, zmq_timers_cancel (timers, timer_id));

    //  timeout without any timers active
    TEST_ASSERT_FAILURE_ERRNO (EINVAL, zmq_timers_timeout (timers));

    //  cleanup
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_destroy (&timers));
}

void test_timers ()
{
    void *timers = zmq_timers_new ();
    TEST_ASSERT_NOT_NULL (timers);

    bool timer_invoked = false;

    const unsigned long full_timeout = 100;
    void *const stopwatch = zmq_stopwatch_start ();

    const int timer_id = TEST_ASSERT_SUCCESS_ERRNO (
      zmq_timers_add (timers, full_timeout, handler, &timer_invoked));

    //  Timer should not have been invoked yet
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_execute (timers));

    if (zmq_stopwatch_intermediate (stopwatch) < full_timeout) {
        TEST_ASSERT_FALSE (timer_invoked);
    }

    //  Wait half the time and check again
    long timeout = TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_timeout (timers));
    msleep (timeout / 2);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_execute (timers));
    if (zmq_stopwatch_intermediate (stopwatch) < full_timeout) {
        TEST_ASSERT_FALSE (timer_invoked);
    }

    // Wait until the end
    TEST_ASSERT_SUCCESS_ERRNO (sleep_and_execute (timers));
    TEST_ASSERT_TRUE (timer_invoked);
    timer_invoked = false;

    //  Wait half the time and check again
    timeout = TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_timeout (timers));
    msleep (timeout / 2);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_execute (timers));
    if (zmq_stopwatch_intermediate (stopwatch) < 2 * full_timeout) {
        TEST_ASSERT_FALSE (timer_invoked);
    }

    // Reset timer and wait half of the time left
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_reset (timers, timer_id));
    msleep (timeout / 2);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_execute (timers));
    if (zmq_stopwatch_stop (stopwatch) < 2 * full_timeout) {
        TEST_ASSERT_FALSE (timer_invoked);
    }

    // Wait until the end
    TEST_ASSERT_SUCCESS_ERRNO (sleep_and_execute (timers));
    TEST_ASSERT_TRUE (timer_invoked);
    timer_invoked = false;

    // reschedule
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_set_interval (timers, timer_id, 50));
    TEST_ASSERT_SUCCESS_ERRNO (sleep_and_execute (timers));
    TEST_ASSERT_TRUE (timer_invoked);
    timer_invoked = false;

    // cancel timer
    timeout = TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_timeout (timers));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_cancel (timers, timer_id));
    msleep (timeout * 2);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_execute (timers));
    TEST_ASSERT_FALSE (timer_invoked);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_timers_destroy (&timers));
}

int ZMQ_CDECL main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_timers);
    RUN_TEST (test_null_timer_pointers);
    RUN_TEST (test_corner_cases);
    return UNITY_END ();
}
