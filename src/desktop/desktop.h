#ifndef DESKTOP_H
#define DESKTOP_H

void init_desk(void);
void redraw_desk_region(int x, int y, int width, int height);
int desktop_pointer(int mouse_x, int mouse_y, int buttons);

#endif
