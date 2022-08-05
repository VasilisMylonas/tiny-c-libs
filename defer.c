#include "defer.h"

#include "config.h"

#ifdef DEFER_HAVE_LIBUNWIND
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef struct
{
    struct
    {
        void (*cleanup)(void*);
        void* arg;
    } entries[DEFER_MAX];
    jmp_buf context;
    void* link;
    int count;
    bool can_recover;
    bool ok;
} defer_frame_t;

typedef enum
{
    DEFER_FRAME_ALLOC_FAILURE,
    DEFER_FRAME_OVERFLOW,
    DEFER_BACKTRACE_FAILURE,
    DEFER_BACKTRACE_NOTSUPP,
    DEFER_TSS_FAILURE,
    DEFER_ATEXIT_FAILURE,
} defer_msg_t;

static void __defer_msg(defer_msg_t message);
static const char* __defer_error_name(int error);
static const char* __defer_error_desc(int error);
static void __defer_frame_register(defer_frame_t* self);
static void __defer_frame_deregister(defer_frame_t* self);
static void __defer_unwind(int error, bool recoverable);
static void __defer_one_time_init();
static void __defer_thrd_fini();

static once_flag __defer_once_flag = ONCE_FLAG_INIT;
static thread_local defer_frame_t* __defer_frame_current = NULL;

void* defer(void (*cleanup)(void*), void* arg)
{
    assert(cleanup != NULL);

    if (__defer_frame_current->count == DEFER_MAX)
    {
        __defer_msg(DEFER_FRAME_OVERFLOW);
        abort();
    }

    // Register the function and argument.
    __defer_frame_current->entries[__defer_frame_current->count].cleanup = cleanup;
    __defer_frame_current->entries[__defer_frame_current->count].arg = arg;
    __defer_frame_current->count++;

    return arg;
}

void panic(int error)
{
    assert(error != 0);

    __defer_unwind(error, true);

    fprintf(
        stderr, "Panic with error %s: %s\n", __defer_error_name(error), __defer_error_desc(error));

#ifdef DEFER_HAVE_LIBUNWIND
    unw_cursor_t cursor;
    unw_context_t context;

    if (unw_getcontext(&context) != 0 || unw_init_local(&cursor, &context) != 0)
    {
        __defer_msg(DEFER_BACKTRACE_FAILURE);
    }

    char name[DEFER_SYMBOL_NAME_MAX];

    while (unw_step(&cursor) > 0)
    {
        fprintf(stderr,
                "\tat %s()\n",
                unw_get_proc_name(&cursor, name, sizeof(name), NULL) != 0 ? "???" : name);
    }
#else
    __defer_msg(DEFER_BACKTRACE_NOTSUPP);
#warning "libunwind missing, backtraces will not be available"
#endif

    thrd_exit(error);
}

// Handle error reporting.
void __defer_msg(defer_msg_t kind)
{
    const char* message = NULL;

    switch (kind)
    {
    case DEFER_FRAME_ALLOC_FAILURE:
        message = "Could not allocate defer frame\n";
        break;
    case DEFER_FRAME_OVERFLOW:
        message = "Tried to defer more than DEFER_MAX handlers\n";
        break;
    case DEFER_BACKTRACE_FAILURE:
        message = "Backtrace generation failed\n";
        break;
    case DEFER_BACKTRACE_NOTSUPP:
        message = "Backtrace not supported\n";
        break;
    case DEFER_ATEXIT_FAILURE:
        message = "Could not register atexit() handler\n";
        break;
    case DEFER_TSS_FAILURE:
        message = "TSS registration failed\n";
        break;
    default:
        break;
    }

    fputs(message, stderr);
}

// This runs on function entry.
void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void* func, void* call_site)
{
    (void)func;
    (void)call_site;

    defer_frame_t* frame = calloc(1, sizeof(defer_frame_t));
    if (frame == NULL)
    {
        __defer_msg(DEFER_FRAME_ALLOC_FAILURE);
        abort();
    }
    __defer_frame_register(frame);
}

// This runs on function exit.
void __attribute__((no_instrument_function)) __cyg_profile_func_exit(void* func, void* call_site)
{
    (void)func;
    (void)call_site;

    defer_frame_t* frame = __defer_frame_current;
    __defer_frame_deregister(frame);
    free(frame);
}

void __defer_one_time_init()
{
    if (atexit(__defer_thrd_fini) != 0)
    {
        __defer_msg(DEFER_ATEXIT_FAILURE);
        abort();
    }
}

