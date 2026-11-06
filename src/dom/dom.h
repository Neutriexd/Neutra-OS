#ifndef NEUTRA_DOM_H
#define NEUTRA_DOM_H

#include <stdint.h>

#define DOM_MAX 2048
#define DOM_TEXT_POOL 131072
#define DOM_ATTRS 4
#define DOM_NAME 12
#define DOM_VALUE 96
#define DOM_TYPE_ELEMENT 0
#define DOM_TYPE_TEXT 1

typedef struct {
    char name[DOM_NAME];
    char value[DOM_VALUE];
} DomAttr;

typedef struct {
    int type;
    char tag[DOM_NAME];
    DomAttr attrs[DOM_ATTRS];
    int attr_count;
    int text_off;
    int text_len;
    int parent;
    int first;
    int last;
    int next;
} DomNode;

void dom_reset(void);
int dom_parse_document(const uint8_t* b, int n);
const DomNode* dom_node(int i);
int dom_body(void);
const char* dom_attr(int i, const char* name);
void dom_set_attr(int i, const char* name, const char* value);
int dom_get_by_id(const char* id);
int dom_create(const char* tag);
void dom_append(int parent, int child);
int dom_text_content(int i, char* out, int max);
void dom_set_text(int i, const char* text);
void dom_set_html(int i, const char* html, int len);
int dom_collect_tag(const char* tag, int* out, int max);
const char* dom_text_pool_at(int off);

#endif
