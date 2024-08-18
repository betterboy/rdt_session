
#include "rdt_session.h"
#include "mbuf.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static int g_count = 0;

static void writelog(const char *log, rdt_session_t *rdts, void *user)
{
	printf("%s\n", log);
}

static int dump(const char *str, size_t sz, const char *buffer)
{
	printf("%s (%d) ",str, (int)sz);
	size_t i;
	int *buf = (int *)buffer;
	int cnt = sz / 4;
	for (i=0;i<cnt;i++) {
		printf("%d ", buf[i]);
	}
	printf("\n");
	return cnt;
}

static void sendto_peer(rdt_session_t *rdts, int n)
{
	uint8_t *buf = (uint8_t *)malloc(n * 4);
	int i;
	int *p = (int *)buf;
	for (i = 0; i < n; i++) {
		*p = ++g_count;
		p++;
	}

	rdts_send(rdts, (const char *)buf, n * 4);
	free(buf);
}

static int dispatch_client(rdt_session_t *client, rdt_session_t *server)
{
	int n = 0;
	mbuf_t *raw_rcv_buf = client->raw_rcv_buf;
	mbuf_t *snd_buf = client->snd_buf;
	if (raw_rcv_buf->data_size > 0) {
		n++;
		dump("server->client: ", raw_rcv_buf->data_size, mbuf_pullup(raw_rcv_buf));
		mbuf_drain(raw_rcv_buf, raw_rcv_buf->data_size);
	} else if (snd_buf->data_size) {
		n++;
		const char *send_data = mbuf_pullup(snd_buf);
		rdts_input(server, send_data, snd_buf->data_size);
		mbuf_drain(snd_buf, snd_buf->data_size);
	} else {

	}

	return n;
}

static int dispatch_server(rdt_session_t *client, rdt_session_t *server)
{
	int cnt = -1;
	int n = 0;
	mbuf_t *raw_rcv_buf = server->raw_rcv_buf;
	mbuf_t *snd_buf = server->snd_buf;
	if (raw_rcv_buf->data_size > 0) {
		n++;
		cnt = dump("client->server: ", raw_rcv_buf->data_size, mbuf_pullup(raw_rcv_buf));
		mbuf_drain(raw_rcv_buf, raw_rcv_buf->data_size);
	} else if (snd_buf->data_size) {
		n++;
		const char *send_data = mbuf_pullup(snd_buf);
		rdts_input(client, send_data, snd_buf->data_size);
		mbuf_drain(snd_buf, snd_buf->data_size);
	} else {

	}

	if (cnt > 0) {
		sendto_peer(server, cnt);
	}

	return n;
}

static void dispatch(rdt_session_t *client, rdt_session_t *server)
{
	for(;;) {
		int n = dispatch_client(client, server);
		n += dispatch_server(client, server);
		if (n == 0) return;
	}
}

static void rdts_reconnect(rdt_session_t *client, rdt_session_t *server)
{
    mbuf_reset(client->snd_buf, 10240);
	rdts_send_ack(client);
	rdts_push_raw(client);

	mbuf_reset(server->snd_buf, 10240);
	rdts_send_ack(server);
	rdts_push_raw(server);
}

static void test_rdt(rdt_session_t *client, rdt_session_t *server)
{
	sendto_peer(client, 10);
	sendto_peer(client, 10);
	dispatch(client, server);
	sendto_peer(client, 20);
	dispatch(client, server);

	sendto_peer(client, 30);
	rdts_reconnect(client, server);
	dispatch(client, server);

	sendto_peer(client, 30);
	sendto_peer(client, 40);
	dispatch(client, server);

	rdts_dump(client);
	rdts_dump(server);
	assert(client->rcv_raw_offset == server->remote_rcv_raw_offset && client->remote_rcv_raw_offset == server->rcv_raw_offset);
}

int main()
{
    int sid = 10000;
	rdt_session_t *client = rdts_create(sid, NULL);
	rdts_init(client, 1024 * 10, 1);
	client->writelog = writelog;
	client->logmask = RDTS_LOG_DEBUG;

	rdt_session_t *server = rdts_create(sid, NULL);
	rdts_init(server, 1024 * 10, 1);
	server->writelog = writelog;
	server->logmask = RDTS_LOG_DEBUG;
	test_rdt(client, server);

    return 0;
}