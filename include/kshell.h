#ifndef KSHELL_H
#define KSHELL_H

void kshell_dispatch_command(const char *line);
void kshell_run(void);
void user_mode_entry(void);

#endif
