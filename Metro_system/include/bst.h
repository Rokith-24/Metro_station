
#ifndef BST_H
#define BST_H

#include "station.h"

StationNode *createNode(Station station);

AppStatus insertStation(StationNode **root, Station station);
StationNode *searchStation(StationNode *root, int id);

AppStatus updateStation(StationNode *root, int id, Station updatedStation);
AppStatus deleteStation(StationNode **root, int id);

void displayStationsInOrder(const StationNode *root);
void displayStation(const Station *station);

int countStations(const StationNode *root);
int findMinimumStationId(const StationNode *root, int *id);
int findMaximumStationId(const StationNode *root, int *id);

void freeStationTree(StationNode *root);

#endif
