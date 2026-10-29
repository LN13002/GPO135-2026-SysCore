#include <pthread.h>

#include <cstring>
#include <iostream>
#include <string>

namespace {

const int THREAD_COUNT = 5;
const int INCREMENTS_PER_THREAD = 100000;

bool report_pthread_error(const char *operation, int error_code) {
    if (error_code == 0) return false;
    std::cerr << operation << ": " << std::strerror(error_code) << "\n";
    return true;
}

struct CounterContext;

struct WorkerArgs {
    int id;
    bool failed;
    CounterContext *context;
};

struct CounterContext {
    long long value;
    pthread_mutex_t mutex;
};

void *increment_counter(void *raw_args) {
    WorkerArgs *args = static_cast<WorkerArgs *>(raw_args);
    CounterContext *context = static_cast<CounterContext *>(args->context);
    for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
        if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&context->mutex))) {
            args->failed = true;
            return NULL;
        }
        ++context->value;
        if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context->mutex))) {
            args->failed = true;
            return NULL;
        }
    }
    if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&context->mutex))) {
        args->failed = true;
        return NULL;
    }
    std::cout << "Hilo " << args->id << " terminó sus incrementos.\n";
    if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context->mutex))) {
        args->failed = true;
    }
    return NULL;
}

int run_counter_demo() {
    CounterContext context = {0, pthread_mutex_t()};
    if (report_pthread_error("pthread_mutex_init", pthread_mutex_init(&context.mutex, NULL))) {
        return 1;
    }

    pthread_t threads[THREAD_COUNT];
    WorkerArgs args[THREAD_COUNT];
    int created = 0;
    bool failed = false;

    for (int i = 0; i < THREAD_COUNT; ++i) {
        args[i].id = i + 1;
        args[i].failed = false;
        args[i].context = &context;
        const int rc = pthread_create(&threads[i], NULL, increment_counter, &args[i]);
        if (report_pthread_error("pthread_create", rc)) {
            failed = true;
            break;
        }
        ++created;
    }

    bool all_joined = true;
    for (int i = 0; i < created; ++i) {
        if (report_pthread_error("pthread_join", pthread_join(threads[i], NULL))) {
            failed = true;
            all_joined = false;
        } else if (args[i].failed) {
            failed = true;
        }
    }

    if (created == THREAD_COUNT) {
        std::cout << "Contador final: " << context.value << " (esperado: "
                  << THREAD_COUNT * INCREMENTS_PER_THREAD << ")\n";
    }
    if (all_joined && report_pthread_error("pthread_mutex_destroy",
                                           pthread_mutex_destroy(&context.mutex))) {
        failed = true;
    }
    return failed ? 1 : 0;
}

struct SyncContext;

struct SyncWorkerArgs {
    int id;
    bool failed;
    SyncContext *context;
};

struct SyncContext {
    pthread_mutex_t mutex;
    pthread_cond_t workers_ready;
    pthread_cond_t start_signal;
    int ready_count;
    bool start;
};

bool initialize_sync_context(SyncContext *context) {
    context->ready_count = 0;
    context->start = false;
    if (report_pthread_error("pthread_mutex_init", pthread_mutex_init(&context->mutex, NULL))) {
        return false;
    }
    if (report_pthread_error("pthread_cond_init (workers_ready)",
                             pthread_cond_init(&context->workers_ready, NULL))) {
        report_pthread_error("pthread_mutex_destroy", pthread_mutex_destroy(&context->mutex));
        return false;
    }
    if (report_pthread_error("pthread_cond_init (start_signal)",
                             pthread_cond_init(&context->start_signal, NULL))) {
        report_pthread_error("pthread_cond_destroy (workers_ready)",
                             pthread_cond_destroy(&context->workers_ready));
        report_pthread_error("pthread_mutex_destroy", pthread_mutex_destroy(&context->mutex));
        return false;
    }
    return true;
}

bool destroy_sync_context(SyncContext *context) {
    bool failed = false;
    if (report_pthread_error("pthread_cond_destroy (workers_ready)",
                             pthread_cond_destroy(&context->workers_ready))) failed = true;
    if (report_pthread_error("pthread_cond_destroy (start_signal)",
                             pthread_cond_destroy(&context->start_signal))) failed = true;
    if (report_pthread_error("pthread_mutex_destroy", pthread_mutex_destroy(&context->mutex))) {
        failed = true;
    }
    return failed;
}

void *wait_for_start(void *raw_args) {
    SyncWorkerArgs *args = static_cast<SyncWorkerArgs *>(raw_args);
    SyncContext *context = args->context;
    if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&context->mutex))) {
        args->failed = true;
        return NULL;
    }

    ++context->ready_count;
    if (report_pthread_error("pthread_cond_signal (workers_ready)",
                             pthread_cond_signal(&context->workers_ready))) {
        args->failed = true;
    }
    while (!context->start) {
        if (report_pthread_error("pthread_cond_wait (start_signal)",
                                 pthread_cond_wait(&context->start_signal, &context->mutex))) {
            args->failed = true;
            report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context->mutex));
            return NULL;
        }
    }
    std::cout << "Hilo " << args->id << " recibió la señal de inicio.\n";
    if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context->mutex))) {
        args->failed = true;
        return NULL;
    }
    return NULL;
}

int run_condition_demo() {
    SyncContext context;
    if (!initialize_sync_context(&context)) return 1;

    pthread_t threads[THREAD_COUNT];
    SyncWorkerArgs args[THREAD_COUNT];
    int created = 0;
    bool failed = false;

    for (int i = 0; i < THREAD_COUNT; ++i) {
        args[i].id = i + 1;
        args[i].failed = false;
        args[i].context = &context;
        const int rc = pthread_create(&threads[i], NULL, wait_for_start, &args[i]);
        if (report_pthread_error("pthread_create", rc)) {
            failed = true;
            break;
        }
        ++created;
    }

    if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&context.mutex))) {
        failed = true;
    } else {
        while (context.ready_count < created) {
            const int rc = pthread_cond_wait(&context.workers_ready, &context.mutex);
            if (report_pthread_error("pthread_cond_wait (workers_ready)", rc)) {
                failed = true;
                break;
            }
        }
        if (created == THREAD_COUNT && !failed) {
            std::cout << "Los cinco hilos están listos; se envía la señal de inicio.\n";
        } else if (created < THREAD_COUNT) {
            std::cerr << "Se iniciarán los hilos creados antes del error.\n";
        }
        context.start = true;
        if (report_pthread_error("pthread_cond_broadcast (start_signal)",
                                 pthread_cond_broadcast(&context.start_signal))) {
            failed = true;
        }
        if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context.mutex))) {
            failed = true;
        }
    }

    bool all_joined = true;
    for (int i = 0; i < created; ++i) {
        if (report_pthread_error("pthread_join", pthread_join(threads[i], NULL))) {
            failed = true;
            all_joined = false;
        } else if (args[i].failed) {
            failed = true;
        }
    }

    if (all_joined && destroy_sync_context(&context)) failed = true;
    return failed ? 1 : 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 1 || std::string(argv[1]) == "todo") {
        std::cout << "=== Ejercicio 1: contador con mutex ===\n";
        if (run_counter_demo() != 0) return 1;
        std::cout << "\n=== Ejercicio 2: sincronización con condición ===\n";
        return run_condition_demo();
    }
    if (std::string(argv[1]) == "contador") return run_counter_demo();
    if (std::string(argv[1]) == "condicion") return run_condition_demo();

    std::cerr << "Uso: " << argv[0] << " [todo|contador|condicion]\n";
    return 2;
}
