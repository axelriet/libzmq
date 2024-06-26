/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

void setUp ()
{
}

void tearDown ()
{
}

// Test vector: rfc.zeromq.org/spec:32/Z85
void test__zmq_z85_encode__valid__success ()
{
    static const size_t size = 8;
    static const size_t length = size * 5 / 4;
    static const uint8_t decoded[size] = {0x86, 0x4F, 0xD2, 0x6F,
                                          0xB5, 0x59, 0xF7, 0x5B};
    static const char expected[length + 1] = "HelloWorld";
    char out_encoded[length + 1] = {0};

    zmq_clear_errno ();
    TEST_ASSERT_NOT_NULL (zmq_z85_encode (out_encoded, decoded, size));
    TEST_ASSERT_EQUAL_STRING (expected, out_encoded);
    TEST_ASSERT_EQUAL_INT (0, zmq_errno ());
}

// Buffer length must be evenly divisible by 4 or must fail with EINVAL.
void test__zmq_z85_encode__invalid__failure (size_t size_)
{
    zmq_clear_errno ();
    TEST_ASSERT_NULL (zmq_z85_encode (NULL, NULL, size_));
    TEST_ASSERT_EQUAL_INT (EINVAL, zmq_errno ());
}

// Test vector: rfc.zeromq.org/spec:32/Z85
void test__zmq_z85_decode__valid__success ()
{
    static const size_t size = 10 * 4 / 5;
    static const uint8_t expected[size] = {0x86, 0x4F, 0xD2, 0x6F,
                                           0xB5, 0x59, 0xF7, 0x5B};
    static const char *encoded = "HelloWorld";
    uint8_t out_decoded[size] = {0};

    zmq_clear_errno ();
    TEST_ASSERT_NOT_NULL (zmq_z85_decode (out_decoded, encoded));
    TEST_ASSERT_EQUAL_INT (0, zmq_errno ());
    TEST_ASSERT_EQUAL_UINT8_ARRAY (expected, out_decoded, size);
}

// Invalid input data must fail with EINVAL.
template <size_t SIZE>
void test__zmq_z85_decode__invalid__failure (const char (&encoded_)[SIZE])
{
    uint8_t decoded[SIZE * 4 / 5 + 1];

    zmq_clear_errno ();
    TEST_ASSERT_NULL (zmq_z85_decode (decoded, encoded_));
    TEST_ASSERT_EQUAL_INT (EINVAL, zmq_errno ());
}


// call zmq_z85_encode, then zmq_z85_decode, and compare the results with the original
template <size_t SIZE>
void test__zmq_z85_encode__zmq_z85_decode__roundtrip (
  const uint8_t (&test_data_)[SIZE])
{
    char test_data_z85[SIZE * 5 / 4 + 1];
    const char *const res1 = zmq_z85_encode (test_data_z85, test_data_, SIZE);
    TEST_ASSERT_NOT_NULL (res1);

    uint8_t test_data_decoded[SIZE];
    const uint8_t *const res2 =
      zmq_z85_decode (test_data_decoded, test_data_z85);
    TEST_ASSERT_NOT_NULL (res2);

    TEST_ASSERT_EQUAL_UINT8_ARRAY (test_data_, test_data_decoded, SIZE);
}

// call zmq_z85_encode, then zmq_z85_decode, and compare the results with the original
template <size_t SIZE>
void test__zmq_z85_decode__zmq_z85_encode__roundtrip (
  const char (&test_data_)[SIZE])
{
    const size_t decoded_size = (SIZE - 1) * 4 / 5;
    uint8_t test_data_decoded[decoded_size];
    const uint8_t *const res1 = zmq_z85_decode (test_data_decoded, test_data_);
    TEST_ASSERT_NOT_NULL (res1);

    char test_data_z85[SIZE];
    const char *const res2 =
      zmq_z85_encode (test_data_z85, test_data_decoded, decoded_size);
    TEST_ASSERT_NOT_NULL (res2);

    TEST_ASSERT_EQUAL_UINT8_ARRAY (test_data_, test_data_z85, SIZE);
}

