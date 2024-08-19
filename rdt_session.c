//=====================================================================
//
// RDT Session - A Better Reliable Data Transfer Session Implementation Used By Mobile Game
// zhuyankong@163.com 2024-2025
//  
//=====================================================================

#include "rdt_session.h"
#include "mbuf.h"

#include <stdio.h>
#include <limits.h>
#include <stdarg.h>

#define SIZE_NONE   0
#define SIZE_UINT8  1
#define SIZE_UINT16 2
#define SIZE_UINT32 3
#define SIZE_UINT64 4

const int MBUF_INIT_SIZE = 10240;

const int RAW_SEND_BUF_DEFAULT = 64 * 1024;
const int AUTO_ACK_THREASHHOLD_DEFAULT = 10 * 1024;

const int DECODE_HEADER_OK = 0;
const int DECODE_HEADER_ERR = -1;
const int DECODE_HEADER_LACK = -2;

typedef struct rdt_header_s {
    unsigned char ack_size : 4;
    unsigned char data_size : 4;
} rdt_header_t;

static char __check_header_size[sizeof(rdt_header_t) == 1 ? 1 : -1];

static int rdts_canlog(rdt_session_t *rdts, int mask)
{
	if ((mask & rdts->logmask) == 0 || rdts->writelog == NULL) return 0;

	return 1;
}

static void rdts_log(rdt_session_t *rdts, int mask, const char *fmt, ...)
{
	char buffer[1024];
	va_list argptr;
	if ((mask & rdts->logmask) == 0 || rdts->writelog == 0) return;
	va_start(argptr, fmt);
	vsprintf(buffer, fmt, argptr);
	va_end(argptr);
	rdts->writelog(buffer, rdts, rdts->user);
}

static void init_packet_header(rdt_header_t *hdr, uint64_t offset, uint32_t data_len)
{
    if (offset == 0) {
        hdr->ack_size = SIZE_NONE;
    } else if (offset <= UCHAR_MAX) {
        hdr->ack_size = SIZE_UINT8;
    } else if (offset <= USHRT_MAX) {
        hdr->ack_size = SIZE_UINT16;
    } else if (offset <= UINT_MAX) {
        hdr->ack_size = SIZE_UINT32;
    } else {
        //rarely use
        hdr->ack_size = SIZE_UINT64;
    }

    if (data_len == 0) {
        hdr->data_size = SIZE_NONE;
    } else if (data_len <= UCHAR_MAX) {
        hdr->data_size = SIZE_UINT8;
    } else if (data_len <= USHRT_MAX) {
        hdr->data_size = SIZE_UINT16;
    } else if (data_len <= UINT32_MAX) {
        hdr->data_size = SIZE_UINT32;
    } else {
        //rarely use
        hdr->data_size = SIZE_UINT64;
    }
}

static void mbuf_push_number(mbuf_t *mbuf, uint64_t len)
{
    if (len <= UCHAR_MAX) {
        unsigned char n = (unsigned char)len;
        MBUF_ENQ_WITH_TYPE(mbuf, &n, unsigned char);
    } else if (len <= USHRT_MAX) {
        unsigned short n = (unsigned short)len;
        MBUF_ENQ_WITH_TYPE(mbuf, &n, unsigned short);
    } else if (len <= UINT32_MAX) {
        uint32_t n = (uint32_t)len;
        MBUF_ENQ_WITH_TYPE(mbuf, &n, uint32_t);
    } else {
        uint64_t n = (uint64_t)len;
        MBUF_ENQ_WITH_TYPE(mbuf, &n, uint64_t);
    }
}

void rdts_dump(rdt_session_t *rdts)
{
    if (!rdts_canlog(rdts, RDTS_LOG_DUMP)) {
        return;
    }

    rdts_log(rdts, RDTS_LOG_DUMP, "rdt_session=%p,\
sid=%d,\
enable=%d,\
need_ack=%d,\
raw_rcv_buf=%u,\
rcv_buf=%u,\
raw_snd_buf=%u,\
snd_buf=%u,\
rcv_raw_offset=%lu,\
remote_rcv_raw_offset=%lu",
rdts,
rdts->sid,
rdts->enable,
rdts->need_ack,
rdts->raw_rcv_buf->data_size,
rdts->rcv_buf->data_size,
rdts->raw_snd_buf->data_size,
rdts->snd_buf->data_size,
rdts->rcv_raw_offset,
rdts->remote_rcv_raw_offset);
}

