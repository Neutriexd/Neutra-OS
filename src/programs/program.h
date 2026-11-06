#ifndef NEUTRA_PROGRAM_H
#define NEUTRA_PROGRAM_H

#include <stdint.h>

#define PROGRAM_WINDOW_MAX 4

int program_run_file(const char* path);
int program_execute_image(const uint8_t* image, int length, const char* title);
int program_list_files(char names[][13], int max);
void program_autostart(void);

int program_windows_mouse(int mouse_x, int mouse_y, int buttons);
void program_windows_paint(void);
int program_window_used(int slot);
const char* program_window_title(int slot);
int program_window_minimized(int slot);
void program_window_toggle(int slot);

void program_draw_icon(int x, int y, int size);

#endif