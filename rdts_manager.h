//rdt session manager
#ifndef __RDTS_MANAGER_H__
#define __RDTS_MANAGER_H__

#include "rdt_session.h"

struct rdt_manager_s;
typedef struct rdt_manager_s rdt_manager_t;

rdt_manager_t *rdt_manager_create();
rdt_session_t *find_session(rdt_manager_t *mng, int sid, int enable);
rdt_session_t *get_disable_session(rdt_manager_t *mng, int sid);

rdt_session_t *create_session(rdt_manager_t *mng, int sid);
int delete_session(rdt_manager_t *mng, int sid);
int disable_session(rdt_manager_t *mng, int sid);
int ack_session(rdt_manager_t *mng, int sid);
int reconnect_session(rdt_manager_t *mng, int sid);
void on_session_reconnect(rdt_session_t *session);

// class SessionManager {

// public:
//     rdt_session_t *GetSession(int sid);

//     void CreateSession(int sid);
//     int DeleteSession(int sid);

//     int DisableSession(int sid);
//     int AckSession(int sid);
//     int ReconnectSession(int sid);
//     void OnSessionReconnect(rdt_session_t *session);


// private:
//     std::unordered_map<int, rdt_session_t *> session_ctx_;
// };


#endif //__RDTS_MANAGER_H__
