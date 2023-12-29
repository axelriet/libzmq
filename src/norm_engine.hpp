
#ifndef __ZMQ_NORM_ENGINE_HPP_INCLUDED__
#define __ZMQ_NORM_ENGINE_HPP_INCLUDED__

#if defined ZMQ_HAVE_NORM

#if defined(ZMQ_HAVE_WINDOWS) && defined(ZMQ_IOTHREAD_POLLER_USE_EPOLL)
#define ZMQ_USE_NORM_SOCKET_WRAPPER
#endif


#include "io_object.hpp"
#include "i_engine.hpp"
#include "options.hpp"
#include "v1_decoder.hpp"
#include "v1_encoder.hpp"
#include "v2_decoder.hpp"
#include "v2_encoder.hpp"

#include <normApi.h>

namespace zmq
{
class io_thread_t;
class msg_t;
class session_base_t;

class norm_engine_t : public io_object_t, public i_engine
{
  public:
    norm_engine_t (zmq::io_thread_t *parent_, const options_t &options_);
    ~norm_engine_t ();

    // create NORM instance, session, etc
    virtual int init (const char *network_, bool send, bool recv);
    void shutdown ();

    bool has_handshake_stage () { return false; };

    //  i_engine interface implementation.
    //  Plug the engine to the session.
    void plug (zmq::io_thread_t *io_thread_,
               class session_base_t *session_);

    //  Terminate and deallocate the engine. Note that 'detached'
    //  events are not fired on termination.
    void terminate ();

    //  This method is called by the session to signalise that more
    //  messages can be written to the pipe.
    bool restart_input ();

    //  This method is called by the session to signalise that there
    //  are messages to send available.
    void restart_output ();

    void zap_msg_available () {}

    const endpoint_uri_pair_t &get_endpoint () const;

    // i_poll_events interface implementation.
    // (we only need in_event() for NormEvent notification)
    // (i.e., don't have any output events or timers (yet))
    void in_event ();

  protected:
    void unplug ();
    virtual void send_data ();
    virtual void recv_data (NormObjectHandle stream);

    enum
    {
        BUFFER_SIZE = 2048
    };

    // Used to keep track of streams from multiple senders
    class NormRxStreamState
    {
      public:
        NormRxStreamState (NormObjectHandle normStream,
                           int64_t maxMsgSize,
                           bool zeroCopy,
                           int inBatchSize);
        ~NormRxStreamState ();

        NormObjectHandle GetStreamHandle () const { return norm_stream; }

        bool Init ();

        void SetRxReady (bool state) { rx_ready = state; }
        bool IsRxReady () const { return rx_ready; }

        void SetSync (bool state) { in_sync = state; }
        bool InSync () const { return in_sync; }

        // These are used to feed data to decoder
        // and its underlying "msg" buffer
        char *AccessBuffer () { return (char *) (buffer_ptr + buffer_count); }
        size_t GetBytesNeeded () const { return buffer_size - buffer_count; }
        void IncrementBufferCount (size_t count) { buffer_count += count; }
        msg_t *AccessMsg () { return zmq_decoder->msg (); }
        // This invokes the decoder "decode" method
        // returning 0 if more data is needed,
        // 1 if the message is complete, If an error
        // occurs the 'sync' is dropped and the
        // decoder re-initialized
        int Decode ();

        class List
        {
          public:
            List ();
            ~List ();

            void Append (NormRxStreamState &item);
            void Remove (NormRxStreamState &item);

            bool IsEmpty () const { return NULL == head; }

            void Destroy ();

            class Iterator
            {
              public:
                Iterator (const List &list);
                NormRxStreamState *GetNextItem ();

              private:
                NormRxStreamState *next_item;
            };
            friend class Iterator;

          private:
            NormRxStreamState *head;
            NormRxStreamState *tail;

        }; // end class zmq::norm_engine_t::NormRxStreamState::List

        friend class List;

        List *AccessList () { return list; }


