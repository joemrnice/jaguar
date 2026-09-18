#ifndef JAG_HTTP_H
#define JAG_HTTP_H
#include "value.h"
#include "interp.h"

void http_server_on(Interp *it, int port);
void http_server_route(const char *method, const char *path_pattern, Value *handler);
void http_server_listen(void);

/* Synchronous (blocking) client requests - see DESIGN_DECISIONS.md for why
   these don't suspend via a coroutine. Always returns a Task (V_NATIVE);
   never NULL. */
Value *http_client_get(const char *url);
Value *http_client_post(const char *url, Value *body);

/* Response-object ( res ) method dispatch: .send/.json/.status/.header.
   `obj` must be a response object created by http_make_response(). Returns
   the (possibly same, for chaining) value, or NULL if `method` isn't one
   of the response methods (caller should report an error in that case). */
Value *http_response_call_method(Value *obj, const char *method, Value **args, int argc);
Value *http_make_response(void);
int http_is_response_object(Value *v);

#endif
