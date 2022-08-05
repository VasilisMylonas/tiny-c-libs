#define BENCHMARK_RUNS 1000

#include "benchmark.h"
#include "defer.h"
#include "except.h"
#include "vec.h"

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

void vec_create_destroy_test()
{
    int* vec = NULL;
    vec_create(&vec, 0);

    assert(vec_size(&vec) == 0);
    assert(vec_cap(&vec) == VEC_DEFAULT_CAP);

    vec_destroy(&vec);

    assert(vec == NULL);
}

void vec_fill_reverse_test()
{
    const int value = 5;

    int* vec = NULL;
    vec_create(&vec, 0);

    _VEC_HEADER(&vec)->size = _VEC_HEADER(&vec)->cap;

    vec_fill(&vec, &value);

    for (size_t i = 0; i < vec_size(&vec); i++)
    {
        assert(vec[i] == value);
    }

    vec[0] = value - 1;
    vec[vec_size(&vec) - 1] = value + 1;

    vec_reverse(&vec);

    assert(vec[0] == value + 1);
    assert(vec[vec_size(&vec) - 1] == value - 1);

    vec_destroy(&vec);
}

void vec_push_pop_test()
{
    int* vec = NULL;
    vec_create(&vec, 0);

    for (size_t i = 0; i < 10; i++)
    {
        vec_push(&vec, &i);
    }

    assert(vec_size(&vec) == 10);

    for (size_t i = 0; i < 10; i++)
    {
        assert(vec[i] == i);
    }

    for (size_t i = 10; i > 0; i--)
    {
        assert(vec_pop(&vec) == i - 1);
    }

    assert(vec_size(&vec) == 0);

    vec_destroy(&vec);
}

void test_throw()
{
    bool exec_try = false;
    bool exec_catch = false;
    bool exec_finally = false;

    try
    {
        exec_try = true;
        throw(int, EINVAL);
        assert(false);
    }
    catch (int, e)
    {
        exec_catch = true;
    }
    finally
    {
        exec_finally = true;
    }

    assert(exec_try);
    assert(exec_catch);
    assert(exec_finally);
}

void test_no_throw()
{
    bool exec_try = false;
    bool exec_catch = false;
    bool exec_finally = false;

    try
    {
        exec_try = true;
    }
    catch (int, e)
    {
        exec_catch = true;
    }
    finally
    {
        exec_finally = true;
    }

    assert(exec_try);
    assert(!exec_catch);
    assert(exec_finally);
}

void test_signal()
{
    except_enable_sigcatch();

    bool error_caught = false;
    try
    {
        raise(SIGFPE);
    }
    catch (arithmetic_error_t, e)
    {
        error_caught = true;
    }

    assert(error_caught);

    except_disable_sigcatch();
}

void with_defer()
{
    FILE* f = fopen("temp.txt", "w");
    defer(fclose, f);
    fprintf(f, "test");
}

void without_defer()
{
    FILE* f = fopen("temp.txt", "w");
    fprintf(f, "test");
    fclose(f);
}

int main()
{
    defer_thrd_init();

    vec_create_destroy_test();
    vec_fill_reverse_test();
    vec_push_pop_test();

    test_throw();
    test_no_throw();
    test_signal();

    benchmark(with_defer, "with_defer");
    benchmark(without_defer, "without_defer");
}
