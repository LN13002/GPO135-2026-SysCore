#include <pthread.h>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

const int THREAD_COUNT = 5;
const int INCREMENTS_PER_THREAD = 100000;

long long shared_counter = 0;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

struct WorkerArgs {
    int id;
};

void *increment_counter(void *raw_args) {
    const WorkerArgs *args = static_cast<const WorkerArgs *>(raw_args);
    for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
        pthread_mutex_lock(&counter_mutex);
        ++shared_counter;
        pthread_mutex_unlock(&counter_mutex);
    }
    std::cout << "Hilo " << args->id << " terminó sus incrementos.\n";
    return NULL;
}

int run_counter_demo() {
    pthread_t threads[THREAD_COUNT];
    WorkerArgs args[THREAD_COUNT];
    int created = 0;

    for (int i = 0; i < THREAD_COUNT; ++i) {
        args[i].id = i + 1;
        const int rc = pthread_create(&threads[i], NULL, increment_counter, &args[i]);
        if (rc != 0) {
            std::cerr << "pthread_create: " << rc << "\n";
            for (int j = 0; j < created; ++j) pthread_join(threads[j], NULL);
            return 1;
        }
        ++created;
    }

    for (int i = 0; i < created; ++i) {
        const int rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            std::cerr << "pthread_join: " << rc << "\n";
            return 1;
        }
    }
    pthread_mutex_destroy(&counter_mutex);
    std::cout << "Contador final: " << shared_counter << " (esperado: "
              << THREAD_COUNT * INCREMENTS_PER_THREAD << ")\n";
    return 0;
}

pthread_mutex_t sync_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t workers_ready = PTHREAD_COND_INITIALIZER;
pthread_cond_t start_signal = PTHREAD_COND_INITIALIZER;
int ready_count = 0;
bool start = false;

void *wait_for_start(void *raw_args) {
    const WorkerArgs *args = static_cast<const WorkerArgs *>(raw_args);
    pthread_mutex_lock(&sync_mutex);
    ++ready_count;
    pthread_cond_signal(&workers_ready);
    while (!start) {
        pthread_cond_wait(&start_signal, &sync_mutex);
    }
    pthread_mutex_unlock(&sync_mutex);

    std::cout << "Hilo " << args->id << " recibió la señal de inicio.\n";
    return NULL;
}

int run_condition_demo() {
    pthread_t threads[THREAD_COUNT];
    WorkerArgs args[THREAD_COUNT];
    int created = 0;

    for (int i = 0; i < THREAD_COUNT; ++i) {
        args[i].id = i + 1;
        const int rc = pthread_create(&threads[i], NULL, wait_for_start, &args[i]);
        if (rc != 0) {
            std::cerr << "pthread_create: " << rc << "\n";
            pthread_mutex_lock(&sync_mutex);
            start = true;
            pthread_cond_broadcast(&start_signal);
            pthread_mutex_unlock(&sync_mutex);
            for (int j = 0; j < created; ++j) pthread_join(threads[j], NULL);
            return 1;
        }
        ++created;
    }

    pthread_mutex_lock(&sync_mutex);
    while (ready_count < THREAD_COUNT) {
        pthread_cond_wait(&workers_ready, &sync_mutex);
    }
    std::cout << "Los cinco hilos están listos; se envía la señal de inicio.\n";
    start = true;
    pthread_cond_broadcast(&start_signal);
    pthread_mutex_unlock(&sync_mutex);

    for (int i = 0; i < created; ++i) {
        const int rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            std::cerr << "pthread_join: " << rc << "\n";
            return 1;
        }
    }
    pthread_cond_destroy(&workers_ready);
    pthread_cond_destroy(&start_signal);
    pthread_mutex_destroy(&sync_mutex);
    return 0;
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
