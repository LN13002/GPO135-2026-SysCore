#include <pthread.h>
#include <stdio.h>
#include <string.h>

/*
 * Configuración general del programa:
 * THREAD_COUNT define la cantidad de hilos de cada ejercicio.
 * INCREMENTS_PER_THREAD establece los incrementos que realizará
 * cada hilo en el ejercicio del contador.
 */
enum {
    THREAD_COUNT = 5,
    INCREMENTS_PER_THREAD = 100000
};

/*
 * Variable global compartida por los hilos del primer ejercicio.
 * Todos sus incrementos se protegen mediante counter_mutex.
 */
int shared_counter = 0;

/* Mutex estático que proporciona exclusión mutua sobre el contador. */
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Argumentos y estado de cada trabajador del ejercicio 1. */
typedef struct {
    int id;
    int failed;
} CounterWorkerArgs;

/*
 * Estado compartido del ejercicio 2:
 * - mutex protege los campos y coordina el acceso al contexto.
 * - workers_ready permite al hilo principal esperar a los trabajadores.
 * - start_signal permite notificar que pueden continuar.
 * - ready_count registra cuántos trabajadores están preparados.
 * - start indica si ya se autorizó el inicio.
 */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t workers_ready;
    pthread_cond_t start_signal;
    int ready_count;
    int start;
} SyncContext;

/* Argumentos de cada trabajador del ejercicio 2. */
typedef struct {
    int id;
    int failed;
    SyncContext *context;
} SyncWorkerArgs;

/*
 * Centraliza la comprobación de errores de las funciones pthread.
 * Devuelve 0 si la operación fue exitosa y 1 si ocurrió un error.
 */
static int report_pthread_error(const char *operation, int error_code) {
    if (error_code == 0) {
        return 0;
    }
    fprintf(stderr, "%s: %s (código %d)\n", operation, strerror(error_code), error_code);
    return 1;
}

/*
 * Función ejecutada por cada hilo del ejercicio 1.
 * Cada trabajador realiza INCREMENTS_PER_THREAD incrementos.
 * El mutex protege la sección crítica para evitar condiciones de carrera.
 */
static void *increment_counter(void *raw_args) {
    CounterWorkerArgs *args = raw_args;

    for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
        /* Solicita acceso exclusivo antes de modificar el contador. */
        if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&counter_mutex))) {
            args->failed = 1;
            return NULL;
        }

        /* Sección crítica: el incremento se realiza con el mutex adquirido. */
        ++shared_counter;

        /* Libera el mutex para que otro hilo pueda acceder al contador. */
        if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&counter_mutex))) {
            args->failed = 1;
            return NULL;
        }
    }

    printf("Hilo %d terminó sus incrementos.\n", args->id);
    return NULL;
}

/*
 * Ejecuta el primer ejercicio: crea los cinco trabajadores,
 * espera su finalización y compara el contador con el total esperado.
 */
static int run_counter_demo(void) {
    pthread_t threads[THREAD_COUNT];
    CounterWorkerArgs args[THREAD_COUNT];
    int created = 0;
    int failed = 0;
    int all_joined = 1;

    /* Reinicia el recurso compartido antes de iniciar el ejercicio. */
    shared_counter = 0;

    /* Crea los hilos y prepara los argumentos individuales de cada uno. */
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

    /* Espera a que todos los hilos creados terminen su ejecución. */
    for (int i = 0; i < created; ++i) {
        const int rc = pthread_join(threads[i], NULL);
        if (report_pthread_error("pthread_join", rc)) {
            failed = 1;
            all_joined = 0;
        } else if (args[i].failed) {
            failed = 1;
        }
    }

    /* Verifica el resultado únicamente si se crearon y unieron todos los hilos. */
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

    /* Destruye el mutex cuando todos los trabajadores ya finalizaron. */
    if (all_joined && report_pthread_error("pthread_mutex_destroy",
                                           pthread_mutex_destroy(&counter_mutex))) {
        failed = 1;
    }

    return failed ? 1 : 0;
}

/*
 * Inicializa el contexto de sincronización del segundo ejercicio.
 * Si falla una inicialización, libera los recursos que ya se crearon.
 */
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

