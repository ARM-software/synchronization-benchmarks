/*
 * SPDX-FileCopyrightText: Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <pthread.h>
#include <errno.h>

#ifdef initialize_lock
#undef initialize_lock
#endif
#define initialize_lock(p_lock, p_pinorder, num_threads) pthread_mutex_lock_init(p_lock)

#ifdef parse_test_args
#undef parse_test_args
#endif
#define parse_test_args(args, argc, argv) pthread_mutex_parse_args(args, argc, argv)

// pthread_mutex_lock test


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

char * test_type_name = NULL;

void pthread_mutex_parse_args(test_args_t * t, int argc, char ** argv) {
    int i;

    while ((i = getopt(argc, argv, ":t:h")) != -1) {
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
                    fprintf(stderr, "pthread_mutex_lock: unknown mutex type %s\n", optarg);
                    exit(-1);
                }
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
                        "-t mutex_kind  one of: ");
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

    if (-1 == asprintf(&test_type_name, "{mutex_type=%s}",
                mutex_type_name)) {
        fprintf(stderr, "asprintf failed\n");
        exit(-1);
    }

    push_dynamic_lock_memory(test_type_name);
}


void pthread_mutex_lock_init(void * lock) {
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
    int ret = pthread_mutex_lock(mutex);
    if (ret) {
        // shouldn't ever happen in this benchmark
        fprintf(stderr, "pthread_mutex_lock ret = %d\n", ret);
        exit(-1);
    }
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
