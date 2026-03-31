#ifndef SYSCALL_NUMBERS_H
#define SYSCALL_NUMBERS_H

enum {
    SYS_WRITE = 0,
    SYS_READCHAR = 1,
    SYS_EXIT = 2,
    SYS_GETTIME = 3,
    SYS_AUDIO_PLAY = 4,
    SYS_OPEN = 5,
    SYS_CLOSE = 6,
    SYS_READ = 7,
    SYS_WRITE_FD = 8,
    SYS_SEEK = 9,
    SYS_MKDIR = 10,
    SYS_DELETE = 11,
    SYS_STAT = 12,
    SYS_LISTDIR = 13
};

#endif