/*
 * Destruye las variables de condición y el mutex del contexto.
 * Esta función debe ejecutarse cuando los trabajadores ya no los utilizan.
 */
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

/*
 * Función ejecutada por cada trabajador del ejercicio 2.
 * Cada hilo registra que está preparado y espera la señal de inicio
 * enviada por el hilo principal mediante una variable de condición.
 */
static void *wait_for_start(void *raw_args) {
    SyncWorkerArgs *args = raw_args;
    SyncContext *context = args->context;

    /* Adquiere el mutex para modificar y consultar el estado compartido. */
    if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&context->mutex))) {
        args->failed = 1;
        return NULL;
    }

    /* Registra que este trabajador ya está preparado para continuar. */
    ++context->ready_count;
    if (report_pthread_error("pthread_cond_signal (workers_ready)",
                             pthread_cond_signal(&context->workers_ready))) {
        args->failed = 1;
    }

    /*
     * Espera mientras no se haya activado la condición de inicio.
     * pthread_cond_wait libera el mutex mientras el hilo espera y lo
     * vuelve a adquirir antes de retornar. Se utiliza while para
     * comprobar nuevamente el predicado después de cada despertar.
     */
    while (!context->start) {
        const int rc = pthread_cond_wait(&context->start_signal, &context->mutex);
        if (report_pthread_error("pthread_cond_wait (start_signal)", rc)) {
            args->failed = 1;
            report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context->mutex));
            return NULL;
        }
    }

    /* El trabajador continúa cuando el hilo principal activa start. */
    printf("Hilo %d recibió la señal de inicio.\n", args->id);
    if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context->mutex))) {
        args->failed = 1;
    }

    return NULL;
}

/*
 * Ejecuta el segundo ejercicio: crea los trabajadores, espera hasta
 * que todos estén preparados, activa la condición de inicio y notifica
 * a los hilos bloqueados mediante pthread_cond_broadcast().
 */
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

    /* Crea los cinco trabajadores y les asigna el contexto compartido. */
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

    /* El hilo principal también adquiere el mutex para revisar ready_count. */
    if (report_pthread_error("pthread_mutex_lock", pthread_mutex_lock(&context.mutex))) {
        failed = 1;
        can_join = 0;
    } else {
        /* Espera hasta que todos los trabajadores creados indiquen que están listos. */
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

        /* Cambia el predicado compartido antes de notificar a los trabajadores. */
        context.start = 1;

        /* Despierta a todos los hilos que esperan en start_signal. */
        if (report_pthread_error("pthread_cond_broadcast (start_signal)",
                                 pthread_cond_broadcast(&context.start_signal))) {
            failed = 1;
        }
        if (report_pthread_error("pthread_mutex_unlock", pthread_mutex_unlock(&context.mutex))) {
            failed = 1;
        }
    }

    /* Espera la finalización de los trabajadores antes de liberar el contexto. */
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

    /* Libera los recursos solo cuando es seguro hacerlo. */
    if (all_joined && can_join && destroy_sync_context(&context)) {
        failed = 1;
    }

    if (created != THREAD_COUNT) {
        failed = 1;
    }
    return failed ? 1 : 0;
}

/*
 * Punto de entrada del programa.
 * Permite ejecutar ambos ejercicios o seleccionar uno mediante
 * los argumentos: todo, contador o condicion.
 */
int main(int argc, char **argv) {
    /* Sin argumento o con "todo": ejecuta los dos ejercicios. */
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "todo") == 0)) {
        printf("=== Ejercicio 1: contador con mutex ===\n");
        if (run_counter_demo() != 0) {
            return 1;
        }
        printf("\n=== Ejercicio 2: sincronización con condición ===\n");
        return run_condition_demo();
    }

    /* Ejecuta únicamente el ejercicio del contador protegido con mutex. */
    if (argc == 2 && strcmp(argv[1], "contador") == 0) {
        return run_counter_demo();
    }

    /* Ejecuta únicamente el ejercicio de sincronización condicional. */
    if (argc == 2 && strcmp(argv[1], "condicion") == 0) {
        return run_condition_demo();
    }

    /* Informa el uso correcto cuando se recibe un argumento no válido. */
    fprintf(stderr, "Uso: %s [todo|contador|condicion]\n", argv[0]);
    return 2;
}
