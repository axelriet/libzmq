/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_V1_DECODER_HPP_INCLUDED__
#define __ZMQ_V1_DECODER_HPP_INCLUDED__

#include "decoder.hpp"

namespace zmq
{
//  Decoder for ZMTP/1.0 protocol. Converts data batches into messages.

class v1_decoder_t ZMQ_FINAL : public decoder_base_t<v1_decoder_t>
{
  public:
    v1_decoder_t (size_t bufsize_, int64_t maxmsgsize_);
    ~v1_decoder_t ();

    msg_t *msg () { return &_in_progress; }

  private:
    int one_byte_size_ready (unsigned char const *);
    int eight_byte_size_ready (unsigned char const *);
    int flags_ready (unsigned char const *);
    int message_ready (unsigned char const *);

#if defined(_MSC_VER)
    __declspec (align (ZMQ_CACHELINE_SIZE)) unsigned char _tmpbuf[8];
    unsigned char _padding[ZMQ_CACHELINE_SIZE - sizeof (_tmpbuf)];
#elif defined(__GNUC__) || defined(__INTEL_COMPILER)                           \
  || (defined(__SUNPRO_C) && __SUNPRO_C >= 0x590)                              \
  || (defined(__SUNPRO_CC) && __SUNPRO_CC >= 0x590)
    unsigned char _tmpbuf[8] __attribute__ ((aligned (ZMQ_CACHELINE_SIZE)));
    unsigned char _padding[ZMQ_CACHELINE_SIZE - sizeof (_tmpbuf)];
#else
    unsigned char _tmpbuf[8];
#endif

    msg_t _in_progress;

    const int64_t _max_msg_size;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (v1_decoder_t)
};
}

#endif
