//====================================
//协议数据包格式：pto_data->len(4)|data
//rdt数据包格式: rdt_data->header(1)|pto_data
//网络数据包格式: pkg_data-> len(4)|use_rdt(1)|[pto_data|rdt_data]
//网络数据包使用use_rdt来标记该数据包是否是rdt包，否则是原始pto_data，解析时会根据此标记进行分别解包处理
//====================================
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "rdts_manager.h"

#include <string.h>
#include <stdlib.h>

const int POOL_EMPTY = 0;
const int POOL_IN = 1;
const int POOL_OUT = 2;

extern lua_State *gL;

static rdt_manager_t *g_rdts_mng = NULL;

typedef struct message_s {
    uint32_t sz;
    char buf[0];
} message_t;

typedef  struct poll_message_s {
    int sid;
    uint32_t sz;
    char *buf;
} poll_message_t;

//packet = [head(4)|data]
static int pollin(rdt_session_t *rdts, poll_message_t *m)
{
    uint32_t total = rdts_get_raw_rcv_buf_length(rdts);
    if (total <= sizeof(message_t)) {
        return POOL_EMPTY;
    }

    const char *buf = rdts_pullup_raw_rcv_buf(rdts);
    message_t *msg = (message_t *)buf;
    uint32_t len = msg->sz;
    if (total < len + sizeof(*msg)) {
        return POOL_EMPTY;
    }

    m->sid = rdts->sid;
    m->sz = len;
    m->buf = (char *)malloc(len);
    memcpy(m->buf, buf + sizeof(*msg), len);
    rdts_drain_raw_rcv_buf(rdts, len + sizeof(*msg));

    return POOL_IN;
}

static int pollout(rdt_session_t *rdts, poll_message_t *m)
{
    uint32_t total = rdts_get_snd_buf_length(rdts);
    if (total <= 0) {
        return POOL_EMPTY;
    }

    const char *buf = rdts_pullup_snd_buf(rdts);
    m->sid = rdts->sid;
    m->sz = total;
    m->buf = (char *)malloc(total);
    memcpy(m->buf, buf, total);
    rdts_drain_snd_buf(rdts, total);

    return POOL_OUT;
}

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

static int lpoll(lua_State *L)
{
    rdt_session_t *rdts = get_session(L);
    poll_message_t m;
    int t = pollout(rdts, &m);
    if (t == POOL_EMPTY) {
        t = pollin(rdts, &m);
    }

    if (t != POOL_EMPTY) {
        lua_pushinteger(L, t);
        lua_pushinteger(L, m.sid);
        lua_pushlstring(L, m.buf, m.sz);
        free(m.buf);
        m.buf = NULL;

        return 3;
    }

    return 0;
}

int luaopen_lsocket_server(lua_State *L)
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