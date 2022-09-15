#include "thrd-base.c"

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <signal.h>
#include <stdio.h>

static int lprt_posix_thread_prio_from(int value)
{
    return -value;
}

static int lprt_posix_thread_prio_value(int prio)
{
    return -prio;
}

static void handle_suspend_resume(int signal)
{
    // The resume signal will be handled by sigwait.
    if (signal == THRD_SIGNAL_RESUME)
    {
        return;
    }

    sigset_t set;
    sigaddset(&set, THRD_SIGNAL_RESUME);

    lprt_lock(lprt_current_thread);
    // Save the current state, before updating.
    thrd_state_t prev = lprt_current_thread->state;
    lprt_current_thread->state = THRD_STATE_SUSPENDED;
    lprt_unlock(lprt_current_thread);

    // Notify thrd_suspend that we have been suspended.
    lprt_signal_all(lprt_current_thread);

    // Wait for THRD_SIGNAL_RESUME.
    sigwait(&set, &signal);

    lprt_lock(lprt_current_thread);
    // Restore the previous state.
    lprt_current_thread->state = prev;
    lprt_unlock(lprt_current_thread);

    // Notify thrd_resume that we have resumed.
    lprt_signal_all(lprt_current_thread);
}

LPRT_DEFER_SCOPED_VOID(thrd_suspend, (thrd_ex_t * self), self)
{
    lprt_lock(self);
    lprt_defer(lprt_unlock, self);

    if (self->state == THRD_STATE_SUSPENDED)
    {
        lprt_panic(EBUSY);
    };

    CHECK_ERROR_POSIX(pthread_kill(self->handle, THRD_SIGNAL_SUSPEND));

    while (self->state != THRD_STATE_SUSPENDED)
    {
        lprt_wait(self);
    }
}

void thrd_resume(thrd_ex_t* self)
{
    lprt_lock(self);
    lprt_defer(lprt_unlock, self);

    if (self->state != THRD_STATE_SUSPENDED)
    {
        lprt_panic(EBUSY);
    }

    CHECK_ERROR_POSIX(pthread_kill(self->handle, THRD_SIGNAL_RESUME));

    while (self->state == THRD_STATE_SUSPENDED)
    {
        lprt_wait(self);
    }
}

void thrd_sleep(unsigned milliseconds)
{
    assert(milliseconds != 0);

    lprt_lock(lprt_current_thread);
    lprt_current_thread->state = THRD_STATE_SLEEPING;
    lprt_unlock(lprt_current_thread);

    struct timespec t = _lprt_timespec_from_ms(milliseconds);
    while (nanosleep(&t, &t) != 0)
    {
        lprt_lock(lprt_current_thread);
        lprt_current_thread->state = THRD_STATE_SLEEPING;
        lprt_unlock(lprt_current_thread);
    }

    lprt_lock(lprt_current_thread);
    lprt_current_thread->state = THRD_STATE_RUNNING;
    lprt_unlock(lprt_current_thread);
}

void thrd_yield()
{
    sched_yield();
}

unsigned thrd_processor()
{
    int cpu = sched_getcpu();
    if (cpu == -1)
    {
        lprt_panic(ENOTSUP);
    }
    return cpu;
}

int thrd_priority(thrd_ex_t* self)
{
    assert(self != NULL);

    struct sched_param p;
    int policy;
    CHECK_ERROR_POSIX(pthread_getschedparam(self->handle, &policy, &p));
    return lprt_posix_thread_prio_value(p.sched_priority);
}

void thrd_set_priority(thrd_ex_t* self, int priority)
{
    assert(self != NULL);
    assert(priority >= THRD_PRIORITY_MIN);
    assert(priority <= THRD_PRIORITY_MAX);
    CHECK_ERROR_POSIX(pthread_setschedprio(self->handle, lprt_posix_thread_prio_from(priority)));
}

void thrd_kill(thrd_ex_t* self)
{
    assert(self != NULL);
    CHECK_ERROR_POSIX(pthread_cancel(self->handle));
    thrd_wait(self);
}

int thrd_wait_for_ps(thrd_ex_t* self, unsigned milliseconds, int* retval)
{
    void* ret;
    int result = 0;

    if (milliseconds == 0)
    {
        result = pthread_join(self->handle, &ret);
    }
    else
    {
        struct timespec ts = _lprt_timespec_from_ms(milliseconds);
        result = pthread_timedjoin_np(self->handle, &ret, &ts);
    }

    switch (result)
    {
    case EDEADLK:
        return EDEADLK;
    case ETIMEDOUT:
        return ETIMEDOUT;
    case EINVAL:
        return EPERM;
    case ESRCH:
        return ESRCH;
    default:
        break;
    }

    *retval = (int)(unsignedptr_t)ret;
    return 0;
}

void thrd_current_init_ps(thrd_ex_t* self)
{
    struct rlimit l;
    assert(getrlimit(RLIMIT_STACK, &l) == 0);
    self->stack_size = l.rlim_cur;
    self->id = syscall(SYS_gettid);
    self->handle = pthread_self();
}

void thrd_start_ps(thrd_ex_t* self)
{
    int (*entry)(void) = self->entry;
    self->id = syscall(SYS_gettid);
    pthread_barrier_wait(self->barrier);
    thrd_exit(entry());
}

int thrd_create_ps(thrd_ex_t* self, int priority, void (*callback)(thrd_ex_t*))
{
    // Install signal handlers.
    struct sigaction si;
    sigemptyset(&si.sa_mask);
    si.sa_handler = handle_suspend_resume;
    si.sa_flags = 0;
    sigaction(THRD_SIGNAL_SUSPEND, &si, NULL);
    sigaction(THRD_SIGNAL_RESUME, &si, NULL);

    pthread_barrier_t barrier;
    int status = 0;

    if (pthread_barrier_init(&barrier, NULL, 2) != 0)
    {
        return EAGAIN;
    }

    self->barrier = &barrier;

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
    {
        pthread_barrier_destroy(&barrier);
        return EAGAIN;
    }

    struct sched_param p = {
        .sched_priority = priority,
    };

    if (pthread_attr_setschedparam(&attr, &p) != 0 ||
        pthread_attr_setstacksize(&attr, self->stack_size) != 0)
    {
        status = ENOTSUP;
        goto cleanup;
    }

    if (pthread_create(&self->handle, &attr, (void* (*)(void*))(void (*)(void))callback, self) != 0)
    {
        status = EAGAIN;
        goto cleanup;
    }

    // sync with thrd_start
    pthread_barrier_wait(&barrier);

cleanup:
    pthread_attr_destroy(&attr);
    pthread_barrier_destroy(&barrier);
    return status;
}

void thrd_exit_ps(int code)
{
    pthread_exit((void*)(unsignedptr_t)code);
}

static void thrd_destroy(void* s)
{
    thrd_ex_t* self = s;

    if (self->do_not_detach)
    {
        return;
    }

    pthread_detach(self->handle);
}
