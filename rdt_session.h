//======================================================
//
// RDT Session - A Better Reliable Data Transfer Session Implementation Used By Mobile Game
// zhuyankong@163.com 2024-2025
//  
// Features:
//
//======================================================

#ifndef __RDT_SESSION_H__
#define __RDT_SESSION_H__

#include <stdint.h>

#ifndef INLINE
#if defined(__GNUC__)

#if (__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1))
#define INLINE         __inline__ __attribute__((always_inline))
#else
#define INLINE         __inline__
#endif

#elif (defined(_MSC_VER) || defined(__BORLANDC__) || defined(__WATCOMC__))
#define INLINE __inline
#else
#define INLINE 
#endif
#endif

#if (!defined(__cplusplus)) && (!defined(inline))
#define inline INLINE
#endif

#define RDTS_LOG_RELEASE	    0	    1
#define RDTS_LOG_BASE		    1
#define RDTS_LOG_RECV			2
#define RDTS_LOG_SEND			4
#define RDTS_LOG_ACK			8
#define RDTS_LOG_PUSH_RAW		16
#define RDTS_LOG_FLAG		    32
#define RDTS_LOG_DUMP		    64
#define RDTS_LOG_INPUT	        128

#define RDTS_LOG_DEBUG          0xffff


#define RDTS_DISABLE 0
#define RDTS_ENABLE  1

#define RDTS_NO_ACK  0
#define RDTS_ACK 1

struct mbuf_s;
typedef struct mbuf_s mbuf_t;

typedef struct rdt_session_s {
    int sid;
    int logmask;
    int enable;
    int need_ack;

    uint64_t rcv_raw_offset;
    uint64_t remote_rcv_raw_offset;
    uint32_t max_raw_snd_buf_size;

    uint32_t auto_ack_limit;
    uint32_t auto_ack_count;

    mbuf_t *raw_rcv_buf;
    mbuf_t *rcv_buf;

    mbuf_t *raw_snd_buf;
    mbuf_t *snd_buf;

    void *user;
    void *userdata;

    void (*on_ack)(uint64_t offset, void *userdata);
	void (*writelog)(const char *log, struct rdt_session_s *session, void *user);

} rdt_session_t;



#if defined(__cplusplus)
extern "C"
{
#endif

//---------------------------------------------------------------------
// interface
//rdts = reliable data transfer session
//---------------------------------------------------------------------

void rdts_dump(rdt_session_t *rdts);

// create a new rdt session object, 'sid' must equal in two endpoint
// from the same connection. 'user' will be passed to the writelog callback
// writelog callback can be setup like this: 'rdt->writelog = your_writelog_callback'
rdt_session_t *rdts_create(int sid, void *user);

//set rdt session args
//@max_raw_snd_buf_size  the max amount of data that can be send before the remote endpoint acks any data
//@auto_ack_size  when one endpoint receives auto_ack_size data, rdt session will auto send an ack to remote endpoint
void rdts_init(rdt_session_t *rdts, uint32_t max_raw_snd_buf_size, uint32_t auto_ack_size);

//set rdts enable flag and return old value
int rdts_set_enable(rdt_session_t *rdts, int flag);
//check rdts enable flag. if enable then return 1 else 0
int rdts_check_enable(rdt_session_t *rdts);

//set rdts need_ack flag
int rdts_set_needack(rdt_session_t *rdts, int flag);
int rdts_check_needack(rdt_session_t *rdts);

// release rdt session control object
void rdts_release(rdt_session_t *rdts);

//reset rdt session to init status
void rdts_reset(rdt_session_t *rdts);

// user level send, returns below 0 for error
int rdts_send(rdt_session_t *rdts, const char *buf, uint32_t len);

// when reconnect to remote endpoint, resend all data in raw_snd_buf to avoid losing user layer data.
int rdts_push_raw(rdt_session_t *rdts);

// when you received a low level packet (eg. tcp or udp packet), call it
int rdts_input(rdt_session_t *rdts, const char *buf, uint32_t len);

// send an ack packet to the remote endpoint to notify the offset of the received data
int rdts_send_ack(rdt_session_t *rdts);

//set on_ack callback, when recieved an ack packet and rdts->need_ack is set, which will be invoked by rdts
void rdts_set_onack(rdt_session_t *rdts, void (*on_ack)(uint64_t offset, void *userdata), void *userdata);

//send buf operation
//pull data from rdts->snd_buf, and call rdts_drain_snd_buf() to free.
const char *rdts_pullup_snd_buf(rdt_session_t *rdts);

// free all data in rdts->snd_buf
void rdts_drain_snd_buf(rdt_session_t *rdts, uint32_t len);

// get data length in rdts->snd_buf
uint32_t rdts_get_snd_buf_length(rdt_session_t *rdts);

//recv buf operation
//pull data from rdts->raw_rcv_buf, and call rdts_drain_snd_buf() to free.
const char *rdts_pullup_raw_rcv_buf(rdt_session_t *rdts);

// free all data in rdts->raw_rcv_buf
void rdts_drain_raw_rcv_buf(rdt_session_t *rdts, uint32_t len);

// get data length in rdts->raw_rcv_buf
uint32_t rdts_get_raw_rcv_buf_length(rdt_session_t *rdts);

//for debug
uint64_t rdts_get_rcv_raw_offset(rdt_session_t *rdts);

#if defined(__cplusplus)
}
#endif


#endif //__RDT_SESSION__