//-----------------------------
//create a new rdt session object
//-----------------------------
rdt_session_t *rdts_create(int sid, void *user)
{
    rdt_session_t *rdts = (rdt_session_t*)malloc(sizeof(rdt_session_t));
    if (rdts == NULL) {
        return NULL;
    }

    rdts->sid = sid;
    rdts->logmask = 0;
    rdts->enable = 1;
    rdts->need_ack = 0;

    rdts->rcv_raw_offset = 0;
    rdts->remote_rcv_raw_offset = 0;
    rdts->max_raw_snd_buf_size = RAW_SEND_BUF_DEFAULT;

    rdts->auto_ack_limit = AUTO_ACK_THREASHHOLD_DEFAULT;
    rdts->auto_ack_count = 0;

    rdts->raw_rcv_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    if (rdts->raw_rcv_buf == NULL) {
        rdts_release(rdts);
        return NULL;
    }
    mbuf_init(rdts->raw_rcv_buf, MBUF_INIT_SIZE);

    rdts->rcv_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    if (rdts->rcv_buf == NULL) {
        rdts_release(rdts);
        return NULL;
    }
    mbuf_init(rdts->rcv_buf, MBUF_INIT_SIZE);

    rdts->raw_snd_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    if (rdts->raw_snd_buf == NULL) {
        rdts_release(rdts);
        return NULL;
    }
    mbuf_init(rdts->raw_snd_buf, MBUF_INIT_SIZE);

    rdts->snd_buf = (mbuf_t *)malloc(sizeof(mbuf_t));
    if (rdts->snd_buf == NULL) {
        rdts_release(rdts);
        return NULL;
    }
    mbuf_init(rdts->snd_buf, MBUF_INIT_SIZE);

    rdts->userdata = NULL;
    rdts->on_ack = NULL;

    rdts->user = user;
    rdts->writelog = NULL;

    return rdts;
}

//-----------------------------
//init rdt session object
void rdts_init(rdt_session_t *rdts, uint32_t max_raw_snd_buf_size, uint32_t auto_ack_size)
{
    rdts->max_raw_snd_buf_size = max_raw_snd_buf_size;
    rdts->auto_ack_limit = auto_ack_size;
}
//-----------------------------

//-----------------------------
// release a rdt session object
//-----------------------------
void rdts_release(rdt_session_t *rdts)
{
    if (rdts == NULL) return;

    if (rdts->raw_snd_buf) {
        mbuf_free(rdts->raw_snd_buf);
        free(rdts->raw_snd_buf);
    }

    if (rdts->snd_buf) {
        mbuf_free(rdts->snd_buf);
        free(rdts->snd_buf);
    }

    if (rdts->raw_rcv_buf) {
        mbuf_free(rdts->raw_rcv_buf);
        free(rdts->raw_rcv_buf);
    }

    if (rdts->rcv_buf) {
        mbuf_free(rdts->rcv_buf);
        free(rdts->rcv_buf);
    }

    rdts->writelog = NULL;
    rdts->on_ack = NULL;
    rdts->user = NULL;
    rdts->userdata = NULL;

    free(rdts);
}

//-----------------------------
// reset a rdt session object
//-----------------------------
void rdts_reset(rdt_session_t *rdts)
{
    rdts->max_raw_snd_buf_size = 0;
    rdts->auto_ack_limit = 0;

    mbuf_reset(rdts->snd_buf, MBUF_INIT_SIZE);
    mbuf_reset(rdts->raw_snd_buf, MBUF_INIT_SIZE);
    mbuf_reset(rdts->raw_rcv_buf, MBUF_INIT_SIZE);
}

//-----------------------------
// set rdts enable flag
//-----------------------------
int rdts_set_enable(rdt_session_t *rdts, int flag)
{
    if (flag != RDTS_ENABLE && flag != RDTS_DISABLE) {
        if (rdts_canlog(rdts, RDTS_LOG_FLAG)) {
            rdts_log(rdts, RDTS_LOG_FLAG, "Unsupported flag value. flag=%d", flag);
        }

        return -1;
    }

    int old = rdts->enable;
    rdts->enable = flag;

    if (rdts_canlog(rdts, RDTS_LOG_FLAG)) {
        rdts_log(rdts, RDTS_LOG_FLAG, "change enable flag. flag=%d,old=%d", flag, old);
    }

    return old;
}

//-----------------------------
//check rdts enable flag. if enable then return 1 else 0
//-----------------------------
int rdts_check_enable(rdt_session_t *rdts)
{
    return rdts->enable;
}

