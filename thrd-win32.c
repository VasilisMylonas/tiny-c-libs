#include "thrd-base.c"

#include <process.h>

static int32_t lprt_win32_thread_prio_from(int32_t value)
{
    if (value < -10)
        return THREAD_PRIORITY_IDLE;
    else if (value < -5)
        return THREAD_PRIORITY_LOWEST;
    else if (value < 0)
        return THREAD_PRIORITY_BELOW_NORMAL;
    else if (value == 0)
        return THREAD_PRIORITY_NORMAL;
    else if (value <= 5)
        return THREAD_PRIORITY_ABOVE_NORMAL;
    else if (value <= 10)
        return THREAD_PRIORITY_HIGHEST;
    else
        return THREAD_PRIORITY_TIME_CRITICAL;
}

static int32_t lprt_win32_thread_prio_value(int32_t prio)
{
    switch (prio)
    {
    case THREAD_PRIORITY_IDLE:
        return -15;
    case THREAD_PRIORITY_LOWEST:
        return -10;
    case THREAD_PRIORITY_BELOW_NORMAL:
        return -5;
    case THREAD_PRIORITY_NORMAL:
        return 0;
    case THREAD_PRIORITY_ABOVE_NORMAL:
        return 5;
    case THREAD_PRIORITY_HIGHEST:
        return 10;
    case THREAD_PRIORITY_TIME_CRITICAL:
        return 15;
    default:
        abort(); // Unreachable.
    }
}

static thread_local lprt_thread_state_t prev;

void lprt_thread_suspend(lprt_thread_t* self)
{
    lprt_guard
    {
        lprt_lock(self);
        lprt_defer(lprt_unlock, self);

        if (self->state == LPRT_THREAD_STATE_SUSPENDED)
        {
            lprt_panic(EBUSY);
        }

        if (SuspendThread(self->handle) == (DWORD)-1)
        {
            CHECK_ERROR_WIN32(0);
        }

        prev = self->state;
        self->state = LPRT_THREAD_STATE_SUSPENDED;
    }
}

void lprt_thread_resume(lprt_thread_t* self)
{
    lprt_guard
    {
        lprt_lock(self);
        lprt_defer(lprt_unlock, self);

        if (self->state != LPRT_THREAD_STATE_SUSPENDED)
        {
            lprt_panic(EBUSY);
        };

        if (ResumeThread(self->handle) == (DWORD)-1)
        {
            CHECK_ERROR_WIN32(0);
        }

        self->state = prev;
    }
}

void lprt_thread_sleep(uint32_t milliseconds)
{
    lprt_require(milliseconds != 0);

    lprt_lock(lprt_current_thread);
    lprt_current_thread->state = LPRT_THREAD_STATE_SLEEPING;
    lprt_unlock(lprt_current_thread);

    Sleep(milliseconds);

    lprt_lock(lprt_current_thread);
    lprt_current_thread->state = LPRT_THREAD_STATE_RUNNING;
    lprt_unlock(lprt_current_thread);
}

void lprt_thread_yield()
{
    SwitchToThread();
}

uint32_t lprt_thread_processor()
{
    return GetCurrentProcessorNumber();
}

int32_t lprt_thread_priority(lprt_thread_t* self)
{
    lprt_require(self != NULL);
    int32_t prio = GetThreadPriority(self->handle);
    if (prio == THREAD_PRIORITY_ERROR_RETURN)
    {
        CHECK_ERROR_WIN32(0);
    }
    return lprt_win32_thread_prio_value(prio);
}

void lprt_thread_set_priority(lprt_thread_t* self, int32_t priority)
{
    lprt_require(self != NULL);
    lprt_require(priority >= LPRT_THREAD_PRIORITY_MIN);
    lprt_require(priority <= LPRT_THREAD_PRIORITY_MAX);
    CHECK_ERROR_WIN32(SetThreadPriority(self->handle, lprt_win32_thread_prio_from(priority)));
}

void lprt_thread_kill(lprt_thread_t* self)
{
    lprt_require(self != NULL);
    CHECK_ERROR_WIN32(TerminateThread(self->handle, EXIT_FAILURE));
    lprt_thread_wait(self);
}

int32_t lprt_thread_wait_for_ps(lprt_thread_t* self, uint32_t milliseconds, int32_t* retval)
{
    DWORD ret = 0;

    switch (WaitForSingleObject(self->handle, milliseconds == 0 ? INFINITE : milliseconds))
    {
    case WAIT_FAILED:
        return GetLastError() == ERROR_ACCESS_DENIED ? EPERM : ESRCH;
    case WAIT_TIMEOUT:
        return ETIMEDOUT;
    default:
        break;
    }

    if (GetExitCodeThread(self->handle, &ret) == 0)
    {
        return GetLastError() == ERROR_ACCESS_DENIED ? EPERM : ESRCH;
    }

    *retval = (int32_t)ret;
    return 0;
}

void lprt_thread_current_init_ps(lprt_thread_t* self)
{
    size_t low_limit = 0;
    size_t high_limit = 0;
    GetCurrentThreadStackLimits(&low_limit, &high_limit);

    self->stack_size = high_limit - low_limit;
    self->handle = GetCurrentThread();
    self->id = GetCurrentThreadId();
}

void lprt_thread_start_ps(lprt_thread_t* thread)
{
    lprt_thread_exit(thread->entry());
}

int32_t lprt_thread_create_ps(lprt_thread_t* self,
                              int32_t priority,
                              void (*callback)(lprt_thread_t*))
{
    self->handle = (void*)_beginthreadex(NULL,
                                         self->stack_size,
                                         (unsigned (*)(void*))(void (*)(void))callback,
                                         self,
                                         0,
                                         (uint32_t*)&self->id);

    if (self->handle == NULL)
    {
        return errno == EINVAL ? ENOTSUP : EAGAIN;
    }

    SetThreadPriority(self->handle, priority);
    return 0;
}

void lprt_thread_exit_ps(int32_t code)
{
    _endthreadex((uint32_t)code);
}

static void lprt_thread_destroy(void* s)
{
    lprt_thread_t* self = s;

    if (self->do_not_detach)
    {
        return;
    }

    CloseHandle(self->handle);
}
