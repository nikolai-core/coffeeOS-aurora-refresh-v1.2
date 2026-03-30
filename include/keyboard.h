#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);
void keyboard_handle_irq(void);
int keyboard_read_char(char *out);

#endif
