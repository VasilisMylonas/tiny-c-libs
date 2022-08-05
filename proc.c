#include "proc.h"

#include <assert.h>
#include <stdlib.h>

#define PROC_WIN32_MAX_PIDS 1024
#define PROC_PATH_MAX       32
#define PROC_NAME_PATH      "/proc/%u/comm"
#define PROC_IO_PATH        "/proc/%u/io"
#define PROC_FD_PATH        "/proc/%u/fdinfo"
#define PROC_STAT_PATH      "/proc/%u/stat"
#define PROC_IO_FORMAT      "%*s %zu %*s %zu"
#define PROC_STAT_FORMAT                                                                           \
    "%*u (%16s %c %*u %*u %*u %*u %*u %*u %zu %*u %zu %*u %zu %zu %*u %*u %*i %*i %u %*d %zu %zu " \
    "%zu %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %zu"

static int proc_create_ps(proc_t* self,
                          const char* args[],
                          const char* env[],
                          int priority,
                          bool detached,
                          bool change_directory);
static void proc_current_ps(proc_t* self);
static void proc_parent_ps(proc_t* self);
static int proc_wait_for_ps(proc_t* self, unsigned milliseconds, int* retval);

//
//
// Begin platform-specific
//
//

#ifdef _WIN32

#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <windows.h>

#include <direct.h>
#include <process.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>

#define CHECK_ERROR_WIN32(call)                                                                    \
    if (call == 0)                                                                                 \
    {                                                                                              \
        return GetLastError() == ERROR_ACCESS_DENIED ? EPERM : ESRCH;                              \
    }

static const char* lprt_win32_dirname(const char* path)
{
    static thread_local char dir[_MAX_DRIVE + _MAX_DIR - 1];
    _splitpath_s(path, dir, _MAX_DRIVE, dir + _MAX_DRIVE - 1, _MAX_DIR, NULL, 0, NULL, 0);
    return dir;
}

int proc_from_id(proc_t* self, unsigned id)
{
    assert(self != NULL);
    assert(id != 0);

    self->id = (long)id;
    self->handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, id);
    if (self->handle == NULL)
    {
        CHECK_ERROR_WIN32(0);
    }

    return 0;
}

int proc_kill(proc_t* self)
{
    assert(self != NULL);
    CHECK_ERROR_WIN32(TerminateProcess(self->handle, EXIT_FAILURE));
    return 0;
}

int proc_suspend(proc_t* self)
{
    assert(self != NULL);
    CHECK_ERROR_WIN32(DebugActiveProcess(self->id));
    return 0;
}

int proc_resume(proc_t* self)
{
    assert(self != NULL);
    CHECK_ERROR_WIN32(DebugActiveProcessStop(self->id));
    return 0;
}

int proc_priority(proc_t* self, int* priority)
{
    assert(self != NULL);
    DWORD p = GetPriorityClass(self->handle);
    CHECK_ERROR_WIN32(p);
    *priority = lprt_win32_process_prio_value(p);
    return 0;
}

int proc_set_priority(proc_t* self, int priority)
{
    assert(self != NULL);
    assert(priority <= PROC_PRIORITY_MAX);
    assert(priority >= PROC_PRIORITY_MIN);
    CHECK_ERROR_WIN32(SetPriorityClass(self->handle, lprt_win32_process_prio_from(priority)));
    return 0;
}

int proc_session(proc_t* self, unsigned* session)
{
    assert(self != NULL);
    assert(session != NULL);

    DWORD temp;
    CHECK_ERROR_WIN32(ProcessIdToSessionId(self->id, &temp));
    *session = temp;
    return 0;
}

unsigned proc_group(proc_t* self)
{
    assert(self != NULL);
    return 0;
}

