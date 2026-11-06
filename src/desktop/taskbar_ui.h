#ifndef TASKBAR_UI_H
#define TASKBAR_UI_H

void taskbar_refresh(void);
int taskbar_mouse(int mouse_x, int mouse_y, int buttons);
int taskbar_explorer_button_hit(int mouse_x, int mouse_y);
int taskbar_editor_button_hit(int mouse_x, int mouse_y);
int taskbar_developer_button_hit(int mouse_x, int mouse_y);

#endif