#define def_test__zmq_z85_basename(basename, name, param)                      \
    void test__zmq_z85_##basename##_##name ()                                  \
    {                                                                          \
        test__zmq_z85_##basename (param);                                      \
    }

#define def_test__zmq_z85_encode__invalid__failure(name, param)                \
    def_test__zmq_z85_basename (encode__invalid__failure, name, param)

def_test__zmq_z85_encode__invalid__failure (1, 1)
  def_test__zmq_z85_encode__invalid__failure (42, 42)

#define def_test__zmq_z85_decode__invalid__failure(name, param)                \
    def_test__zmq_z85_basename (decode__invalid__failure, name, param)

  // String length must be evenly divisible by 5 or must fail with EINVAL.
  def_test__zmq_z85_decode__invalid__failure (indivisble_by_5_multiple_chars,
                                              "01234567")
    def_test__zmq_z85_decode__invalid__failure (indivisble_by_5_one_char, "0")

  // decode invalid data with the maximum representable value
  def_test__zmq_z85_decode__invalid__failure (max, "#####")

  // decode invalid data with the minimum value beyond the limit
  // "%nSc0" is 0xffffffff
  def_test__zmq_z85_decode__invalid__failure (above_limit, "%nSc1")

  // decode invalid data with an invalid character in the range of valid
  // characters
  def_test__zmq_z85_decode__invalid__failure (char_within, "####\0047")

  // decode invalid data with an invalid character just below the range of valid
  // characters
  def_test__zmq_z85_decode__invalid__failure (char_adjacent_below, "####\0200")

  // decode invalid data with an invalid character just above the range of valid
  // characters
  def_test__zmq_z85_decode__invalid__failure (char_adjacent_above, "####\0037")

#define def_test__encode__zmq_z85_decode__roundtrip(name, param)               \
    def_test__zmq_z85_basename (encode__zmq_z85_decode__roundtrip, name, param)

    const uint8_t test_data_min[] = {0x00, 0x00, 0x00, 0x00};
const uint8_t test_data_max[] = {0xff, 0xff, 0xff, 0xff};

def_test__encode__zmq_z85_decode__roundtrip (min, test_data_min)
  def_test__encode__zmq_z85_decode__roundtrip (max, test_data_max)

#define def_test__decode__zmq_z85_encode__roundtrip(name, param)               \
    def_test__zmq_z85_basename (decode__zmq_z85_encode__roundtrip, name, param)

    const char test_data_regular[] = "r^/rM9M=rMToK)63O8dCvd9D<PY<7iGlC+{BiSnG";

def_test__decode__zmq_z85_encode__roundtrip (regular, test_data_regular)

  int ZMQ_CDECL main ()
{
    UNITY_BEGIN ();
    RUN_TEST (test__zmq_z85_encode__valid__success);
    RUN_TEST (test__zmq_z85_encode__invalid__failure_1);
    RUN_TEST (test__zmq_z85_encode__invalid__failure_42);

    RUN_TEST (test__zmq_z85_decode__valid__success);
    RUN_TEST (
      test__zmq_z85_decode__invalid__failure_indivisble_by_5_multiple_chars);
    RUN_TEST (test__zmq_z85_decode__invalid__failure_indivisble_by_5_one_char);
    RUN_TEST (test__zmq_z85_decode__invalid__failure_max);
    RUN_TEST (test__zmq_z85_decode__invalid__failure_above_limit);
    RUN_TEST (test__zmq_z85_decode__invalid__failure_char_within);
    RUN_TEST (test__zmq_z85_decode__invalid__failure_char_adjacent_below);
    RUN_TEST (test__zmq_z85_decode__invalid__failure_char_adjacent_above);

    RUN_TEST (test__zmq_z85_encode__zmq_z85_decode__roundtrip_min);
    RUN_TEST (test__zmq_z85_encode__zmq_z85_decode__roundtrip_max);

    RUN_TEST (test__zmq_z85_decode__zmq_z85_encode__roundtrip_regular);

    return UNITY_END ();
}