int proc_wait_for_ps(proc_t* self, unsigned milliseconds, int* retval)
{
    if (milliseconds == 0)
    { // not a timed wait
        milliseconds = INFINITE;
    }

    switch (WaitForSingleObject(self->handle, milliseconds))
    {
    case WAIT_OBJECT_0:
        break;
    case WAIT_TIMEOUT:
        return ETIMEDOUT;
    default:
        CHECK_ERROR_WIN32(0);
    }

    DWORD temp;
    CHECK_ERROR_WIN32(GetExitCodeProcess(self->handle, &temp));
    *retval = temp;
    return 0;
}

int proc_name(proc_t* self, char* buffer)
{
    assert(self != NULL);
    assert(buffer != NULL);
    CHECK_ERROR_WIN32(GetProcessImageFileName(self->handle, buffer, PROC_NAME_MAX));
    return 0;
}

void proc_destroy(proc_t* self)
{
    assert(self != NULL);

    if (self->handle != NULL)
    {
        CloseHandle(self->handle);
    }
}

int proc_from_name(proc_t* self, const char* name)
{
    assert(name != NULL);

    DWORD pids[PROC_WIN32_MAX_PIDS];
    DWORD size;
    if (EnumProcesses(pids, sizeof(pids), &size) == 0)
    {
        return EAGAIN;
    }

    char temp[PROC_NAME_MAX];

    for (DWORD i = 0; i < PROC_WIN32_MAX_PIDS; i++)
    {
        HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pids[i]);
        if (h == NULL)
        {
            continue;
        }

        DWORD status = GetProcessImageFileName(h, temp, PROC_NAME_MAX);
        CloseHandle(h);

        if (status == 0)
        {
            continue;
        }

        if (strcmp(temp, name) == 0)
        {
            return proc_from_id(self, pids[i]);
        }
    }

    return EAGAIN;
}

// TODO: Error handling

static int proc_create_ps(proc_t* self,
                          const char* args[],
                          const char* env[],
                          int priority,
                          bool detached,
                          bool change_directory)
{
    char* cwd = NULL;

    // Save cwd and change to the new one.
    if (change_directory)
    {
        cwd = _getcwd(NULL, 0);
        if (_chdir(lprt_win32_dirname(args[0])) != 0)
        {
            return ENOENT;
        };
    }

    intptr_t handle = _spawnve(detached ? _P_DETACH : _P_NOWAIT, args[0], args, env);

    // Restore previous dir.
    if (change_directory)
    {
        if (_chdir(cwd) != 0)
        {
            return ENOENT;
        };
        free(cwd);
    }

    if (handle == -1)
    {
        switch (errno)
        {
        case ENOMEM:
            return EAGAIN;
        default: // E2BIG, ENOENT, ENOEXEC:
            return errno;
        }
    }

    self->handle = (void*)handle;

    // Assume these don't fail.
    self->id = GetProcessId(self->handle);
    SetPriorityClass(self->handle, lprt_win32_process_prio_from(priority));
    return 0;
}

static pid_t _getppid(pid_t pid, unsigned* thread_count)
{
    DWORD ppid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32 pe = {0};
    pe.dwSize = sizeof(pe);

    if (!Process32First(snapshot, &pe))
    {
        CloseHandle(snapshot);
        return 0;
    }

    do
    {
        if (pe.th32ProcessID == pid)
        {
            ppid = pe.th32ParentProcessID;
            if (thread_count != NULL)
            {
                *thread_count = pe.cntThreads;
            }
            return ppid;
        }
    } while (Process32Next(snapshot, &pe));
    CloseHandle(snapshot);

    return 0;
}

static inline size_t lprt_filetime_to_seconds(FILETIME* time)
{
    return ((LONGLONG)time->dwLowDateTime + ((LONGLONG)(time->dwHighDateTime) << 32LL)) / 10000000;
}

static inline size_t lprt_boot_time_seconds()
{
    FILETIME curr_time;
    GetSystemTimeAsFileTime(&curr_time);
    return lprt_filetime_to_seconds(&curr_time) - (GetTickCount() / 1000);
}

