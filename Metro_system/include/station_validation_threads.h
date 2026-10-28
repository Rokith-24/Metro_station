
#ifndef STATION_VALIDATION_THREADS_H
#define STATION_VALIDATION_THREADS_H

#include <stddef.h>

#include "common.h"
#include "bst.h"

#define ISSUE_INVALID_ID       1u
#define ISSUE_EMPTY_NAME       2u
#define ISSUE_INVALID_DISTANCE 4u
#define ISSUE_INVALID_FARE     8u
#define ISSUE_INVALID_PLATFORM 16u

typedef struct {
    Station *stations;
    unsigned int *issue_flags;

    size_t station_count;
    size_t invalid_count;

    size_t worker1_checked;
    size_t worker2_checked;
} StationValidationReport;

AppStatus generateStationValidationReport(
    const StationNode *root,
    StationValidationReport *report
);

void displayStationValidationReport(
    const StationValidationReport *report
);

void freeStationValidationReport(
    StationValidationReport *report
);

#endif
