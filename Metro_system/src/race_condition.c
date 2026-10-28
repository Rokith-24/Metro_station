#include <stdio.h>
#include <pthread.h>

#define ITERATIONS 1000000

static long counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *increment_without_mutex(void *arg)
{
    int i;

    (void)arg;

    for (i = 0; i < ITERATIONS; i++) {
        counter++; /* Intentionally unsynchronized */
    }

    return NULL;
}

static void *increment_with_mutex(void *arg)
{
    int i;

    (void)arg;

    for (i = 0; i < ITERATIONS; i++) {
        pthread_mutex_lock(&counter_mutex);
        counter++;
        pthread_mutex_unlock(&counter_mutex);
    }

    return NULL;
}

int main(void)
{
    pthread_t thread1, thread2;
    int expected = ITERATIONS * 2;

    /* Demonstration 1: intentional data race */
    counter = 0;

    if (pthread_create(&thread1, NULL, increment_without_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to create thread 1.\n");
        return 1;
    }

    if (pthread_create(&thread2, NULL, increment_without_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to create thread 2.\n");
        pthread_join(thread1, NULL);
        return 1;
    }

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    printf("RACE CONDITION DEMO\n");
    printf("Expected counter: %d\n", expected);
    printf("Actual counter:   %ld\n\n", counter);

    /* Demonstration 2: protect shared data with a mutex */
    counter = 0;

    if (pthread_create(&thread1, NULL, increment_with_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to create thread 1.\n");
        return 1;
    }

    if (pthread_create(&thread2, NULL, increment_with_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to create thread 2.\n");
        pthread_join(thread1, NULL);
        return 1;
    }

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    printf("MUTEX-PROTECTED DEMO\n");
    printf("Expected counter: %d\n", expected);
    printf("Actual counter:   %ld\n", counter);

    pthread_mutex_destroy(&counter_mutex);
    return 0;
}