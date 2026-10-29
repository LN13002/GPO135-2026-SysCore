#include <pthread.h>

#include <stdio.h>
#include <string.h>

enum {
    THREAD_COUNT = 5,
    INCREMENTS_PER_THREAD = 100000
};

int shared_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int id;
    int failed;
} CounterWorkerArgs;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t workers_ready;
    pthread_cond_t start_signal;
    int ready_count;
    int start;
} SyncContext;

typedef struct {
    int id;
    int failed;
    SyncContext *context;
} SyncWorkerArgs;

static int report_pthread_error(const char *operation, int error_code) {
    if (error_code == 0) {
        return 0;
    }
    fprintf(stderr, "%s: %s (código %d)\n", operation, strerror(error_code), error_code);
    return 1;
}

static void *increment_counter(void *raw_args) {
    CounterWorkerArgs *args = raw_args;

    for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
        if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&counter_mutex))) {
            args->failed = 1;
            return NULL;
        }

        ++shared_counter;

        if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&counter_mutex))) {
            args->failed = 1;
            return NULL;
        }
    }

    printf("Hilo %d terminó sus incrementos.\n", args->id);
    return NULL;
}

static int run_counter_demo(void) {
    pthread_t threads[THREAD_COUNT];
    CounterWorkerArgs args[THREAD_COUNT];
    int created = 0;
    int failed = 0;
    int all_joined = 1;

    shared_counter = 0;

    for (int i = 0; i < THREAD_COUNT; ++i) {
        args[i].id = i + 1;
        args[i].failed = 0;

        const int rc = pthread_create(&threads[i], NULL, increment_counter, &args[i]);
        if (report_pthread_error("pthread_create", rc)) {
            failed = 1;
            break;
        }
        ++created;
    }

    for (int i = 0; i < created; ++i) {
        const int rc = pthread_join(threads[i], NULL);
        if (report_pthread_error("pthread_join", rc)) {
            failed = 1;
            all_joined = 0;
        } else if (args[i].failed) {
            failed = 1;
        }
    }

    if (created == THREAD_COUNT && all_joined) {
        const int expected = THREAD_COUNT * INCREMENTS_PER_THREAD;
        printf("Contador final: %d (esperado: %d)\n", shared_counter, expected);
        if (shared_counter != expected) {
            fprintf(stderr, "El contador no coincide con el valor esperado.\n");
            failed = 1;
        }
    } else {
        failed = 1;
    }

    if (all_joined && report_pthread_error("pthread_mutex_destroy",
                                           pthread_mutex_destroy(&counter_mutex))) {
        failed = 1;
    }

    return failed ? 1 : 0;
}

static int initialize_sync_context(SyncContext *context) {
    context->ready_count = 0;
    context->start = 0;

    if (report_pthread_error("pthread_mutex_init", pthread_mutex_init(&context->mutex, NULL))) {
        return 1;
    }
    if (report_pthread_error("pthread_cond_init (workers_ready)",
                             pthread_cond_init(&context->workers_ready, NULL))) {
        report_pthread_error("pthread_mutex_destroy", pthread_mutex_destroy(&context->mutex));
        return 1;
    }
    if (report_pthread_error("pthread_cond_init (start_signal)",
                             pthread_cond_init(&context->start_signal, NULL))) {
        report_pthread_error("pthread_cond_destroy (workers_ready)",
                             pthread_cond_destroy(&context->workers_ready));
        report_pthread_error("pthread_mutex_destroy", pthread_mutex_destroy(&context->mutex));
        return 1;
    }

    return 0;
}

static int destroy_sync_context(SyncContext *context) {
    int failed = 0;

    if (report_pthread_error("pthread_cond_destroy (workers_ready)",
                             pthread_cond_destroy(&context->workers_ready))) {
        failed = 1;
    }
    if (report_pthread_error("pthread_cond_destroy (start_signal)",
                             pthread_cond_destroy(&context->start_signal))) {
        failed = 1;
    }
    if (report_pthread_error("pthread_mutex_destroy", pthread_mutex_destroy(&context->mutex))) {
        failed = 1;
    }

    return failed;
}

static void *wait_for_start(void *raw_args) {
    SyncWorkerArgs *args = raw_args;
    SyncContext *context = args->context;

    if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&context->mutex))) {
        args->failed = 1;
        return NULL;
    }

    ++context->ready_count;
    if (report_pthread_error("pthread_cond_signal (workers_ready)",
                             pthread_cond_signal(&context->workers_ready))) {
        args->failed = 1;
    }

    while (!context->start) {
        const int rc = pthread_cond_wait(&context->start_signal, &context->mutex);
        if (report_pthread_error("pthread_cond_wait (start_signal)", rc)) {
            args->failed = 1;
            report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context->mutex));
            return NULL;
        }
    }

    printf("Hilo %d recibió la señal de inicio.\n", args->id);
    if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context->mutex))) {
        args->failed = 1;
    }

    return NULL;
}

static int run_condition_demo(void) {
    SyncContext context;
    if (initialize_sync_context(&context)) {
        return 1;
    }

    pthread_t threads[THREAD_COUNT];
    SyncWorkerArgs args[THREAD_COUNT];
    int created = 0;
    int failed = 0;
    int can_join = 1;
    int all_joined = 1;

    for (int i = 0; i < THREAD_COUNT; ++i) {
        args[i].id = i + 1;
        args[i].failed = 0;
        args[i].context = &context;

        const int rc = pthread_create(&threads[i], NULL, wait_for_start, &args[i]);
        if (report_pthread_error("pthread_create", rc)) {
            failed = 1;
            break;
        }
        ++created;
    }

    if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&context.mutex))) {
        failed = 1;
        can_join = 0;
    } else {
        while (context.ready_count < created) {
            const int rc = pthread_cond_wait(&context.workers_ready, &context.mutex);
            if (report_pthread_error("pthread_cond_wait (workers_ready)", rc)) {
                failed = 1;
                break;
            }
        }

        if (created == THREAD_COUNT && !failed) {
            printf("Los cinco hilos están listos; se envía la señal de inicio.\n");
        } else if (created < THREAD_COUNT) {
            fprintf(stderr, "Se iniciarán los hilos creados antes del error.\n");
        }

        context.start = 1;
        if (report_pthread_error("pthread_cond_broadcast (start_signal)",
                                 pthread_cond_broadcast(&context.start_signal))) {
            failed = 1;
        }
        if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context.mutex))) {
            failed = 1;
        }
    }

    if (can_join) {
        for (int i = 0; i < created; ++i) {
            const int rc = pthread_join(threads[i], NULL);
            if (report_pthread_error("pthread_join", rc)) {
                failed = 1;
                all_joined = 0;
            } else if (args[i].failed) {
                failed = 1;
            }
        }
    }

    if (all_joined && can_join && destroy_sync_context(&context)) {
        failed = 1;
    }

    if (created != THREAD_COUNT) {
        failed = 1;
    }
    return failed ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "todo") == 0)) {
        printf("=== Ejercicio 1: contador con mutex ===\n");
        if (run_counter_demo() != 0) {
            return 1;
        }
        printf("\n=== Ejercicio 2: sincronización con condición ===\n");
        return run_condition_demo();
    }
    if (argc == 2 && strcmp(argv[1], "contador") == 0) {
        return run_counter_demo();
    }
    if (argc == 2 && strcmp(argv[1], "condicion") == 0) {
        return run_condition_demo();
    }

    fprintf(stderr, "Uso: %s [todo|contador|condicion]\n", argv[0]);
    return 2;
}
