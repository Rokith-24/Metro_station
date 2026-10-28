
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include "station_validation_threads.h"

typedef struct {
    const Station *stations;
    unsigned int *issue_flags;

    size_t start;
    size_t end;

    size_t checked_count;
    size_t invalid_count;
} ValidationWorkerData;

/* Copy AVL records into a stable array in sorted ID order. */
static void copyStationsInOrder(
    const StationNode *root,
    Station *stations,
    size_t *index
)
{
    if (root == NULL) {
        return;
    }

    copyStationsInOrder(root->left, stations, index);

    stations[*index] = root->station;
    (*index)++;

    copyStationsInOrder(root->right, stations, index);
}

/* Validate one station record. */
static unsigned int validateStation(const Station *station)
{
    unsigned int issues = 0;

    if (station->id <= 0) {
        issues |= ISSUE_INVALID_ID;
    }

    if (station->name[0] == '\0') {
        issues |= ISSUE_EMPTY_NAME;
    }

    if (!isfinite(station->distance) ||
        station->distance < 0.0f) {
        issues |= ISSUE_INVALID_DISTANCE;
    }

    if (!isfinite(station->fare) ||
        station->fare < 0.0f) {
        issues |= ISSUE_INVALID_FARE;
    }

    if (station->platform <= 0) {
        issues |= ISSUE_INVALID_PLATFORM;
    }

    return issues;
}

/* Worker thread: process only the assigned range. */
static void *validateStationRange(void *argument)
{
    ValidationWorkerData *data =
        (ValidationWorkerData *)argument;


    data->checked_count = 0;
    data->invalid_count = 0;

    for (size_t i = data->start; i < data->end; i++) {
        unsigned int issues =
            validateStation(&data->stations[i]);

        /*
         * Each worker writes to a separate range of the array.
         * Therefore, workers do not write to the same element.
         */
        data->issue_flags[i] = issues;

        data->checked_count++;

        if (issues != 0) {
            data->invalid_count++;
        }
    }

    return NULL;
}

AppStatus generateStationValidationReport(
    const StationNode *root,
    StationValidationReport *report
)
{
    size_t count;
    size_t index = 0;
    size_t midpoint;

    pthread_t thread1;
    pthread_t thread2;

    ValidationWorkerData worker1;
    ValidationWorkerData worker2;

    if (report == NULL) {
        return APP_INVALID_INPUT;
    }

    memset(report, 0, sizeof(*report));

    count = (size_t)countStations(root);

    /* An empty tree is a valid input to the report function. */
    if (count == 0) {
        return APP_SUCCESS;
    }

    report->stations =
        malloc(count * sizeof(*report->stations));

    report->issue_flags =
        calloc(count, sizeof(*report->issue_flags));

    if (report->stations == NULL ||
        report->issue_flags == NULL) {
        freeStationValidationReport(report);
        return APP_ERROR;
    }

    copyStationsInOrder(root, report->stations, &index);
    report->station_count = index;

    if (index != count) {
        freeStationValidationReport(report);
        return APP_ERROR;
    }

    midpoint = (count + 1) / 2;

    worker1.stations = report->stations;
    worker1.issue_flags = report->issue_flags;
    worker1.start = 0;
    worker1.end = midpoint;
    worker1.checked_count = 0;
    worker1.invalid_count = 0;

    worker2.stations = report->stations;
    worker2.issue_flags = report->issue_flags;
    worker2.start = midpoint;
    worker2.end = count;
    worker2.checked_count = 0;
    worker2.invalid_count = 0;

    /* Start the first worker. */
    if (pthread_create(
            &thread1, NULL, validateStationRange, &worker1) != 0) {
        freeStationValidationReport(report);
        return APP_ERROR;
    }

    /* Start the second worker. */
    if (pthread_create(
            &thread2, NULL, validateStationRange, &worker2) != 0) {
        pthread_join(thread1, NULL);
        freeStationValidationReport(report);
        return APP_ERROR;
    }

    /* Wait for both workers to finish before reading their results. */
    if (pthread_join(thread1, NULL) != 0) {
        pthread_join(thread2, NULL);
        freeStationValidationReport(report);
        return APP_ERROR;
    }

    if (pthread_join(thread2, NULL) != 0) {
        freeStationValidationReport(report);
        return APP_ERROR;
    }

    report->worker1_checked = worker1.checked_count;
    report->worker2_checked = worker2.checked_count;

    report->invalid_count =
        worker1.invalid_count + worker2.invalid_count;

    return APP_SUCCESS;
}

void displayStationValidationReport(
    const StationValidationReport *report
)
{

    if (report == NULL) {
        printf("Error: validation report is unavailable.\n");
        return;
    }

    printf("\n========== STATION DATA VALIDATION REPORT ==========\n");

    if (report->station_count == 0) {
        printf("No stations are available for validation.\n");
        return;
    }

    printf("Total records checked: %lu\n",
           (unsigned long)report->station_count);

    printf("Worker 1 processed: %lu records\n",
           (unsigned long)report->worker1_checked);

    printf("Worker 2 processed: %lu records\n",
           (unsigned long)report->worker2_checked);

    printf("Valid records: %lu\n",
           (unsigned long)(
               report->station_count - report->invalid_count));

    printf("Records with issues: %lu\n",
           (unsigned long)report->invalid_count);

    if (report->invalid_count == 0) {
        printf("\nAll station records passed validation.\n");
    } else {
        printf("\nStation records requiring attention:\n");

        for (size_t i = 0; i < report->station_count; i++) {
            unsigned int flags = report->issue_flags[i];

            if (flags == 0) {
                continue;
            }

            printf("\nStation ID: %d\n",
                   report->stations[i].id);

            if (flags & ISSUE_INVALID_ID) {
                printf("  - Station ID must be positive.\n");
            }

            if (flags & ISSUE_EMPTY_NAME) {
                printf("  - Station name is empty.\n");
            }

            if (flags & ISSUE_INVALID_DISTANCE) {
                printf("  - Distance is invalid or negative.\n");
            }

            if (flags & ISSUE_INVALID_FARE) {
                printf("  - Fare is invalid or negative.\n");
            }

            if (flags & ISSUE_INVALID_PLATFORM) {
                printf("  - Platform number must be positive.\n");
            }
        }
    }

    printf("====================================================\n");
}

void freeStationValidationReport(
    StationValidationReport *report
)
{
    if (report == NULL) {
        return;
    }

    free(report->stations);
    free(report->issue_flags);

    memset(report, 0, sizeof(*report));
}
