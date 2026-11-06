#include <stddef.h>
#include "quickjs/quickjs.h"
#include "js_host.h"

static char* out_buf;
static int out_max;
static int out_pos;

static void out_append(const char* s, size_t n) {
    for (size_t i = 0; i < n && out_pos < out_max - 1; i++) out_buf[out_pos++] = s[i];
    out_buf[out_pos] = '\0';
}

static JSValue js_console_log(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    for (int i = 0; i < argc; i++) {
        size_t len = 0;
        const char* s = JS_ToCStringLen(ctx, &len, argv[i]);
        if (s) {
            if (i > 0) out_append(" ", 1);
            out_append(s, len);
            JS_FreeCString(ctx, s);
        }
    }
    out_append("\n", 1);
    return JS_UNDEFINED;
}

int js_run(const char* src, int len, char* out, int outmax) {
    out_buf = out;
    out_max = outmax;
    out_pos = 0;
    out_buf[0] = '\0';

    JSRuntime* rt = JS_NewRuntime();
    if (!rt) return -1;
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return -2;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);

    JSValue result = JS_Eval(ctx, src, (size_t)len, "<script>", JS_EVAL_TYPE_GLOBAL);
    int status = 0;
    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        if (msg) {
            size_t n = 0;
            while (msg[n]) n++;
            out_append(msg, n);
            JS_FreeCString(ctx, msg);
        }
        out_append("\n", 1);
        JS_FreeValue(ctx, exc);
        status = -3;
    }
    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return status;
}
