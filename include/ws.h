#ifndef JAG_WS_H
#define JAG_WS_H
#include "value.h"
#include "interp.h"

void ws_server_on(Interp *it, int port);
void ws_server_route(const char *path, Value *handler);
void ws_server_listen(void);

/* SocketConnNative (a `ws: Socket`) method dispatch: .on("message"|"close", fn),
   .send(text), .close(). Returns the argument's own result (usually void),
   or NULL if `method` isn't recognized. */
Value *ws_conn_call_method(Value *conn, const char *method, Value **args, int argc);

#endif