void defer_thrd_init()
{
    call_once(&__defer_once_flag, __defer_one_time_init);

    // Register __defer_thrd_fini to run on thread exit.
    tss_t key;
    if (tss_create(&key, (void (*)(void*))__defer_thrd_fini) != thrd_success)
    {
        __defer_msg(DEFER_TSS_FAILURE);
        abort();
    }
    tss_delete(key);
}

void __defer_thrd_fini()
{
    // This is a non-recoverable unwind.
    __defer_unwind(0, false);
}

jmp_buf* __defer_frame_context()
{
    __defer_frame_current->can_recover = true;
    return &__defer_frame_current->context;
}

void __defer_frame_register(defer_frame_t* self)
{
    assert(self != NULL);

    // Insert the frame to the list.
    self->link = __defer_frame_current;
    __defer_frame_current = self;
}

void __defer_frame_deregister(defer_frame_t* self)
{
    assert(self != NULL);

    // Execute each deferred function.
    const size_t count = self->count;
    for (size_t i = 0; i < count; i++)
    {
        self->count--;
        self->entries[self->count].cleanup(self->entries[self->count].arg);
    }

    self->ok = true;

    // Remove the frame from the list.
    __defer_frame_current = self->link;
}

void __defer_unwind(int error, bool recoverable)
{
    // Deregister all previous frames.
    while (__defer_frame_current != NULL)
    {
        // Check if this frame contains a recovery point.
        if (__defer_frame_current->can_recover && recoverable)
        {
            longjmp(__defer_frame_current->context, error);
        }
        __defer_frame_deregister(__defer_frame_current);
    }
}

const char* __defer_error_desc(int error)
{
    static thread_local char buffer[DEFER_STRERROR_MAX];
    strerror_r(error, buffer, sizeof(buffer));
    return buffer;
}

#define ERROR_CASE(e)                                                                              \
    case e:                                                                                        \
        return #e;