void proc_stats(proc_t* self, proc_stats_t* stats)
{
    assert(self != NULL);
    assert(stats != NULL);

    FILETIME sys_time;
    FILETIME user_time;
    FILETIME start_time;
    FILETIME exit_time;

    GetProcessTimes(self->handle, &start_time, &exit_time, &sys_time, &user_time);
    stats->sys_time = lprt_filetime_to_seconds(&sys_time);
    stats->user_time = lprt_filetime_to_seconds(&user_time);
    stats->start_time = lprt_filetime_to_seconds(&start_time) - lprt_boot_time_seconds();

    PROCESS_MEMORY_COUNTERS counters;
    GetProcessMemoryInfo(self->handle, &counters, sizeof(counters));
    stats->page_faults = counters.PageFaultCount;
    stats->swap_size = counters.PagefileUsage;
    stats->rss = counters.WorkingSetSize;
    proc_name(self, stats->name);

    _getppid(_getpid(), &stats->threads);

    IO_COUNTERS io_counters;
    GetProcessIoCounters(self->handle, &io_counters);
    stats->bytes_read = io_counters.ReadTransferCount;
    stats->bytes_written = io_counters.WriteTransferCount;

    DWORD handles;
    GetProcessHandleCount(self->handle, &handles);
    stats->handles = handles;

    bool is_alive = WaitForSingleObject(self->handle, 0) == WAIT_TIMEOUT;
    stats->state = is_alive ? PROC_STATE_RUNNING : PROC_STATE_EXITED;

    // TODO: Apparently this is unimplemented on wine and causes an exception.
    //
    // APP_MEMORY_INFORMATION info;
    // GetProcessInformation(self->handle, ProcessAppMemoryInfo, &info, sizeof(info));
    // stats->vmem_size = info.TotalCommitUsage;
    //
    // For now just set this equal to swap_size + rss
    stats->vmem_size = stats->swap_size + stats->rss;
}

static void proc_current_ps(proc_t* self)
{
    proc_from_id(self, _getpid());
}

static void proc_parent_ps(proc_t* self)
{
    proc_from_id(self, _getppid(_getpid(), NULL));
}

#else
extern char** environ;

#include <libgen.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wait.h>

#define CHECK_ERROR_POSIX(call)                                                                    \
    switch (call)                                                                                  \
    {                                                                                              \
    case EPERM:                                                                                    \
    case EACCES:                                                                                   \
        return EPERM;                                                                              \
    case ESRCH:                                                                                    \
        return ESRCH;                                                                              \
    default:                                                                                       \
        break;                                                                                     \
    }

static int lprt_posix_process_prio_from(int value)
{
    return -value;
}

static int lprt_posix_process_prio_value(int prio)
{
    return -prio;
}

int proc_from_id(proc_t* self, unsigned id)
{
    assert(self != NULL);
    assert(id != 0);

    if (kill(id, 0) != 0)
    {
        CHECK_ERROR_POSIX(errno);
    }

    self->id = (pid_t)id;
    return 0;
}

int proc_kill(proc_t* self)
{
    assert(self != NULL);
    if (kill(self->id, SIGKILL) != 0)
    {
        CHECK_ERROR_POSIX(errno);
    }

    return 0;
}

int proc_suspend(proc_t* self)
{
    assert(self != NULL);
    if (kill(self->id, SIGSTOP) != 0)
    {
        CHECK_ERROR_POSIX(errno);
    }

    return 0;
}

int proc_resume(proc_t* self)
{
    assert(self != NULL);
    if (kill(self->id, SIGCONT) != 0)
    {
        CHECK_ERROR_POSIX(errno);
    }

    return 0;
}

int proc_priority(proc_t* self, int* priority)
{
    assert(self != NULL);
    assert(priority != NULL);

    errno = 0;
    int temp = getpriority(PRIO_PROCESS, self->id);
    CHECK_ERROR_POSIX(errno);
    *priority = lprt_posix_process_prio_value(temp);
    return 0;
}

