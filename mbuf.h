//======================================================
// A Simple mbuf Implementation
//======================================================

#ifndef __MBUF_H__
#define __MBUF_H__

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct mbuf_s mbuf_t;
typedef struct mbuf_blk_s {
	struct mbuf_blk_s *next;
	mbuf_t *mbuf;
	unsigned id;
	uint32_t size;
	char *buf;
	char *head;
	char *tail;
	char *end;
} mbuf_blk_t;

struct mbuf_s {
	mbuf_blk_t *head;
	mbuf_blk_t *tail;
	
	mbuf_blk_t *blk_deq;
	mbuf_blk_t *blk_enq;
	int blk_count;
	uint32_t data_size;
	uint32_t hint_size;
	uint32_t alloc_size;
};


#define MBUF_ADVANCE(mbuf, blk, len)  do {\
	(mbuf)->data_size += (len);	\
	(blk)->tail += (len);	\
} while (0)

#define MBUF_BLK_CAP(blk) ((blk)->end - (blk)->tail)
#define MBUF_BLK_DATA_LEN(blk) ((blk)->tail - (blk)->head)

#define MBUF_ENQ(mbuf, data, len) do { \
	void *p = MBUF_ALLOC(mbuf, len);\
	memcpy(p, data, len);\
} while(0)

#define MBUF_ENQ_WITH_TYPE(mbuf, data, type) do { \
	type *p = (type*)MBUF_ALLOC(mbuf, sizeof(type));	\
	*p = *(data);										\
} while(0)

typedef struct inbuf_s {
	const char *buf;
	uint32_t off;
	uint32_t size;
} inbuf_t;

#define INBUF_INIT(in, data, len) do {\
	(in)->buf = (data);\
	(in)->size = (len);\
	(in)->off = 0;	\
} while (0)

#define INBUF_POS(in) ((void*)((in)->buf + (in)->off))
#define INBUF_LEN(in) ((in)->size - (in)->off)
#define INBUF_ADV(in, len) ((in)->off += (len))
#define INBUF_CHECK(in, len)  do {if (INBUF_LEN(in) < (len)) return -1; } while (0)

#define INBUF_GET_NO_CHECK(in, retv, len) do { \
	memcpy((retv), INBUF_POS((in)), (len));\
	INBUF_ADV((in), (len));	\
} while(0)

#define INBUF_GET_TYPE(in, retv, type) do {\
	INBUF_CHECK((in), sizeof(type));\
	*(type*)(retv) = *(type*)INBUF_POS((in));\
	INBUF_ADV((in), sizeof(type)); \
} while(0)


#if defined(__cplusplus)
extern "C" {
#endif

void mbuf_init(mbuf_t *mbuf, uint32_t blk_size);
void mbuf_free(mbuf_t *mbuf);
void *mbuf_enq(mbuf_t *mbuf, void *data, uint32_t len);
void mbuf_enq_span(mbuf_t *mbuf, void *data, uint32_t len);
uint32_t mbuf_deq(mbuf_t *mbuf, void *ret, uint32_t len);
void mbuf_reset(mbuf_t *mbuf, uint32_t reset_size);
const char *mbuf_pullup(mbuf_t *mbuf);
void mbuf_drain(mbuf_t *mbuf, uint32_t drainlen);

mbuf_blk_t *mbuf_add_blk(mbuf_t *mbuf, uint32_t size);

inline static void *MBUF_ALLOC(mbuf_t *mbuf, uint32_t len)
{
	for (; (uint32_t)MBUF_BLK_CAP(mbuf->blk_enq) < len; ) {
		mbuf->blk_enq = mbuf->blk_enq->next;
		if (mbuf->blk_enq == NULL) {
			mbuf_add_blk(mbuf, len);
			break;
		}
		assert(MBUF_BLK_DATA_LEN(mbuf->blk_enq) == 0);
	}

	MBUF_ADVANCE(mbuf, mbuf->blk_enq, len);
	return mbuf->blk_enq->tail - len;
}
#if defined(__cplusplus)
}
#endif

#endif // __MBUF_H__

