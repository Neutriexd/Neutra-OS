#include <stddef.h>
#include "quickjs/quickjs.h"
#include "dom.h"
#include "js_page.h"

static JSRuntime* page_rt;
static JSContext* page_ctx;
static char page_log[1024];
static int interrupt_budget;
static int page_log_len;

static int interrupt_check(JSRuntime* rt, void* opaque) {
    (void)rt;
    (void)opaque;
    return --interrupt_budget <= 0;
}

static void log_append(const char* s, size_t n) {
    for (size_t i = 0; i < n && page_log_len < (int)sizeof(page_log) - 1; i++) page_log[page_log_len++] = s[i];
    page_log[page_log_len] = '\0';
}

static JSValue n_console_log(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    for (int i = 0; i < argc; i++) {
        size_t len = 0;
        const char* s = JS_ToCStringLen(ctx, &len, argv[i]);
        if (s) {
            if (i > 0) log_append(" ", 1);
            log_append(s, len);
            JS_FreeCString(ctx, s);
        }
    }
    log_append("\n", 1);
    return JS_UNDEFINED;
}

static int arg_int(JSContext* ctx, JSValueConst v, int* out) {
    int32_t x = 0;
    if (JS_ToInt32(ctx, &x, v) != 0) return -1;
    *out = (int)x;
    return 0;
}

static JSValue n_get_by_id(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    const char* id = JS_ToCString(ctx, argv[0]);
    const int r = id ? dom_get_by_id(id) : -1;
    if (id) JS_FreeCString(ctx, id);
    return JS_NewInt32(ctx, r);
}

static JSValue n_create(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    const char* tag = JS_ToCString(ctx, argv[0]);
    const int r = tag ? dom_create(tag) : -1;
    if (tag) JS_FreeCString(ctx, tag);
    return JS_NewInt32(ctx, r);
}

static JSValue n_body(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, dom_body());
}

static JSValue n_append(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    int p = -1;
    int c = -1;
    arg_int(ctx, argv[0], &p);
    arg_int(ctx, argv[1], &c);
    dom_append(p, c);
    return JS_UNDEFINED;
}

static JSValue n_get_text(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    int i = -1;
    arg_int(ctx, argv[0], &i);
    static char buf[8192];
    dom_text_content(i, buf, sizeof(buf));
    return JS_NewString(ctx, buf);
}

