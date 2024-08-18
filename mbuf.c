
#include "mbuf.h"
#include <errno.h>

#define MIN_BLK_SIZE 4 
#define BLK_FACTOR 4 

#define BLK_SIZE  (2048)

inline static void blk_buf_init(mbuf_blk_t *blk)
{
	blk->head = blk->tail = blk->buf;
}

mbuf_blk_t *mbuf_add_blk(mbuf_t *mbuf, uint32_t size)
{
	mbuf_blk_t *blk = (mbuf_blk_t *)calloc(1, sizeof(mbuf_blk_t));

	size = mbuf->hint_size > size ? mbuf->hint_size : size;
	size = BLK_FACTOR * ((size + BLK_FACTOR - 1) / BLK_FACTOR);
	if (size < MIN_BLK_SIZE) {
		size = MIN_BLK_SIZE;
	}

	blk->id = mbuf->blk_count;	
	blk->mbuf = mbuf;
	blk->buf = (char*)malloc(size);
	blk->size = size;
	blk->end = blk->buf + blk->size;
	blk_buf_init(blk);


	blk->next = NULL;
	if (mbuf->tail == NULL) {
		mbuf->head = mbuf->tail = blk;
	} else {
		mbuf->tail->next = blk;
		mbuf->tail = blk;
	}
	
	mbuf->blk_count++;
	mbuf->blk_enq = blk;
	mbuf->alloc_size += blk->size;

	return blk;
}

void mbuf_init(mbuf_t *mbuf, uint32_t blk_size)
{
	mbuf->hint_size = blk_size == 0 ? BLK_SIZE : blk_size;
	mbuf->data_size = 0;
	mbuf->blk_enq = NULL;
	mbuf->blk_deq = NULL;
	mbuf->blk_count = 0;
	mbuf->alloc_size = 0;
	mbuf->head = NULL;
	mbuf->tail = NULL;
	mbuf->blk_deq = mbuf_add_blk(mbuf, mbuf->hint_size);
}

void mbuf_free(mbuf_t *mbuf)
{
	mbuf_blk_t *blk, *tmp;

	for (blk = mbuf->head; blk && (tmp = blk->next, 1); blk = tmp) {
		mbuf->blk_count--;
		free(blk->buf);
		free(blk);
	}
}

void mbuf_reset(mbuf_t *mbuf, uint32_t reset_size)
{
	if (mbuf->blk_count > 1 || (reset_size > mbuf->alloc_size)) {
		mbuf_free(mbuf);
		mbuf_init(mbuf, (reset_size > mbuf->alloc_size) ? reset_size : mbuf->alloc_size);
	} else {
		assert(mbuf->blk_count == 1);
		blk_buf_init(mbuf->head);
		mbuf->data_size = 0;
	}
	assert(mbuf->head == mbuf->tail && mbuf->head == mbuf->blk_enq && mbuf->head == mbuf->blk_deq);
}

const char *mbuf_pullup(mbuf_t *mbuf)
{
	uint32_t size, offset;
	mbuf_blk_t *blk;
	if (mbuf->blk_count <= 0) return NULL;
	if (mbuf->blk_count == 1) goto done;

	blk = (mbuf_blk_t *)calloc(1, sizeof(mbuf_blk_t));

	size = mbuf->alloc_size;

	blk->id = 0;
	blk->mbuf = mbuf;
	blk->buf = (char*)malloc(size);
	blk->size = size;
	blk->end = blk->buf + blk->size;
	blk_buf_init(blk);
	blk->next = NULL;

	offset = 0;
	mbuf_blk_t *tblk, *tmp;
	for (tblk = mbuf->head; tblk && (tmp = tblk->next, 1); tblk = tmp) {
		uint32_t len = MBUF_BLK_DATA_LEN(tblk);
		if (len > 0) {
			memcpy(blk->buf+offset, tblk->head, len);
			offset += len;
		}

		free(tblk->buf);
		free(tblk);
	}

	blk->tail = blk->head + offset;

	mbuf->head = mbuf->tail = mbuf->blk_deq = mbuf->blk_enq = blk;
	mbuf->blk_count = 1;

done:
	return mbuf->head->head;
}

void mbuf_enq_span(mbuf_t *mbuf, void *data, uint32_t len)
{
	mbuf_blk_t *blk = mbuf->blk_enq;
	uint32_t capacity = MBUF_BLK_CAP(blk);
	char *dat = (char *)data;
	if (capacity < len) {
		memcpy(blk->tail, dat, capacity);
		MBUF_ADVANCE(mbuf, blk, capacity);

		len -= capacity;
		dat += capacity;
		blk = mbuf_add_blk(mbuf, len);
	}
	memcpy(blk->tail, dat, len);
	MBUF_ADVANCE(mbuf, blk, len);
}



void *mbuf_enq(mbuf_t *mbuf, void *data, uint32_t len)
{
	void *p = MBUF_ALLOC(mbuf, len);
	if (data)
		memcpy(p, data, len);
	return p;
}

uint32_t mbuf_deq(mbuf_t *mbuf, void *ret, uint32_t len)
{
	mbuf_blk_t *blk = mbuf->blk_deq;
	uint32_t slen = len;

	do {
		uint32_t payload = blk->tail - blk->head;
		uint32_t min = payload < len ? payload : len;
		if (min > 0) {
			if (ret != NULL)
				memcpy(ret, blk->head, payload);
			blk->head += min;
			mbuf->data_size -= min;
			len -= min;
		}

	} while (len > 0 && NULL != (blk = mbuf->blk_deq = blk->next));

	return slen - len;
}

void mbuf_drain(mbuf_t *mbuf, uint32_t drainlen)
{
	mbuf_deq(mbuf, NULL, drainlen);
}