      protected:
        NormObjectHandle norm_stream;
        int64_t max_msg_size;
        bool zero_copy;
        int in_batch_size;
        bool in_sync;
        bool rx_ready;
        v2_decoder_t *zmq_decoder;
        bool skip_norm_sync;
        unsigned char *buffer_ptr;
        size_t buffer_size;
        size_t buffer_count;

        NormRxStreamState *prev;
        NormRxStreamState *next;
        NormRxStreamState::List *list;

    }; // end class zmq::norm_engine_t::NormRxStreamState

    const endpoint_uri_pair_t _empty_endpoint;

    session_base_t *zmq_session;
    options_t options;
    NormInstanceHandle norm_instance;
    handle_t norm_descriptor_handle;
    NormSessionHandle norm_session;
    bool is_sender;
    bool is_receiver;
    // Sender state
    msg_t tx_msg;
    v2_encoder_t zmq_encoder; // for tx messages (we use v2 for now)
    NormObjectHandle norm_tx_stream;
    bool tx_first_msg;
    bool tx_more_bit;
    bool zmq_output_ready; // zmq has msg(s) to send
    bool norm_tx_ready;    // norm has tx queue vacancy
    // TBD - maybe don't need buffer if can access zmq message buffer directly?
    char tx_buffer[BUFFER_SIZE];
    unsigned int tx_index;
    unsigned int tx_len;

    // Receiver state
    // Lists of norm rx streams from remote senders
    bool zmq_input_ready; // zmq ready to receive msg(s)
    NormRxStreamState::List
      rx_pending_list; // rx streams waiting for data reception
    NormRxStreamState::List
      rx_ready_list; // rx streams ready for NormStreamRead()
    NormRxStreamState::List
      msg_ready_list; // rx streams w/ msg ready for push to zmq

#ifdef ZMQ_USE_NORM_SOCKET_WRAPPER
    fd_t
      wrapper_read_fd; // filedescriptor used to read norm events through the wrapper
    unsigned int wrapper_thread_id;
    HANDLE wrapper_thread_handle;
#endif

}; // end class norm_engine_t

class norm_engine2_t : public norm_engine_t
{
  public:
      norm_engine2_t (io_thread_t *parent_,
                                       const options_t &options_) :
        norm_engine_t (parent_, options_),
        _encoder (0),
        _write_size (0),
        _send_more (false)
    {
    }

    virtual int init (const char *network_, bool send, bool recv) ZMQ_OVERRIDE
    {
        int result = norm_engine_t::init (network_, send, recv);

        _in_batch_size = options.in_batch_size;
        _max_message_size = options.maxmsgsize;
        _out_batch_size = options.out_batch_size;

        return result;
    }

  private:
    v1_encoder_t _encoder;
    size_t _write_size;
    bool _send_more;
    msg_t _outgoing_msg;
    size_t _out_batch_size;
    std::unique_ptr<uint8_t> _send_buffer;

    size_t _in_batch_size;
    size_t _max_message_size;

    typedef struct PeerStreamState
    {
        session_base_t *_session;
        bool _joined;
        v1_decoder_t _decoder;
        size_t _read_size;
        bool _receive_more;
        size_t _in_batch_size;
        msg_t _incoming_msg;
        std::unique_ptr<uint8_t> _receive_buffer;

        PeerStreamState (session_base_t *session_,
                         size_t in_batch_size_,
                           size_t max_message_size_) :
            _session(session_),
            _joined (false),
            _decoder (0, max_message_size_),
            _read_size (0),
            _receive_more (false),
            _in_batch_size (in_batch_size_)
        {
            _incoming_msg.init ();
            _receive_buffer.reset (
              new (std::nothrow) unsigned char[_in_batch_size]);
            alloc_assert (_receive_buffer.get ());
        }
        int ProcessInput (NormObjectHandle object);
    } PeerStreamState;

    void send_data () ZMQ_OVERRIDE;
    void recv_data (NormObjectHandle stream) ZMQ_OVERRIDE;
};

}

#endif // ZMQ_HAVE_NORM

#endif // !__ZMQ_NORM_ENGINE_HPP_INCLUDED__