//-----------------------------
//set rdts need_ack flag
//-----------------------------
int rdts_set_needack(rdt_session_t *rdts, int flag)
{
    int old = rdts->need_ack;
    rdts->need_ack = flag;

    if (rdts_canlog(rdts, RDTS_LOG_FLAG)) {
        rdts_log(rdts, RDTS_LOG_FLAG, "change need_ack flag. flag=%d,old=%d", flag, old);
    }

    return old;
}

int rdts_check_needack(rdt_session_t *rdts)
{
    return rdts->need_ack;
}

//-----------------------------
//set on_ack callback, when recieved an ack packet and rdts->need_ack is set, which will be invoked by rdts
//-----------------------------
void rdts_set_onack(rdt_session_t *rdts, void (*on_ack)(uint64_t offset, void *userdata), void *userdata)
{
    rdts->on_ack = on_ack;
    rdts->userdata = userdata;
}

//-----------------------------
// user/upper level send, returns below zero for error
//-----------------------------
int rdts_send(rdt_session_t *rdts, const char *buf, uint32_t len)
{
    if (rdts->raw_snd_buf->data_size + len >= rdts->max_raw_snd_buf_size) {
        if (rdts_canlog(rdts, RDTS_LOG_SEND)) {
            rdts_log(rdts, RDTS_LOG_SEND, "raw_snd_buf overflow. sid=%d,snd_buf_sz=%ld,len=%ld", rdts->sid, rdts->raw_snd_buf->data_size, len);
        }
        return -1;
    }

    rdt_header_t hdr;
    init_packet_header(&hdr, 0, len);

    MBUF_ENQ_WITH_TYPE(rdts->snd_buf, &hdr, rdt_header_t);
    mbuf_push_number(rdts->snd_buf, len);
    MBUF_ENQ(rdts->snd_buf, buf, len);

    MBUF_ENQ(rdts->raw_snd_buf, buf, len);

    if (rdts_canlog(rdts, RDTS_LOG_SEND)) {
        rdts_log(rdts, RDTS_LOG_SEND, "send data. sid=%d,snd_buf_sz=%ld,len=%ld", rdts->sid, rdts->raw_snd_buf->data_size, len);
    }

    return 0;
}

//-----------------------------
// when reconnect to remote endpoint, resend all data in raw_snd_buf to avoid losing user layer data.
//-----------------------------
int rdts_push_raw(rdt_session_t *rdts)
{
    uint32_t len = rdts->raw_snd_buf->data_size;
    if (len <=0 ) {
        return 0;
    }

    //discard data in snd_buf
    mbuf_reset(rdts->snd_buf, MBUF_INIT_SIZE);

    rdt_header_t hdr;
    init_packet_header(&hdr, 0, len);
    MBUF_ENQ_WITH_TYPE(rdts->snd_buf, &hdr, rdt_header_t);
    mbuf_push_number(rdts->snd_buf, len);

    const char *buf = mbuf_pullup(rdts->raw_snd_buf);
    MBUF_ENQ(rdts->snd_buf, buf, len);

    if (rdts_canlog(rdts, RDTS_LOG_PUSH_RAW)) {
        rdts_log(rdts, RDTS_LOG_SEND, "push raw. sid=%d,raw_snd_buf=%u,remote_rcv_raw_offset=%lu", rdts->sid, rdts->raw_snd_buf->data_size, rdts->remote_rcv_raw_offset);
    }

    return 0;
}

//-----------------------------
// send an ack packet to the remote endpoint to notify the offset of the received data
//-----------------------------
int rdts_send_ack(rdt_session_t *rdts)
{
    rdt_header_t hdr;
    uint64_t offset = rdts->rcv_raw_offset;
    init_packet_header(&hdr, offset, 0);
    MBUF_ENQ_WITH_TYPE(rdts->snd_buf, &hdr, rdt_header_t);
    mbuf_push_number(rdts->snd_buf, offset);
    rdts->auto_ack_count = 0;

    if (rdts_canlog(rdts, RDTS_LOG_ACK)) {
        rdts_log(rdts, RDTS_LOG_ACK, "[info]send ack. sid=%d,rcv_raw_offset=%ld", rdts->sid, offset);
    }

    return 0;
}

