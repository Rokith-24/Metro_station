
#ifndef STATION_H
#define STATION_H

#include "common.h"

typedef struct {
    int id;
    char name[STATION_NAME_MAX];
    float distance;
    float fare;
    int platform;
} Station;

typedef struct StationNode {
    Station station;
    int height;
    struct StationNode *left;
    struct StationNode *right;
} StationNode;

#endif
