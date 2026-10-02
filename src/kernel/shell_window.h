#ifndef SHELL_WINDOW_H
#define SHELL_WINDOW_H

void shell_window_init(void);   
void shell_open(void);
void shell_close(void);
int shell_is_open(void);
void shell_redraw(void);        
void shell_set_background_painter(void (*paint)(int x, int y, int w, int h));


void shell_window_mouse(int mouse_x, int mouse_y, int buttons);

#endif