//-----------------------------
//when received an ack, modify the remote_recv_raw_offset
//-----------------------------
static int rdts_on_rcv_ack(rdt_session_t *rdts, uint64_t offset)
{
    if (rdts->remote_rcv_raw_offset == offset) {
        if (rdts_canlog(rdts, RDTS_LOG_ACK)) {
            rdts_log(rdts, RDTS_LOG_ACK, "[warn]remote repeat ack. sid=%d,remote_rcv_raw_offset=%lu,offset=%lu", rdts->sid, rdts->remote_rcv_raw_offset, offset);
        }
        return 0;
    } else if (rdts->remote_rcv_raw_offset > offset) {
        if (rdts_canlog(rdts, RDTS_LOG_ACK)) {
            rdts_log(rdts, RDTS_LOG_ACK, "[warn]remote ack smaller then local. sid=%d,remote_rcv_raw_offset=%lu,offset=%lu", rdts->sid, rdts->remote_rcv_raw_offset, offset);
        }
        return -1;
    }

    uint64_t delta = offset - rdts->remote_rcv_raw_offset;
    if (rdts->raw_snd_buf->data_size < delta) {
        if (rdts_canlog(rdts, RDTS_LOG_ACK)) {
            rdts_log(rdts, RDTS_LOG_ACK, "[error]remote ack: not enough data for ack. sid=%d,remote_rcv_raw_offset=%lu,offset=%lu,delta=%lu,raw_snd_buf=%u", rdts->sid, rdts->remote_rcv_raw_offset, offset, delta, rdts->raw_snd_buf->data_size);
        }

        return -2;
    }

    mbuf_drain(rdts->raw_snd_buf, delta);
    rdts->remote_rcv_raw_offset = offset;
    if (rdts->need_ack && rdts->on_ack) {
        rdts->on_ack(offset, rdts->userdata);
    }
    rdts->need_ack = 0;

    if (rdts_canlog(rdts, RDTS_LOG_ACK)) {
        rdts_log(rdts, RDTS_LOG_ACK, "[info]remote ack offset. sid=%d,remote_rcv_raw_offset=%lu,offset=%lu,delta=%u", rdts->sid, rdts->remote_rcv_raw_offset - delta, offset, delta);
    }

    return 0;
}


//-----------------------------
//when received remote data, push into raw_rcv_buf and wait for user level read
//-----------------------------
static int rdts_on_rcv_data(rdt_session_t *rdts, const char *buf, uint32_t len)
{
    mbuf_t *rcv_buf = rdts->raw_rcv_buf;
    MBUF_ENQ(rcv_buf, buf, len);
    rdts->rcv_raw_offset += len;

    rdts->auto_ack_count += len;
    if (rdts->auto_ack_count >= rdts->auto_ack_limit) {
        rdts_send_ack(rdts);
    }

    if (rdts_canlog(rdts, RDTS_LOG_RECV)) {
        rdts_log(rdts, RDTS_LOG_ACK, "[info]recv data. sid=%d,rcv_raw_offset=%lu,len=%u", rdts->sid, rdts->rcv_raw_offset, len);
    }

    return 0;
}

#define READ_TYPE(p, end, dest, type)  \
    if (p + sizeof(type) - 1 > end)    \
    {                                  \
        return DECODE_HEADER_ERR; \
    }                                  \
    *dest = *((type *)(p));            \
    p += sizeof(type);                 \
    break

//-----------------------------
// parse header
// packet: [hdr, end]
//-----------------------------

static int parse_header(rdt_header_t *hdr, uint32_t payload, uint64_t *ack_offset, uint32_t *data_size, const char **pdata, uint32_t *pkg_len)
{
    const char *p = (const char *)(hdr + 1);
    const char *end = (const char *)hdr + payload - 1;
    *ack_offset = 0;
    *data_size = 0;
    *pkg_len = 0;
    *pdata = NULL;

    //parse ack filed
    switch (hdr->ack_size) {
    case SIZE_NONE: {
        *ack_offset = 0;
        break;
    }
    case SIZE_UINT8: {
        READ_TYPE(p, end, ack_offset, uint8_t);
    }
    case SIZE_UINT16: {
        READ_TYPE(p, end, ack_offset, uint16_t);
    }
    case SIZE_UINT32: {
        READ_TYPE(p, end, ack_offset, uint32_t);
    }
    case SIZE_UINT64: {
        READ_TYPE(p, end, ack_offset, uint64_t);
    }
    default: {
        return DECODE_HEADER_ERR;
    }
    }

    switch (hdr->data_size) {
    case SIZE_NONE: {
        *data_size = 0;
        break;
    }
    case SIZE_UINT8: {
        READ_TYPE(p, end, data_size, uint8_t);
    }
    case SIZE_UINT16: {
        READ_TYPE(p, end, data_size, uint16_t);
    }
    case SIZE_UINT32: {
        READ_TYPE(p, end, data_size, uint32_t);
    }
    case SIZE_UINT64: {
        READ_TYPE(p, end, data_size, uint64_t);
    }
    default: {
        return DECODE_HEADER_ERR;
    }
    }

    if (*data_size > 0 && (p + *data_size - 1) > end) {
        return DECODE_HEADER_LACK;
    }

    *pdata = p;
    *pkg_len = p - (const char *)hdr + *data_size; //the packet len to drain
    return DECODE_HEADER_OK;
}

