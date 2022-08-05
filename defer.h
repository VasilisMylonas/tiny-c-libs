#ifndef DEFER_H
#define DEFER_H

#include <errno.h>  // For E* constants.
#include <setjmp.h> // For setjmp

// The maximum number of defer calls for a function scope.
#define DEFER_MAX 16

#define DEFER_STRERROR_MAX    128
#define DEFER_SYMBOL_NAME_MAX 128
// #define DEFER_HAVE_LIBUNWIND

/**
 * @brief Register a handler to be run at function scope exit with the given argument.
 *
 * The registered handler must not be NULL.
 *
 * @param handler The handler to register.
 * @param arg The argument to pass to the handler.
 * @return The provided argument.
 */
extern void* defer(void (*handler)(void*), void* arg);

/**
 * @brief Signals an serious error condition.
 *
 * The error condition must not be 0.
 *
 * All previously registered defer handlers are run in reverse order before exiting the current
 * thread. Upon exit, information about the error, including its origin may be printed to stderr.
 * Most E* constants found in errno.h are recognized with proper name and description information.
 *
 * Execution may instead be recovered by a call to recover() as if by calling longjmp() with the
 * provided value.
 *
 * @param error The error that occurred.
 */
extern void panic(int error);

/**
 * @brief Recover execution from a panic.
 *
 * Only the latest recover() call will serve as a recovery point in a given function scope.
 *
 * @return 0 if no panic is occurring, otherwise the value passed to panic().
 */
#define recover() setjmp(*__libdefer_frame_context())

/**
 * @brief Performs per-thread initialization for libdefer.
 *
 * This MUST be called at the start of main() as well as the start of any thread entry point and
 * ONLY once per thread. Calling defer(), panic() or recover() without a prior call to
 * defer_thrd_init is undefined behavior.
 */
extern void defer_thrd_init();

// Implementation detail.
extern jmp_buf* __defer_frame_context();

#endif // DEFER_H
