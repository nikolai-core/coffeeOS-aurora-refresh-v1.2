#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KEY_F5 0x81u
#define KEY_DELETE 0x7Fu

void keyboard_init(void);
void keyboard_handle_irq(void);
int keyboard_read_char(char *out);
int keyboard_ctrl_held(void);

#endif
