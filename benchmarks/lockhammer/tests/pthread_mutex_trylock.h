/*
 * SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <pthread.h>
#include <errno.h>
#include <time.h>

#ifdef initialize_lock
#undef initialize_lock
#endif
#define initialize_lock(p_lock, p_pinorder, num_threads) pthread_mutex_trylock_init(p_lock)

#ifdef parse_test_args
#undef parse_test_args
#endif
#define parse_test_args(args, argc, argv) pthread_mutex_parse_args(args, argc, argv)

// pthread_mutex_trylock test


const struct {
    char * type_name;
    int kind;
} pthread_mutex_types[] = {
#ifdef __USE_GNU
    { .kind = PTHREAD_MUTEX_FAST_NP,     .type_name = "fast" },
#endif
    { .kind = PTHREAD_MUTEX_RECURSIVE,   .type_name = "recursive" },
    { .kind = PTHREAD_MUTEX_ERRORCHECK,  .type_name = "errorcheck" },
    { .kind = PTHREAD_MUTEX_NORMAL,      .type_name = "normal" },
    { .kind = PTHREAD_MUTEX_DEFAULT,     .type_name = "default" },
    { .kind = PTHREAD_MUTEX_ADAPTIVE_NP, .type_name = "adaptive" },
    { .kind = -1,                        .type_name = NULL }
};

int mutex_kind = PTHREAD_MUTEX_DEFAULT;
char * mutex_type_name = "default";

struct timespec retry_delay_timespec = { .tv_sec = 0, .tv_nsec = 0 };
unsigned long blackhole_tokens = 0;

char * test_type_name = NULL;

void pthread_mutex_parse_args(test_args_t * t, int argc, char ** argv) {
    int i;

    while ((i = getopt(argc, argv, ":t:d:i:h")) != -1) {
        switch (i) {
            case 't':   // kind of mutex
                mutex_type_name = NULL;   // use as a flag to signal found

                for (size_t j = 0; pthread_mutex_types[j].type_name; j++) {
                    if (0 == strcmp(optarg, pthread_mutex_types[j].type_name)) {
                        mutex_kind = pthread_mutex_types[j].kind;
                        mutex_type_name = pthread_mutex_types[j].type_name;
                        break;
                    }
                }

                if (! mutex_type_name) {
                    fprintf(stderr, "pthread_mutex_trylock: unknown mutex type %s\n", optarg);
                    exit(-1);
                }
                break;

            case 'd':
                {
                    const unsigned long ns_per_s = 1000000000;
                    const unsigned long nsec = strtoul(optarg, NULL, 0);
                    retry_delay_timespec.tv_sec = nsec / ns_per_s;
                    retry_delay_timespec.tv_nsec = nsec % ns_per_s;
                }
                break;

            case 'i':
                blackhole_tokens = strtoul(optarg, NULL, 0);
                break;

            case '?':
            case ':':
                if (i == '?')
                    printf("option flag %s is unknown\n\n", argv[optind-1]);
                else if (i == ':')
                    printf("option flag %s is missing an argument\n\n", argv[optind-1]);
                // fall-through
            case 'h':
                fprintf(stderr,
                        "pthread_mutex additional options after --:\n"
                        "\n"
                        "-d retry_delay_nsec\n"
                        "-i delay tokens using blackhole; overrides retry_delay_nsec\n"
                        "-t mutex_kind, one of: ");
                for (size_t j = 0; pthread_mutex_types[j].type_name; j++) {
                    fprintf(stderr, "%s%s", j ? ", " : "", pthread_mutex_types[j].type_name);
                }
                fprintf(stderr, "\n");
                exit(2);
            default:
                fprintf(stderr, "should never have gotten here\n");
                exit(-1);
                break;
        }
    }

    if (-1 == asprintf(&test_type_name, "{mutex_type=%s, retry_delay_ns=%lu, blackhole_tokens=%lu}",
                mutex_type_name,
                1000000000 * retry_delay_timespec.tv_sec + retry_delay_timespec.tv_nsec,
                blackhole_tokens)) {
        fprintf(stderr, "asprintf failed\n");
        exit(-1);
    }

    push_dynamic_lock_memory(test_type_name);
}


void pthread_mutex_trylock_init(void * lock) {
    pthread_mutex_t * mutex = (pthread_mutex_t *) lock;

    pthread_mutex_destroy(mutex);

    pthread_mutexattr_t mutexattr;

    pthread_mutexattr_init(&mutexattr);

    int r = pthread_mutexattr_settype(&mutexattr, mutex_kind);
    if (r) { perror("pthread_mutexattr_settype"); exit(EXIT_FAILURE); }

    (void) pthread_mutex_init(mutex, &mutexattr);
}


static inline unsigned long lock_acquire (void * lock, unsigned long threadnum) {
    pthread_mutex_t * mutex = (pthread_mutex_t *) lock;
    do {
        int ret = pthread_mutex_trylock(mutex);
        if (! ret)
            break;
        if (ret == EBUSY) {
//#define SHOW_RETRY_DELAY
#ifdef SHOW_RETRY_DELAY
            unsigned long tsc_start = timer_get_counter();
#endif
            if (blackhole_tokens) {
                blackhole(blackhole_tokens);
            } else {
                nanosleep(&retry_delay_timespec, NULL);
            }
#ifdef SHOW_RETRY_DELAY
            unsigned long tsc_diff = timer_get_counter() - tsc_start;
            printf("%lu: tsc_diff = %lu, ns = %lu\n",
                    threadnum, tsc_diff,
                    (unsigned long) (tsc_diff * 1e9 / timer_get_timer_freq()));
#endif
        } else if (ret) {
            fprintf(stderr, "pthread_mutex_trylock ret = %d\n", ret);
            exit(-1);
        }
    } while(1);
    return 0;
}


static inline void lock_release (void * lock, unsigned long threadnum) {
    pthread_mutex_t * mutex = (pthread_mutex_t *) lock;
    int ret = pthread_mutex_unlock(mutex);
    if (ret) {
        // shouldn't ever happen in this benchmark
        printf("pthread_mutex_unlock ret = %d\n", ret);
    }
    return;
}

/* vim: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
