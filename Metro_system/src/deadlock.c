#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t coordination_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t coordination_cond = PTHREAD_COND_INITIALIZER;

static int ready_threads = 0;

typedef struct {
    const char *name;
    pthread_mutex_t *first_lock;
    pthread_mutex_t *second_lock;
} ThreadData;

static void *demonstrate_deadlock_risk(void *arg)
{
    ThreadData *data = (ThreadData *)arg;
    int result;

    /* Each thread acquires a different first lock. */
    pthread_mutex_lock(data->first_lock);

    printf("%s acquired its first lock.\n", data->name);

    /* Wait until both threads hold their first locks. */
    pthread_mutex_lock(&coordination_mutex);

    ready_threads++;

    if (ready_threads == 2) {
        pthread_cond_broadcast(&coordination_cond);
    }

    while (ready_threads < 2) {
        pthread_cond_wait(&coordination_cond, &coordination_mutex);
    }

    pthread_mutex_unlock(&coordination_mutex);

    /* Try the second lock without blocking forever. */
    result = pthread_mutex_trylock(data->second_lock);

    if (result == 0) {
        printf("%s acquired its second lock.\n", data->name);
        pthread_mutex_unlock(data->second_lock);
    } else {
        printf("%s could not acquire its second lock: "
               "deadlock risk detected.\n", data->name);
    }

    pthread_mutex_unlock(data->first_lock);
    return NULL;
}

int main(void)
{
    pthread_t thread1, thread2;

    ThreadData data1 = {
        "Thread 1", &lock_a, &lock_b
    };

    ThreadData data2 = {
        "Thread 2", &lock_b, &lock_a
    };

    printf("DEADLOCK RISK DEMONSTRATION\n\n");

    if (pthread_create(&thread1, NULL,
                       demonstrate_deadlock_risk, &data1) != 0) {
        fprintf(stderr, "Failed to create thread 1.\n");
        return 1;
    }

    if (pthread_create(&thread2, NULL,
                       demonstrate_deadlock_risk, &data2) != 0) {
        fprintf(stderr, "Failed to create thread 2.\n");
        pthread_join(thread1, NULL);
        return 1;
    }

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    printf("\nBoth threads completed; no permanent deadlock occurred.\n");
    printf("Prevention: acquire shared locks in a consistent order.\n");

    pthread_mutex_destroy(&lock_a);
    pthread_mutex_destroy(&lock_b);
    pthread_mutex_destroy(&coordination_mutex);
    pthread_cond_destroy(&coordination_cond);

    return 0;
}