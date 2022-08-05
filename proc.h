#ifndef PROC_H
#define PROC_H

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

#define PROC_NAME_MAX         16
#define PROC_PRIORITY_MAX     20
#define PROC_PRIORITY_MIN     -20
#define PROC_PRIORITY_DEFAULT 0

typedef struct proc proc_t;

extern int proc_create(proc_t* self, const char* args[]);
extern int proc_with(proc_t* self,
                     const char* args[],
                     const char* env[],
                     int priority,
                     bool detached,
                     bool change_directory);
extern int proc_from_id(proc_t* self, unsigned id);
extern int proc_from_name(proc_t* self, const char* name);
extern void proc_destroy(proc_t* self);
extern proc_t* proc_current();
extern int proc_name(proc_t* self, char* buffer);
extern unsigned proc_id(proc_t* self);
extern int proc_priority(proc_t* self, int* priority);
extern int proc_set_priority(proc_t* self, int priority);
extern int proc_session(proc_t* self, unsigned* session);
extern int proc_wait(proc_t* self, int* retval);
extern int proc_wait_for(proc_t* self, unsigned milliseconds, int* retval);
extern int proc_suspend(proc_t* self);
extern int proc_resume(proc_t* self);
extern noreturn void proc_exit(int status);
extern int proc_kill(proc_t* self);
extern proc_t* proc_parent();

// Not supported on win32
extern unsigned proc_group(proc_t* self);

typedef enum
{
    PROC_STATE_RUNNING,
    PROC_STATE_SUSPENDED,
    PROC_STATE_SLEEPING,
    PROC_STATE_EXITED,
} proc_state_t;

typedef struct
{
    char name[PROC_NAME_MAX];
    size_t user_time;
    size_t sys_time;
    size_t start_time;
    size_t vmem_size;
    size_t swap_size;
    size_t rss;
    size_t page_faults;
    size_t bytes_read;
    size_t bytes_written;
    unsigned handles;
    unsigned threads;
    proc_state_t state;
} proc_stats_t;

extern void proc_stats(proc_t* self, proc_stats_t* stats);

struct proc
{
    void* handle;
    long id;
};

#endif // PROC_H