static JSValue n_set_text(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    int i = -1;
    arg_int(ctx, argv[0], &i);
    const char* s = JS_ToCString(ctx, argv[1]);
    if (s) {
        dom_set_text(i, s);
        JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}

static JSValue n_set_html(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    int i = -1;
    arg_int(ctx, argv[0], &i);
    size_t len = 0;
    const char* s = JS_ToCStringLen(ctx, &len, argv[1]);
    if (s) {
        dom_set_html(i, s, (int)len);
        JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}

static JSValue n_get_attr(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    int i = -1;
    arg_int(ctx, argv[0], &i);
    const char* name = JS_ToCString(ctx, argv[1]);
    const char* v = name ? dom_attr(i, name) : 0;
    if (name) JS_FreeCString(ctx, name);
    if (!v) return JS_NULL;
    return JS_NewString(ctx, v);
}

static JSValue n_set_attr(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    int i = -1;
    arg_int(ctx, argv[0], &i);
    const char* name = JS_ToCString(ctx, argv[1]);
    const char* value = JS_ToCString(ctx, argv[2]);
    if (name && value) dom_set_attr(i, name, value);
    if (name) JS_FreeCString(ctx, name);
    if (value) JS_FreeCString(ctx, value);
    return JS_UNDEFINED;
}

static JSValue n_tag(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    (void)this_val;
    (void)argc;
    int i = -1;
    arg_int(ctx, argv[0], &i);
    const DomNode* n = dom_node(i);
    if (!n) return JS_NULL;
    return JS_NewString(ctx, n->tag);
}

static const char prelude[] =
    "function __wrap(i) {\n"
    "  if (i < 0) return null;\n"
    "  return {\n"
    "    __i: i,\n"
    "    get id() { var v = __dom_get_attr(this.__i, 'id'); return v === null ? '' : v; },\n"
    "    get tagName() { return __dom_tag(this.__i); },\n"
    "    get textContent() { return __dom_get_text(this.__i); },\n"
    "    set textContent(v) { __dom_set_text(this.__i, String(v)); },\n"
    "    set innerHTML(v) { __dom_set_html(this.__i, String(v)); },\n"
    "    appendChild(c) { __dom_append(this.__i, c.__i); return c; },\n"
    "    setAttribute(n, v) { __dom_set_attr(this.__i, String(n), String(v)); },\n"
    "    getAttribute(n) { return __dom_get_attr(this.__i, String(n)); }\n"
    "  };\n"
    "}\n"
    "var document = {\n"
    "  getElementById(id) { return __wrap(__dom_get_by_id(String(id))); },\n"
    "  createElement(t) { return __wrap(__dom_create(String(t))); },\n"
    "  get body() { return __wrap(__dom_body()); }\n"
    "};\n"
    "var window = this;\n";

static void install_natives(JSContext* ctx, JSValue global) {
    JS_SetPropertyStr(ctx, global, "__dom_get_by_id", JS_NewCFunction(ctx, n_get_by_id, "__dom_get_by_id", 1));
    JS_SetPropertyStr(ctx, global, "__dom_create", JS_NewCFunction(ctx, n_create, "__dom_create", 1));
    JS_SetPropertyStr(ctx, global, "__dom_body", JS_NewCFunction(ctx, n_body, "__dom_body", 0));
    JS_SetPropertyStr(ctx, global, "__dom_append", JS_NewCFunction(ctx, n_append, "__dom_append", 2));
    JS_SetPropertyStr(ctx, global, "__dom_get_text", JS_NewCFunction(ctx, n_get_text, "__dom_get_text", 1));
    JS_SetPropertyStr(ctx, global, "__dom_set_text", JS_NewCFunction(ctx, n_set_text, "__dom_set_text", 2));
    JS_SetPropertyStr(ctx, global, "__dom_set_html", JS_NewCFunction(ctx, n_set_html, "__dom_set_html", 2));
    JS_SetPropertyStr(ctx, global, "__dom_get_attr", JS_NewCFunction(ctx, n_get_attr, "__dom_get_attr", 2));
    JS_SetPropertyStr(ctx, global, "__dom_set_attr", JS_NewCFunction(ctx, n_set_attr, "__dom_set_attr", 3));
    JS_SetPropertyStr(ctx, global, "__dom_tag", JS_NewCFunction(ctx, n_tag, "__dom_tag", 1));
}

int js_page_open(void) {
    js_page_close();
    page_log_len = 0;
    page_log[0] = '\0';
    page_rt = JS_NewRuntime();
    if (!page_rt) return -1;
    JS_SetInterruptHandler(page_rt, interrupt_check, 0);
    interrupt_budget = 2000;
    page_ctx = JS_NewContext(page_rt);
    if (!page_ctx) {
        JS_FreeRuntime(page_rt);
        page_rt = 0;
        return -2;
    }
    JSValue global = JS_GetGlobalObject(page_ctx);
    JSValue console = JS_NewObject(page_ctx);
    JS_SetPropertyStr(page_ctx, console, "log", JS_NewCFunction(page_ctx, n_console_log, "log", 1));
    JS_SetPropertyStr(page_ctx, global, "console", console);
    install_natives(page_ctx, global);
    JS_FreeValue(page_ctx, global);
    JSValue r = JS_Eval(page_ctx, prelude, sizeof(prelude) - 1, "<prelude>", JS_EVAL_TYPE_GLOBAL);
    const int bad = JS_IsException(r);
    JS_FreeValue(page_ctx, r);
    return bad ? -3 : 0;
}

int js_page_eval(const char* src, int len) {
    if (!page_ctx) return -1;
    interrupt_budget = 2000;
    JSValue r = JS_Eval(page_ctx, src, (size_t)len, "<script>", JS_EVAL_TYPE_GLOBAL);
    int status = 0;
    if (JS_IsException(r)) {
        JSValue exc = JS_GetException(page_ctx);
        const char* msg = JS_ToCString(page_ctx, exc);
        log_append("Error: ", 7);
        if (msg) {
            size_t n = 0;
            while (msg[n]) n++;
            log_append(msg, n);
            JS_FreeCString(page_ctx, msg);
        }
        log_append("\n", 1);
        JS_FreeValue(page_ctx, exc);
        status = -1;
    }
    JS_FreeValue(page_ctx, r);
    return status;
}

void js_page_close(void) {
    if (page_ctx) {
        JS_FreeContext(page_ctx);
        page_ctx = 0;
    }
    if (page_rt) {
        JS_FreeRuntime(page_rt);
        page_rt = 0;
    }
}

const char* js_page_log(void) {
    return page_log;
}