//-----------------------------
// when you received a low level packet (eg. tcp or udp packet), call it
//-----------------------------
int rdts_input(rdt_session_t *rdts, const char *buf, uint32_t len)
{
    if (rdts_canlog(rdts, RDTS_LOG_INPUT)) {
        rdts_log(rdts, RDTS_LOG_INPUT, "[info]input data. sid=%d,len=%u", rdts->sid, len);
    }

    rdt_header_t *hdr = NULL;
    const char *pdata = NULL;
    const char *pinput = NULL;
    uint64_t ack_offset = 0;
    uint32_t data_size = 0, pkg_len = 0, drain_len = 0;
    int r = 0, use_buf = 0;
    mbuf_t *rcv_buf = rdts->rcv_buf;


    if (rcv_buf->data_size <= 0) {
        pinput = buf;
    } else {
        MBUF_ENQ(rcv_buf, buf, len);
        pinput = (const char *)mbuf_pullup(rcv_buf);
        len = rcv_buf->data_size;
        use_buf = 1;
    }

    while (1) {
        if (len <= sizeof(rdt_header_t)) {
            if (!use_buf) {
                MBUF_ENQ(rcv_buf, pinput, len);
            }
            break;
        }

        hdr = (rdt_header_t *)pinput;
        ack_offset = data_size = pkg_len = 0;
        r = parse_header(hdr, len, &ack_offset, &data_size, &pdata, &pkg_len);
        if (r == DECODE_HEADER_OK) {
            if (ack_offset > 0) {
                rdts_on_rcv_ack(rdts, ack_offset);
            }

            if (data_size > 0) {
                rdts_on_rcv_data(rdts, pdata, data_size);
            }

            drain_len += pkg_len;
            pinput += pkg_len;
            len -= pkg_len;
        } else if (r == DECODE_HEADER_LACK) {
            if (!use_buf) {
                MBUF_ENQ(rcv_buf, pinput, len);
            }
        } else {
            if (rdts_canlog(rdts, RDTS_LOG_INPUT)) {
                rdts_log(rdts, RDTS_LOG_INPUT, "rdts_input: parse header error. sid=%d,r=%d", rdts->sid, r);

                return -1;
            }
        }
    }

    if (use_buf && drain_len > 0) {
        mbuf_drain(rcv_buf, drain_len);
    }

    return 0;
}

//send buf operation
//-----------------------------
//pull data from rdts->snd_buf, and call rdts_drain_snd_buf() to free.
//-----------------------------
const char *rdts_pullup_snd_buf(rdt_session_t *rdts)
{
    return (const char *)mbuf_pullup(rdts->snd_buf);
}

//-----------------------------
// free all data in rdts->snd_buf
//-----------------------------
void rdts_drain_snd_buf(rdt_session_t *rdts, uint32_t len)
{
    mbuf_drain(rdts->snd_buf, len);
}

//-----------------------------
// get data length in rdts->snd_buf
//-----------------------------
uint32_t rdts_get_snd_buf_length(rdt_session_t *rdts)
{
    return rdts->snd_buf->data_size;
}

//recv buf operation
//-----------------------------
//pull data from rdts->raw_rcv_buf, and call rdts_drain_snd_buf() to free.
//-----------------------------
const char *rdts_pullup_raw_rcv_buf(rdt_session_t *rdts)
{
    return (const char *)mbuf_pullup(rdts->raw_rcv_buf);
}

//-----------------------------
// free all data in rdts->raw_rcv_buf
//-----------------------------
void rdts_drain_raw_rcv_buf(rdt_session_t *rdts, uint32_t len)
{
    mbuf_drain(rdts->raw_rcv_buf, len);
}

//-----------------------------
// get data length in rdts->raw_rcv_buf
//-----------------------------
uint32_t rdts_get_raw_rcv_buf_length(rdt_session_t *rdts)
{
    return rdts->raw_rcv_buf->data_size;
}

//for debug
uint64_t rdts_get_rcv_raw_offset(rdt_session_t *rdts)
{
    return rdts->rcv_raw_offset;
}