const char* __defer_error_name(int error)
{
    switch (error)
    {
#ifdef EPERM
        ERROR_CASE(EPERM);
#endif
#ifdef ENOENT
        ERROR_CASE(ENOENT);
#endif
#ifdef ESRCH
        ERROR_CASE(ESRCH);
#endif
#ifdef EINTR
        ERROR_CASE(EINTR);
#endif
#ifdef EIO
        ERROR_CASE(EIO);
#endif
#ifdef ENXIO
        ERROR_CASE(ENXIO);
#endif
#ifdef E2BIG
        ERROR_CASE(E2BIG);
#endif
#ifdef ENOEXEC
        ERROR_CASE(ENOEXEC);
#endif
#ifdef EBADF
        ERROR_CASE(EBADF);
#endif
#ifdef ECHILD
        ERROR_CASE(ECHILD);
#endif
#ifdef EAGAIN
        ERROR_CASE(EAGAIN);
#endif
#ifdef ENOMEM
        ERROR_CASE(ENOMEM);
#endif
#ifdef EACCES
        ERROR_CASE(EACCES);
#endif
#ifdef EFAULT
        ERROR_CASE(EFAULT);
#endif
#ifdef ENOTBLK
        ERROR_CASE(ENOTBLK);
#endif
#ifdef EBUSY
        ERROR_CASE(EBUSY);
#endif
#ifdef EEXIST
        ERROR_CASE(EEXIST);
#endif
#ifdef EXDEV
        ERROR_CASE(EXDEV);
#endif
#ifdef ENODEV
        ERROR_CASE(ENODEV);
#endif
#ifdef ENOTDIR
        ERROR_CASE(ENOTDIR);
#endif
#ifdef EISDIR
        ERROR_CASE(EISDIR);
#endif
#ifdef EINVAL
        ERROR_CASE(EINVAL);
#endif
#ifdef ENFILE
        ERROR_CASE(ENFILE);
#endif
#ifdef EMFILE
        ERROR_CASE(EMFILE);
#endif
#ifdef ENOTTY
        ERROR_CASE(ENOTTY);
#endif
#ifdef ETXTBSY
        ERROR_CASE(ETXTBSY);
#endif
#ifdef EFBIG
        ERROR_CASE(EFBIG);
#endif
#ifdef ENOSPC
        ERROR_CASE(ENOSPC);
#endif
#ifdef ESPIPE
        ERROR_CASE(ESPIPE);
#endif
#ifdef EROFS
        ERROR_CASE(EROFS);
#endif
#ifdef EMLINK
        ERROR_CASE(EMLINK);
#endif
#ifdef EPIPE
        ERROR_CASE(EPIPE);
#endif
#ifdef EDOM
        ERROR_CASE(EDOM);
#endif
#ifdef ERANGE
        ERROR_CASE(ERANGE);
#endif
#ifdef EDEADLK
        ERROR_CASE(EDEADLK);
#endif
#ifdef ENAMETOOLONG
        ERROR_CASE(ENAMETOOLONG);
#endif
#ifdef ENOLCK
        ERROR_CASE(ENOLCK);
#endif
#ifdef ENOSYS
        ERROR_CASE(ENOSYS);
#endif
#ifdef ENOTEMPTY
        ERROR_CASE(ENOTEMPTY);
#endif
#ifdef ELOOP
        ERROR_CASE(ELOOP);
#endif
#ifdef ENOMSG
        ERROR_CASE(ENOMSG);
#endif
#ifdef EIDRM
        ERROR_CASE(EIDRM);
#endif
#ifdef ECHRNG
        ERROR_CASE(ECHRNG);
#endif
#ifdef EL2NSYNC
        ERROR_CASE(EL2NSYNC);
#endif
#ifdef EL3HLT
        ERROR_CASE(EL3HLT);
#endif
#ifdef EL3RST
        ERROR_CASE(EL3RST);
#endif
#ifdef ELNRNG
        ERROR_CASE(ELNRNG);
#endif
#ifdef EUNATCH
        ERROR_CASE(EUNATCH);
#endif
#ifdef ENOCSI
        ERROR_CASE(ENOCSI);
#endif
#ifdef EL2HLT
        ERROR_CASE(EL2HLT);
#endif
#ifdef EBADE
        ERROR_CASE(EBADE);
#endif
#ifdef EBADR
        ERROR_CASE(EBADR);
#endif
#ifdef EXFULL
        ERROR_CASE(EXFULL);
#endif
#ifdef ENOANO
        ERROR_CASE(ENOANO);
#endif
#ifdef EBADRQC
        ERROR_CASE(EBADRQC);
#endif
#ifdef EBADSLT
        ERROR_CASE(EBADSLT);
#endif
#ifdef EBFONT
        ERROR_CASE(EBFONT);
#endif
#ifdef ENOSTR
        ERROR_CASE(ENOSTR);
#endif
#ifdef ENODATA
        ERROR_CASE(ENODATA);
#endif
#ifdef ETIME
        ERROR_CASE(ETIME);
#endif
#ifdef ENOSR
        ERROR_CASE(ENOSR);
#endif
#ifdef ENONET
        ERROR_CASE(ENONET);
#endif
#ifdef ENOPKG
        ERROR_CASE(ENOPKG);
#endif
#ifdef EREMOTE
        ERROR_CASE(EREMOTE);
#endif
#ifdef ENOLINK
        ERROR_CASE(ENOLINK);
#endif
#ifdef EADV
        ERROR_CASE(EADV);
#endif
#ifdef ESRMNT
        ERROR_CASE(ESRMNT);
#endif
#ifdef ECOMM
        ERROR_CASE(ECOMM);
#endif
#ifdef EPROTO
        ERROR_CASE(EPROTO);
#endif
#ifdef EMULTIHOP
        ERROR_CASE(EMULTIHOP);
#endif
#ifdef EDOTDOT
        ERROR_CASE(EDOTDOT);
#endif
#ifdef EBADMSG
        ERROR_CASE(EBADMSG);
#endif
#ifdef EOVERFLOW
        ERROR_CASE(EOVERFLOW);
#endif
#ifdef ENOTUNIQ
        ERROR_CASE(ENOTUNIQ);
#endif
#ifdef EBADFD
        ERROR_CASE(EBADFD);
#endif
#ifdef EREMCHG
        ERROR_CASE(EREMCHG);
#endif
#ifdef ELIBACC
        ERROR_CASE(ELIBACC);
#endif
#ifdef ELIBBAD
        ERROR_CASE(ELIBBAD);
#endif
#ifdef ELIBSCN
        ERROR_CASE(ELIBSCN);
#endif
#ifdef ELIBMAX
        ERROR_CASE(ELIBMAX);
#endif
#ifdef ELIBEXEC
        ERROR_CASE(ELIBEXEC);
#endif
#ifdef EILSEQ
        ERROR_CASE(EILSEQ);
#endif
#ifdef ERESTART
        ERROR_CASE(ERESTART);
#endif
#ifdef ESTRPIPE
        ERROR_CASE(ESTRPIPE);
#endif
#ifdef EUSERS
        ERROR_CASE(EUSERS);
#endif
#ifdef ENOTSOCK
        ERROR_CASE(ENOTSOCK);
#endif
#ifdef EDESTADDRREQ
        ERROR_CASE(EDESTADDRREQ);
#endif
#ifdef EMSGSIZE
        ERROR_CASE(EMSGSIZE);
#endif
#ifdef EPROTOTYPE
        ERROR_CASE(EPROTOTYPE);
#endif
#ifdef ENOPROTOOPT
        ERROR_CASE(ENOPROTOOPT);
#endif
#ifdef EPROTONOSUPPORT
        ERROR_CASE(EPROTONOSUPPORT);
#endif
#ifdef ESOCKTNOSUPPORT
        ERROR_CASE(ESOCKTNOSUPPORT);
#endif
#ifdef EOPNOTSUPP
        ERROR_CASE(EOPNOTSUPP);
#endif
#ifdef EPFNOSUPPORT
        ERROR_CASE(EPFNOSUPPORT);
#endif
#ifdef EAFNOSUPPORT
        ERROR_CASE(EAFNOSUPPORT);
#endif
#ifdef EADDRINUSE
        ERROR_CASE(EADDRINUSE);
#endif
#ifdef EADDRNOTAVAIL
        ERROR_CASE(EADDRNOTAVAIL);
#endif
#ifdef ENETDOWN
        ERROR_CASE(ENETDOWN);
#endif
#ifdef ENETUNREACH
        ERROR_CASE(ENETUNREACH);
#endif
#ifdef ENETRESET
        ERROR_CASE(ENETRESET);
#endif
#ifdef ECONNABORTED
        ERROR_CASE(ECONNABORTED);
#endif
#ifdef ECONNRESET
        ERROR_CASE(ECONNRESET);
#endif
#ifdef ENOBUFS
        ERROR_CASE(ENOBUFS);
#endif
#ifdef EISCONN
        ERROR_CASE(EISCONN);
#endif
#ifdef ENOTCONN
        ERROR_CASE(ENOTCONN);
#endif
#ifdef ESHUTDOWN
        ERROR_CASE(ESHUTDOWN);
#endif
#ifdef ETOOMANYREFS
        ERROR_CASE(ETOOMANYREFS);
#endif
#ifdef ETIMEDOUT
        ERROR_CASE(ETIMEDOUT);
#endif
#ifdef ECONNREFUSED
        ERROR_CASE(ECONNREFUSED);
#endif
#ifdef EHOSTDOWN
        ERROR_CASE(EHOSTDOWN);
#endif
#ifdef EHOSTUNREACH
        ERROR_CASE(EHOSTUNREACH);
#endif
#ifdef EALREADY
        ERROR_CASE(EALREADY);
#endif
#ifdef EINPROGRESS
        ERROR_CASE(EINPROGRESS);
#endif
#ifdef ESTALE
        ERROR_CASE(ESTALE);
#endif
#ifdef EUCLEAN
        ERROR_CASE(EUCLEAN);
#endif
#ifdef ENOTNAM
        ERROR_CASE(ENOTNAM);
#endif
#ifdef ENAVAIL
        ERROR_CASE(ENAVAIL);
#endif
#ifdef EISNAM
        ERROR_CASE(EISNAM);
#endif
#ifdef EREMOTEIO
        ERROR_CASE(EREMOTEIO);
#endif
#ifdef EDQUOT
        ERROR_CASE(EDQUOT);
#endif
#ifdef ENOMEDIUM
        ERROR_CASE(ENOMEDIUM);
#endif
#ifdef EMEDIUMTYPE
        ERROR_CASE(EMEDIUMTYPE);
#endif
#ifdef ECANCELED
        ERROR_CASE(ECANCELED);
#endif
#ifdef ENOKEY
        ERROR_CASE(ENOKEY);
#endif
#ifdef EKEYEXPIRED
        ERROR_CASE(EKEYEXPIRED);
#endif
#ifdef EKEYREVOKED
        ERROR_CASE(EKEYREVOKED);
#endif
#ifdef EKEYREJECTED
        ERROR_CASE(EKEYREJECTED);
#endif
#ifdef EOWNERDEAD
        ERROR_CASE(EOWNERDEAD);
#endif
#ifdef ENOTRECOVERABLE
        ERROR_CASE(ENOTRECOVERABLE);
#endif
#ifdef ERFKILL
        ERROR_CASE(ERFKILL);
#endif
#ifdef EHWPOISON
        ERROR_CASE(EHWPOISON);
#endif

    default:
        return "???";
    }
}

#undef ERROR_CASE
