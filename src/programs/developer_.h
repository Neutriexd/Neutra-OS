#ifndef NEUTRA_DEVELOPER_H
#define NEUTRA_DEVELOPER_H

void developer_run(void);
int developer_is_open(void);
int developer_is_minimized(void);
int developer_is_active(void);
void developer_request_minimize(void);
void developer_log(const char* text);

#endif