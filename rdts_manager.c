//rdt session manager

#include "rdts_manager.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <string.h>
#include <stdlib.h>
#define MAXSOCKET 16384

lua_State *gL;

struct rdt_manager_s
{
    rdt_session_t *ctx[MAXSOCKET];
};

static rdt_session_t *find_by_id(rdt_manager_t *mng, int id)
{
    if (id == 0) {
        return NULL;
    }

    int slot = id % MAXSOCKET;
    rdt_session_t *rdts = mng->ctx[slot];
    if (rdts && rdts->sid == id) {
        return rdts;
    }

    return NULL;
}

static void session_on_ack(uint64_t offset, void *session)
{
    rdt_session_t *rdts = (rdt_session_t *)session;
    if (rdts_check_needack(rdts)) {
        rdts_set_needack(rdts, RDTS_NO_ACK);
        rdts_push_raw(rdts);

        //TODO: call lua OnSessionReconnected
        lua_getglobal(gL, "OnSessionReconnected");
        lua_pushinteger(gL, rdts->sid);
        lua_pcall(gL, 1, 0, 0);
    }
}

rdt_session_t *find_session(rdt_manager_t *mng, int sid, int enable)
{
    rdt_session_t *rdts = find_by_id(mng, sid);
    if (rdts == NULL) {
        printf("session not found: %d\n", sid);
        return NULL;
    }

    if (enable && !rdts_check_enable(rdts)) {
        printf("session disable: %d\n", sid);
        return NULL;
    }

    return rdts;
}

rdt_manager_t *rdt_manager_create()
{
    int i;
    rdt_manager_t *mng = (rdt_manager_t *)malloc(sizeof(*mng));
    for (i = 0; i < MAXSOCKET; i++) {
        mng->ctx[i] = NULL;
    }

    return mng;
}

rdt_session_t *create_session(rdt_manager_t *mng, int sid)
{
    rdt_session_t *rdts = find_session(mng, sid, 0);
    if (rdts) {
        delete_session(mng, sid);
    }

    rdts = rdts_create(sid, NULL);
    int slot = sid % MAXSOCKET;
    mng->ctx[slot] = rdts;

    return rdts;
}

int delete_session(rdt_manager_t *mng, int sid)
{
    rdt_session_t *rdts = find_session(mng, sid, 0);
    if (rdts) {
        int slot = sid % MAXSOCKET;
        mng->ctx[slot] = NULL;
        rdts_release(rdts);

        return 0;
    }

    return -1;
}

int disable_session(rdt_manager_t *mng, int sid)
{
    rdt_session_t *rdts = find_session(mng, sid, 1);
    if (rdts) {
        rdts_set_enable(rdts, RDTS_DISABLE);

        return 0;
    }
    return -1;
}

int ack_session(rdt_manager_t *mng, int sid)
{
    rdt_session_t *rdts = find_session(mng, sid, 1);
    if (rdts) {
        rdts_send_ack(rdts);
        return 0;
    }

    return -1;
}

int reconnect_session(rdt_manager_t *mng, int sid)
{
    rdt_session_t *rdts = find_by_id(mng, sid);
    if (rdts == NULL) {
        printf("no such session: %d\n", sid);

        return -1;
    }

    rdts_set_enable(rdts, RDTS_ENABLE);
    rdts_set_needack(rdts, RDTS_ACK);
    rdts_drain_snd_buf(rdts, rdts_get_snd_buf_length(rdts));
    rdts_send_ack(rdts);
    rdts_set_onack(rdts, session_on_ack, (void *)rdts);

    return 0;
}

void on_session_reconnect(rdt_session_t *session)
{

}



// rdt_session_t * SessionManager::GetSession(int sid)
// {
//     auto iter = session_ctx_.find(sid);
//     if (iter == session_ctx_.end()) {
//         return NULL;
//     }

//     if (!rdts_check_enable(iter->second)) {
//         return NULL;
//     }

//     return iter->second;
// }

// int SessionManager::DeleteSession(int sid)
// {
//     auto iter = session_ctx_.find(sid);
//     if (iter == session_ctx_.end()) {
//         return -1;
//     }

//     session_ctx_.erase(sid);
//     rdts_release(iter->second);

//     return 0;
// }

// void SessionManager::CreateSession(int sid)
// {
//     auto iter = session_ctx_.find(sid);
//     if (iter != session_ctx_.end()) {
//         rdts_release(iter->second);
//         session_ctx_.erase(sid);
//     }

//     rdt_session_t *rdts = rdts_create(sid, NULL);
//     session_ctx_.insert(std::make_pair(sid, rdts));
// }

// int SessionManager::DisableSession(int sid)
// {
//     rdt_session_t *rdts = GetSession(sid);
//     if (rdts == NULL) {
//         return -1;
//     }

//     rdts_set_enable(rdts, RDTS_DISABLE);
//     return 0;
// }

// int SessionManager::AckSession(int sid)
// {
//     rdt_session_t *rdts = GetSession(sid);
//     if (rdts == NULL) {
//         return -1;
//     }

//     rdts_send_ack(rdts);
//     return 0;
// }

// int SessionManager::ReconnectSession(int sid)
// {
//     auto iter = session_ctx_.find(sid);
//     if (iter == session_ctx_.end()) {
//         std::cout << "no such session: " << sid << std::endl;
        
//         return -1;
//     }

//     rdt_session_t *rdts = iter->second;
//     rdts_set_enable(rdts, RDTS_ENABLE);
//     rdts_set_needack(rdts, RDTS_ACK);
//     rdts_drain_snd_buf(rdts, rdts_get_snd_buf_length(rdts));
//     rdts_send_ack(rdts);
//     rdts_set_onack(rdts, SessionOnAck, (void *)rdts);

//     return 0;
// }

// void SessionManager::OnSessionReconnect(rdt_session_t *rdts)
// {
//     //TODO call lua function
// }