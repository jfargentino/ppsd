#ifndef SLOG_H
#define SLOG_H
#include <stdio.h>

#define flog(file, fmt, ...) if ((file) != NULL) {                          \
                                 (void)fprintf((file), fmt, ##__VA_ARGS__); \
                                 (void)fflush(file); }

#ifndef SLOG_CMT_STR
    #define SLOG_CMT_STR "#"
#endif

#define fcmt(file, fmt, ...) flog((file), "%s" fmt, SLOG_CMT_STR, ##__VA_ARGS__)

#define fdbg(file, fmt, ...) fcmt((file),         \
                                  "[%s:%d]" fmt,  \
                                  __FILE__,       \
                                  __LINE__,       \
                                  ##__VA_ARGS__);

#define ASS
#ifndef STDASS
    #define STDASS stderr
#endif
#ifdef ASS
    #include <stdlib.h>
    #define _ass(cond, fmt, ...) \
    while(!(cond)) { \
        fdbg(STDASS, fmt, ##__VA_ARGS__); \
        exit (EXIT_FAILURE); \
    }
#else
    #define _ass(cond, fmt, ...)
#endif
#define ass(cond) _ass(cond, "!!! \"%s\" is false !!!!\n", #cond)

#ifndef STDOUT
    #define STDOUT NULL
    #define slogout(fmt,...)
    #define slogcmt(fmt,...)
#else
    #define slogout(fmt,...) flog(STDOUT, fmt, ##__VA_ARGS__)
    #define slogcmt(fmt,...) fcmt(STDOUT, fmt, ##__VA_ARGS__)
#endif // STDOUT

#ifdef STDERR
    #define slogerr(fmt,...) flog(STDERR, fmt, ##__VA_ARGS__)
#else
    #define STDERR NULL
    #define slogerr(fmt,...)
#endif // STDERR

#ifndef STDDBG
    #define STDDBG NULL
    #define slogdbg(fmt,...)
#else
    #define slogdbg(fmt,...) fdbg(STDDBG, fmt, ##__VA_ARGS__)
#endif // STDDBG

#endif

