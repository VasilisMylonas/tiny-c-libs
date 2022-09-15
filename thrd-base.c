#include "thrd.h"

#include <assert.h>
#include <stdlib.h>

#ifdef LPRT_WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#include <windows.h>
#else
#include <pthread.h>
#endif

//
// Possibilities for thread lifecycle:
//
// Creation.
// Failure (no thread is created)
//
// Creation
// Exit (lprt_destroy by child and cleanup)
// Join (do_not_detach is set, lprt_destroy by parent deallocates the memory)
//
// Creation
// Detachment (lprt_destroy by parent detaches the thread)
// Exit (lprt_destroy by child deallocates the memory and does cleanup)
//
struct thrd
{
#ifdef LPRT_WIN32
    void* handle;
#else
    pthread_t handle;
    pthread_barrier_t* barrier;
#endif
    char name[THRD_NAME_MAX];
    int (*volatile entry)(void);
    volatile size_t stack_size;
    volatile unsigned id;
    volatile thrd_state_t state;
    volatile bool do_not_detach;
};

static int thrd_create_ps(thrd_ex_t* self, int priority, void (*callback)(thrd_ex_t*));
static void thrd_start_ps(thrd_ex_t* self);
static void thrd_current_init_ps(thrd_ex_t* self);
static noreturn void thrd_exit_ps(int code);
static int thrd_wait_for_ps(thrd_ex_t* self, unsigned milliseconds, int* retval);
static void thrd_destroy(void*);
static int thrd_wait_for_impl(thrd_ex_t* self, unsigned milliseconds);

extern int main();
static thread_local thrd_ex_t* lprt_current_thread = NULL;

// From defer.c
void _lprt_rt_unwind(int error, bool recoverable);

LPRT_GENERATE_TYPE_INFO(thrd_ex_t, thrd_destroy);

thrd_ex_t* thrd(int (*callback)(void))
{
    return thrd_with(callback, THRD_PRIORITY_DEFAULT, THRD_STACK_SIZE_DEFAULT, THRD_NAME_DEFAULT);
}

void thrd_name(thrd_ex_t* self, char* buffer)
{
    assert(self != NULL);
    assert(buffer != NULL);

    lprt_lock(self);
    str_copy(self->name, buffer, THRD_NAME_MAX);
    lprt_unlock(self);
}

unsigned thrd_id(thrd_ex_t* self)
{
    assert(self != NULL);

    // No locks required because the ID is constant.
    return self->id;
}

thrd_state_t thrd_state(thrd_ex_t* self)
{
    assert(self != NULL);

    // TODO: locks possibly required.
    return self->state;
}

thrd_ex_t* thrd_current()
{
    return lprt_retain(lprt_current_thread);
}

int thrd_wait(thrd_ex_t* self)
{
    assert(self != NULL);
    return thrd_wait_for_impl(self, 0);
}

int thrd_wait_for(thrd_ex_t* self, unsigned milliseconds)
{
    assert(self != NULL);
    assert(milliseconds != 0);
    return thrd_wait_for_impl(self, milliseconds);
}

static void thrd_start(thrd_ex_t* self)
{
    lprt_current_thread = self;
    thrd_start_ps(self);
}

thrd_ex_t* thrd_with(int (*callback)(void), int priority, size_t stack_size, const char* name)
{
    assert(callback != NULL);
    assert(priority <= THRD_PRIORITY_MAX);
    assert(priority >= THRD_PRIORITY_MIN);
    assert(stack_size <= THRD_STACK_SIZE_MAX);
    assert(stack_size >= THRD_STACK_SIZE_MIN);
    assert(name != NULL);

    thrd_ex_t* self = lprt_alloc(lprt_typeid(thrd_ex_t));
    self->stack_size = stack_size;

    str_copy(name, self->name, THRD_NAME_MAX);
    self->entry = callback;
    self->state = THRD_STATE_RUNNING;

    lprt_retain(self);

    int status = thrd_create_ps(self, priority, thrd_start);
    if (status != 0)
    {
        // Call this twice because of lprt_strong_ref
        self->do_not_detach = true;
        lprt_destroy(self);
        lprt_destroy(self);
        lprt_panic(status);
    }

    return self;
}

static int thrd_wait_for_impl(thrd_ex_t* self, unsigned milliseconds)
{
    if (self == lprt_current_thread)
    {
        lprt_panic(EDEADLK);
    }

    int retval = 0;

    int result = thrd_wait_for_ps(self, milliseconds, &retval);
    if (result != 0)
    {
        lprt_panic(result);
    }

    self->do_not_detach = true;
    return retval;
}

void thrd_exit(int code)
{
    lprt_lock(lprt_current_thread);

    // Ensure that atexit doesn't destroy the thread twice.
    if (lprt_current_thread->entry == main)
    {
        lprt_unlock(lprt_current_thread);
        thrd_exit_ps(code);
    }

    // TODO: _df_unwind(0, false);
    lprt_current_thread->state = THRD_STATE_EXITED;

    lprt_unlock(lprt_current_thread);
    lprt_destroy(lprt_current_thread);

    thrd_exit_ps(code);
}

void thrd_abort()
{
    lprt_lock(lprt_current_thread);

    // Ensure that atexit doesn't destroy the thread twice.
    if (lprt_current_thread->entry == main)
    {
        lprt_unlock(lprt_current_thread);
        thrd_exit_ps(ECANCELED);
    }

    lprt_current_thread->state = THRD_STATE_EXITED;

    lprt_unlock(lprt_current_thread);
    lprt_destroy(lprt_current_thread);

    thrd_exit_ps(ECANCELED);
}

static void thrd_module_fini()
{
    lprt_destroy(lprt_current_thread);
}

void _thrd_module_init()
{
    lprt_current_thread = lprt_alloc(lprt_typeid(thrd_ex_t));
    thrd_current_init_ps(lprt_current_thread);
    lprt_current_thread->state = THRD_STATE_RUNNING;
    lprt_current_thread->entry = main;
    str_copy(THRD_NAME_MAIN, lprt_current_thread->name, THRD_NAME_MAX);

    atexit(thrd_module_fini);
}
