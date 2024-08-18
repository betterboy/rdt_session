#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "rdts_manager.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

extern lua_State *gL;
static rdt_manager_t *g_rdts_mng = NULL;

const int MESSAGE_EMPTY = 0;
const int MESSAGE_IN = 1;
const int MESSAGE_OUT = 2;

typedef struct message_s {
    uint32_t sz;
    char buf[0];
} message_t;

typedef struct connection_message_s {
    uint32_t sz;
    char *buf;
} connection_message_t;

static rdt_session_t * get_session(lua_State *L)
{
    if (g_rdts_mng == NULL) {
        luaL_error(L, "session manager not init");
    }

    int sid = luaL_optinteger(L, 1, 0);
    if (sid <= 0) {
        luaL_error(L, "need session id");
    }

    rdt_session_t *rdts = find_session(g_rdts_mng, sid, 1);
    if (rdts == NULL) {
        luaL_error(L, "rdt session not create: [%d]", sid);
    }

    return rdts;
}


static int lrdt_delete(lua_State *L)
{
    rdt_session_t *rdts = get_session(L);
    delete_session(g_rdts_mng, rdts->sid);

    return 0;
}

static int lrdt_create(lua_State *L)
{
    int sid = luaL_optinteger(L, 1, 0);
    if (sid <= 0) {
        luaL_error(L, "need session id");
    }

    rdt_session_t *rdts = find_session(g_rdts_mng, sid, 0);
    if (rdts) {
        luaL_error(L, "session already create");
    }

    create_session(g_rdts_mng, sid);
    return 0;
}

static int lrdt_disable(lua_State *L)
{
    rdt_session_t *rdts = get_session(L);
    disable_session(g_rdts_mng, rdts->sid);

    return 0;
}

static int lrdt_reconnect(lua_State *L)
{
    if (g_rdts_mng == NULL) {
        luaL_error(L, "session manager not init");
    }

    int sid = luaL_optinteger(L, 1, 0);
    if (sid <= 0) {
        luaL_error(L, "need session id");
    }

    rdt_session_t *rdts = find_session(g_rdts_mng, sid, 0);
    if (rdts == NULL) {
        luaL_error(L, "rdt session not create: [%d]", sid);
    }

    reconnect_session(g_rdts_mng, rdts->sid);
    return 0;
}

static int lrdt_ack(lua_State *L)
{
    rdt_session_t *rdts = get_session(L);
    ack_session(g_rdts_mng, rdts->sid);

    return 0;
}

static message_t *new_message(const char *buf, uint32_t len)
{
    message_t *msg = (message_t *)malloc(sizeof(*msg) + len);
    msg->sz = len;
    memcpy(msg->buf, (const void *)buf, len);

    return msg;
}

static int lsend(lua_State *L)
{
    rdt_session_t *rdts = get_session(L);
    size_t sz = 0;
    const char *buf = luaL_checklstring(L, 2, &sz);
    if (sz == 0) {
        return 0;
    }

    message_t *msg = new_message(buf, (uint32_t)sz);
    rdts_send(rdts, (const char *)msg, sizeof(*msg) + sz);

    return 0;
}

static int lrecv(lua_State *L)
{
    rdt_session_t *rdts = get_session(L);
    size_t sz = 0;
    const char *buf = luaL_checklstring(L, 2, &sz);
    if (sz == 0) {
        return 0;
    }

    rdts_input(rdts, buf, sz);
    return 0;
}

static int pollin(rdt_session_t *rdts, connection_message_t *m)
{
    uint32_t total = rdts_get_raw_rcv_buf_length(rdts);
    if (total <= sizeof(message_t)) {
        return MESSAGE_EMPTY;
    }

    const char *buf = rdts_pullup_raw_rcv_buf(rdts);
    message_t *msg = (message_t *)buf;
    uint32_t len = msg->sz;
    if (total < len + sizeof(*msg)) {
        return MESSAGE_EMPTY;
    }

    m->sz = len;
    m->buf = (char *)malloc(len);
    memcpy(m->buf, buf + sizeof(*msg), len);
    rdts_drain_raw_rcv_buf(rdts, len + sizeof(*msg));

    return MESSAGE_IN;
}

static int pollout(rdt_session_t *rdts, connection_message_t *m)
{
    uint32_t total = rdts_get_snd_buf_length(rdts);
    if (total <= 0) {
        return MESSAGE_EMPTY;
    }

    const char *buf = rdts_pullup_snd_buf(rdts);
    m->sz = total;
    m->buf = (char *)malloc(total);
    memcpy(m->buf, buf, total);
    rdts_drain_snd_buf(rdts, total);

    return MESSAGE_OUT;
}

static int lpoll(lua_State *L)
{
    rdt_session_t *rdts = get_session(L);
    connection_message_t m;
    int t = pollout(rdts, &m);
    if (t == MESSAGE_EMPTY) {
        t = pollin(rdts, &m);
    }

    if (t != MESSAGE_EMPTY) {
        lua_pushinteger(L, t);
        lua_pushlstring(L, m.buf, m.sz);
        free(m.buf);
        return 2;
    }

    return 0;
}

int luaopen_lsocket_client(lua_State *L)
{
    gL = L;
    g_rdts_mng = rdt_manager_create();
    const luaL_Reg method[] = {
		{"rdt_delete", lrdt_delete},
		{"rdt_create", lrdt_create},
		{"rdt_disable", lrdt_disable},
        {"rdt_reconnect", lrdt_reconnect},
		{"rdt_ack", lrdt_ack},
		{"rdt_send", lsend},
		{"rdt_recv", lrecv},
		{"rdt_poll", lpoll},
		// {"", },
		{NULL, NULL},
    };
    luaL_newlib(L, method);

    return 1;
}