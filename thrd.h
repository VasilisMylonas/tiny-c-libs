#ifndef LIGHTPORT_THREAD_H
#define LIGHTPORT_THREAD_H

#define THRD_NAME_MAX           16
#define THRD_STACK_SIZE_MIN     64000
#define THRD_STACK_SIZE_DEFAULT 1000000
#define THRD_STACK_SIZE_MAX     8000000
#define THRD_PRIORITY_MAX       20
#define THRD_PRIORITY_MIN       -20
#define THRD_PRIORITY_DEFAULT   0

#define THRD_NAME_DEFAULT   "<unnamed>"
#define THRD_NAME_MAIN      "main"
#define THRD_SIGNAL_SUSPEND SIGRTMIN + 1
#define THRD_SIGNAL_RESUME  SIGRTMIN + 2

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

/**
 * Represents a program thread.
 *
 * A thread of execution contains code that runs in parallel with other threads. Each thread has its
 * own stack but shares the code, data and heap segments with other threads. Each thread also has
 * its own unique copy of static variables declared with thread_local.
 *
 * Creating a thread via thrd/thrd_with creates a thread object refering to the
 * created thread. To get an object refering to the current thread use thrd_current.
 */
typedef struct thrd_ex thrd_ex_t;

/**
 * Represents the state of a thread.
 */
typedef enum
{
    /// The thread is executing user code.
    THRD_STATE_RUNNING,

    /// The thread has been suspended via a call to thrd_suspend.
    THRD_STATE_SUSPENDED,

    /// The thread is sleeping via a call to thrd_sleep.
    THRD_STATE_SLEEPING,

    /// The thread has exited, probably via a call to thrd_exit.
    THRD_STATE_EXITED,
} thrd_state_t;

/**
 * Creates a thread executing the provided callback.
 *
 * Errors:
 * EAGAIN - There were not enough system resources to create the thread.
 * ENOTSUP - Thread creation not supported.
 *
 * @pre callback != NULL.
 *
 * @param callback The function to begin execution.
 * @return The thread object.
 */
extern thrd_ex_t* thrd(int (*callback)(void));

/**
 * Creates a thread with the specified properties.
 *
 * Errors:
 * EAGAIN - There were not enough system resources to create the thread.
 * ENOTSUP - Thread creation not supported or invalid stack size provided.
 *
 * @pre callback != NULL.
 * @pre priority <=  THRD_PRIORITY_MAX
 * @pre priority >=  THRD_PRIORITY_MIN
 * @pre stack_size <= THRD_STACK_SIZE_MAX
 * @pre stack_size >= THRD_STACK_SIZE_MIN
 * @pre name != NULL.
 *
 * @param callback The function to begin execution.
 * @param priority The thread priority.
 * @param stack_size The stack size for the thread.
 * @param name The thread name.
 * @return The thread object.
 */
extern thrd_ex_t* thrd_with(int (*callback)(), int priority, size_t stack_size, const char* name);

/**
 * Returns an object representing the current thread.
 *
 * @return The current thread.
 */
extern thrd_ex_t* thrd_current();

/**
 * Gets a string containing the name of a thread.
 *
 * @pre self != NULL.
 * @pre buffer != NULL.
 *
 * @param self The thread.
 * @param buffer The buffer of size THRD_NAME_MAX int o which the name will be stored.
 */
extern void thrd_name(thrd_ex_t* self, char* buffer);

/**
 * Returns the operating system ID of a thread.
 *
 * @pre self != NULL.
 *
 * @param self The thread.
 * @return The thread id.
 */
extern unsigned thrd_id(thrd_ex_t* self);

/**
 * Reports on the current state of a thread.
 *
 * @pre self != NULL.
 *
 * @param self The thread.
 * @return The thread state.
 */
extern thrd_state_t thrd_state(thrd_ex_t* self);

/**
 * Returns the scheduling priority of the specified thread.
 *
 * The priority value has a range of [THRD_PRIORITY_MIN, THRD_PRIORITY_MAX].
 *
 * Errors:
 * ESRCH - No such thread.
 * EPERM - Insufficient permissions.
 *
 * @pre self != NULL.
 *
 * @param self The thread.
 * @return The thread priority.
 */
