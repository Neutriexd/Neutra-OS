#ifndef NEUTRA_JS_PAGE_H
#define NEUTRA_JS_PAGE_H

int js_page_open(void);
int js_page_eval(const char* src, int len);
void js_page_close(void);
const char* js_page_log(void);

#endif
