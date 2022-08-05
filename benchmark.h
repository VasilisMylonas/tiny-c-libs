#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdio.h>
#include <time.h>

#ifndef BENCHMARK_RUNS
#define BENCHMARK_RUNS 100
#endif

static void benchmark(void (*callback)(), const char* name)
{
    double min_time = 0;
    double max_time = 0;
    double avg_time = 0;

    for (size_t i = 0; i < BENCHMARK_RUNS; i++)
    {
        clock_t start = clock();
        callback();
        clock_t end = clock();

        double current_time = (double)(end - start) / CLOCKS_PER_SEC;

        if (min_time > current_time)
            min_time = current_time;

        if (max_time < current_time)
            max_time = current_time;

        avg_time += current_time;
    }

    avg_time /= BENCHMARK_RUNS;

    fprintf(stderr,
            "%s (%u runs):\n"
            "min: %lfs\n"
            "avg: %lfs\n"
            "max: %lfs\n",
            name,
            BENCHMARK_RUNS,
            min_time,
            avg_time,
            max_time);
}

#endif // BENCHMARK_H
