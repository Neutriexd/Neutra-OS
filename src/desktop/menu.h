#ifndef NEUTRA_MENU_H
#define NEUTRA_MENU_H

#include <stdint.h>

void menu_init(void);
void menu_open(void);
void menu_close(void);
void menu_toggle(void);
int menu_is_open(void);

int menu_mouse(int mouse_x, int mouse_y, int buttons);

void menu_on_scancode(uint8_t scancode);

void menu_run_pending(void);

void menu_redraw_if_open(void);

void menu_request_explorer(void);

void menu_request_editor(void);

void menu_request_developer(void);

void menu_restore_screen(void);

#endif