extern int thrd_priority(thrd_ex_t* self);

/**
 * Sets the scheduling priority for the specified thread.
 *
 * Errors:
 * ESRCH - No such thread.
 * EPERM - Insufficient permissions.
 *
 * @pre self != NULL.
 * @pre priority >= THRD_PRIORITY_MIN.
 * @pre priority <= THRD_PRIORITY_MAX.
 *
 * @param self The thread.
 * @param priority The priority value.
 */
extern void thrd_set_priority(thrd_ex_t* self, int priority);

/**
 * Waits for a thread to finish executing.
 *
 * Errors:
 * ESRCH - No such thread.
 * EPERM - Insufficient permissions or someone is already waiting for the thread.
 * EDEADLK - Attempted to wait for the current thread which would cause a deadlock.
 *
 * @pre self != NULL.
 *
 * @param self The thread to wait for.
 * @return The thread return value.
 */
extern int thrd_wait(thrd_ex_t* self);

/**
 * Waits for a thread to finish executing or until the specified time elapses.
 *
 * Errors:
 * ESRCH - No such thread.
 * EPERM - Insufficient permissions or someone is already waiting for the thread.
 * EDEADLK - Attempted to wait for the current thread which would cause a deadlock.
 * ETIMEDOUT - The specified time elapsed and the thread did not finish executing.
 *
 * @pre self != NULL.
 * @pre milliseconds != 0.
 *
 * @param self The thread to wait for.
 * @param milliseconds The number of milliseconds to wait for the thread.
 * @return The thread return value.
 */
extern int thrd_wait_for(thrd_ex_t* self, unsigned milliseconds);

/**
 * Suspends execution of a thread.
 *
 * Errors:
 * ESRCH - No such thread.
 * EPERM - Insufficient permissions.
 * EBUSY - The thread was already suspended.
 *
 * @pre self != NULL.
 * @param self The thread to suspend.
 */
extern void thrd_suspend(thrd_ex_t* self);

/**
 * Resumes execution of a thread.
 *
 * Errors:
 * ESRCH - No such thread.
 * EPERM - Insufficient permissions.
 * EBUSY - The thread was not suspended.
 *
 * @pre self != NULL.
 * @param self The thread to resume.
 */
extern void thrd_resume(thrd_ex_t* self);

/**
 * Ends execution of the current thread and calls all deferred handlers.
 *
 * After exit the thread state is changed to THRD_STATE_EXITED.
 *
 * @param code The status code.
 */
extern noreturn void thrd_exit(int code);

/**
 * Ends execution of the current thread without calling deferred handlers.
 *
 * After abort the thread state is changed to THRD_STATE_EXITED.
 * The return value of the thread is ECANCELED.
 */
extern noreturn void thrd_abort();

/**
 * Pauses execution of the current thread for a specified amount of time.
 *
 * During sleep the thread state is changed to THRD_STATE_SLEEPING.
 *
 * @pre milliseconds != 0.
 *
 * @param milliseconds The number of milliseconds to pause execution for.
 */
extern void thrd_sleep(unsigned milliseconds);

/**
 * Returns the index of the processor that the current thread is running on.
 *
 * Errors:
 * ENOTSUP - This operation is not supported.
 *
 * @return The processor index.
 */
extern unsigned thrd_processor();

/**
 * Switches to another thread.
 */
extern void thrd_yield();

/**
 * Terminates a thread.
 *
 * @warning This function is inherently unsafe and error-prone.
 *
 * Errors:
 * ESRCH - No such thread.
 * EPERM - Insufficient permissions.
 *
 * @pre self != NULL.
 *
 * @param self The thread.
 */
extern void thrd_kill(thrd_ex_t* self);

// TODO: Not implemented.
typedef struct
{
    char name[THRD_NAME_MAX];
    size_t user_time;
    size_t sys_time;
    size_t start_time;
    size_t guard_size;
    size_t stack_size;
    void* stack_address;
    void* code_address;
    void* context;
    unsigned concurrency;
    unsigned affinity;
    thrd_state_t state;
} thrd_stats_t;

extern void thrd_stats(thrd_ex_t* self, thrd_stats_t* stats);

#endif // LIGHTPORT_THREAD_H
