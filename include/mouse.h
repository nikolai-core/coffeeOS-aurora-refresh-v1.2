#ifndef MOUSE_H
#define MOUSE_H

void mouse_init(void);
void mouse_handle_irq(void);
int mouse_get_state(int *dx, int *dy, int *buttons, int *wheel);
void mouse_get_pos(int *x, int *y);

#endif