int proc_set_priority(proc_t* self, int priority)
{
    assert(self != NULL);
    assert(priority <= PROC_PRIORITY_MAX);
    assert(priority >= PROC_PRIORITY_MIN);
    errno = 0;
    setpriority(PRIO_PROCESS, self->id, lprt_posix_process_prio_from(priority));
    CHECK_ERROR_POSIX(errno);
    return 0;
}

int proc_session(proc_t* self, unsigned* session)
{
    assert(self != NULL);
    errno = 0;
    pid_t sid = getsid(self->id);
    CHECK_ERROR_POSIX(errno);
    *session = sid;
    return 0;
}

unsigned proc_group(proc_t* self)
{
    assert(self != NULL);
    errno = 0;
    pid_t pgid = getpgid(self->id);
    CHECK_ERROR_POSIX(errno);
    return (unsigned)pgid;
}

int proc_wait_for_ps(proc_t* self, unsigned ms, int* retval)
{
    int options = ms == 0 ? 0 : WNOHANG;
    clock_t clocks = ms == 0 ? LONG_MAX : clock() + CLOCKS_PER_SEC * (ms / 1000.0);

retry:
    while (clock() < clocks)
    {
        switch (waitpid(self->id, retval, options))
        {
        case -1:
            if (errno == EINTR)
            {
                goto retry;
            }
            CHECK_ERROR_POSIX(errno);
        case 0:
            break;
        default:
            return 0;
        }
        // TODO: thread yield maybe?
    }

    return ETIMEDOUT;
}

int proc_name(proc_t* self, char* buffer)
{
    assert(self != NULL);
    assert(buffer != NULL);

    char path[PROC_PATH_MAX];
    snprintf(path, sizeof(path), PROC_NAME_PATH, (unsigned)self->id);

    FILE* f = fopen(path, "r");
    if (f == NULL)
    {
        // TODO: This may simply be not supported on some systems.
        return ESRCH;
    }

    size_t size = fread(buffer, sizeof(char), PROC_NAME_MAX, f);
    fclose(f);

    if (size == 0)
    {
        // TODO: Same as above.
        return ESRCH;
    }

    buffer[size - 1] = 0;
    return 0;
}

#include <dirent.h>
// TODO: Error checking and cleanup.
// https://man7.org/linux/man-pages/man5/proc.5.html

int proc_from_name(proc_t* self, const char* name)
{
    assert(name != NULL);

    for (unsigned id = 0; id <= (unsigned)-1; id++)
    {
        char path[PROC_PATH_MAX];
        snprintf(path, sizeof(path), PROC_NAME_PATH, (unsigned)id);

        FILE* f = fopen(path, "r");
        if (f == NULL)
        {
            continue;
        }

        char temp[PROC_NAME_MAX];
        fscanf(f, "%s", temp);
        fclose(f);

        if (strcmp(temp, name) == 0)
        {
            return proc_from_id(self, id);
        }
    }

    return ESRCH;
}

static int proc_create_ps(proc_t* self,
                          const char* args[],
                          const char* env[],
                          int priority,
                          bool detached,
                          bool change_directory)
{
    struct sched_param p;
    p.sched_priority = priority;

    pid_t id = fork();
    switch (id)
    {
    case 0: { // Child
        sched_setparam(getpid(), &p);

        if (change_directory)
        {
            char buffer[strlen(args[0]) + 1];
            memcpy(buffer, args[0], sizeof(buffer));
            if (chdir(dirname(buffer)) != 0)
            {
                // TODO: error. pipes???
            }
        }

        if (detached)
        {
            setsid();
        }

        execve(args[0], (char**)args, (char**)env);
        _Exit(EXIT_FAILURE);
    }
    case -1: // Error
        return errno == ENOSYS ? ENOTSUP : EAGAIN;
    default:
        break;
    }

    self->id = id;

    return 0;
}

