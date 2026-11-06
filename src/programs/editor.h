#ifndef EDITOR_H
#define EDITOR_H

void init_editor(void);
void editor_open_file(const char* path);
void editor_run(void);

int editor_is_open(void);
int editor_is_minimized(void);
int editor_is_active(void);
void editor_request_minimize(void);

#endif