/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_V1_ENCODER_HPP_INCLUDED__
#define __ZMQ_V1_ENCODER_HPP_INCLUDED__

#include "encoder.hpp"

namespace zmq
{
//  Encoder for ZMTP/1.0 protocol. Converts messages into data batches.

class v1_encoder_t ZMQ_FINAL : public encoder_base_t<v1_encoder_t>
{
  public:
    v1_encoder_t (size_t bufsize_);
    ~v1_encoder_t ();

  private:
    void size_ready ();
    void message_ready ();

#if defined(_MSC_VER)
    __declspec (align (ZMQ_CACHELINE_SIZE)) unsigned char _tmpbuf[11];
    unsigned char _padding[ZMQ_CACHELINE_SIZE - sizeof (_tmpbuf)];
#elif defined(__GNUC__) || defined(__INTEL_COMPILER)                           \
  || (defined(__SUNPRO_C) && __SUNPRO_C >= 0x590)                              \
  || (defined(__SUNPRO_CC) && __SUNPRO_CC >= 0x590)
    unsigned char _tmpbuf[11] __attribute__ ((aligned (ZMQ_CACHELINE_SIZE)));
    unsigned char _padding[ZMQ_CACHELINE_SIZE - sizeof (_tmpbuf)];
#else
    unsigned char _tmpbuf[11];
#endif

    ZMQ_NON_COPYABLE_NOR_MOVABLE (v1_encoder_t)
};
}

#endif