void proc_stats(proc_t* self, proc_stats_t* stats)
{
    assert(self != NULL);
    assert(stats != NULL);

    char path[PROC_PATH_MAX];
    snprintf(path, sizeof(path), PROC_STAT_PATH, (unsigned)self->id);

    char temp[PROC_NAME_MAX + 1]; // +1 for the extra ')'

    size_t major_faults;
    size_t minor_faults;
    char state;

    FILE* f = fopen(path, "r");
    fscanf(f,
           PROC_STAT_FORMAT,
           temp,
           &state,
           &minor_faults,
           &major_faults,
           &stats->user_time,
           &stats->sys_time,
           &stats->threads,
           &stats->start_time,
           &stats->vmem_size,
           &stats->rss,
           &stats->swap_size);
    fclose(f);

    size_t size = strlen(temp);
    memcpy(stats->name, temp, sizeof(stats->name));
    stats->name[size - 1] = 0; // Ignore the ')'

    switch (state)
    {
    case 'S':
    case 'D':
        stats->state = PROC_STATE_SLEEPING;
        break;
    case 'X':
    case 'Z':
        stats->state = PROC_STATE_EXITED;
        break;
    case 'T':
    case 't':
        stats->state = PROC_STATE_SUSPENDED;
        break;
    default:
        stats->state = PROC_STATE_RUNNING;
        break;
    }

    stats->rss *= sysconf(_SC_PAGESIZE);
    stats->swap_size *= sysconf(_SC_PAGESIZE);

    stats->user_time /= sysconf(_SC_CLK_TCK);
    stats->sys_time /= sysconf(_SC_CLK_TCK);
    stats->start_time /= sysconf(_SC_CLK_TCK);

    stats->page_faults = major_faults + minor_faults;

    snprintf(path, sizeof(path), PROC_IO_PATH, (unsigned)self->id);

    f = fopen(path, "r");
    fscanf(f, PROC_IO_FORMAT, &stats->bytes_read, &stats->bytes_written);
    fclose(f);

    snprintf(path, sizeof(path), PROC_FD_PATH, (unsigned)self->id);

    stats->handles = 0;
    DIR* dir = opendir(path);
    while (readdir(dir) != NULL)
    {
        stats->handles++;
    }
    closedir(dir);
}

void proc_destroy(proc_t* self)
{
    assert(self != NULL);
}

static void proc_current_ps(proc_t* self)
{
    proc_from_id(self, getpid());
}

static void proc_parent_ps(proc_t* self)
{
    proc_from_id(self, getppid());
}
#endif // _WIN32

//
//
// End platform-specific
//
//

int proc_wait(proc_t* self, int* retval)
{
    assert(self != NULL);
    assert(retval != NULL);
    return proc_wait_for_ps(self, 0, retval);
}

int proc_wait_for(proc_t* self, unsigned milliseconds, int* retval)
{
    assert(self != NULL);
    assert(retval != NULL);
    assert(milliseconds != 0);
    return proc_wait_for_ps(self, milliseconds, retval);
}

void proc_exit(int status)
{
    exit(status);
}

int proc_with(proc_t* self,
              const char* args[],
              const char* env[],
              int priority,
              bool detached,
              bool change_directory)
{
    assert(self != NULL);
    assert(args != NULL);
    assert(env != NULL);
    assert(priority >= PROC_PRIORITY_MIN);
    assert(priority <= PROC_PRIORITY_MAX);

    return proc_create_ps(self, args, env, priority, detached, change_directory);
}

int proc_create(proc_t* self, const char* args[])
{
    assert(self != NULL);
    assert(args != NULL);

    return proc_with(self, args, (const char**)environ, PROC_PRIORITY_DEFAULT, false, false);
}

unsigned proc_id(proc_t* self)
{
    assert(self != NULL);
    return (unsigned)self->id;
}

proc_t* proc_current()
{
    static bool init = false;
    static proc_t current;

    if (!init)
    {
        init = true;
        proc_current_ps(&current);
    }

    return &current;
}

proc_t* proc_parent()
{
    static bool init = false;
    static proc_t parent;

    if (!init)
    {
        init = true;
        proc_parent_ps(&parent);
    }

    return &parent;
}
