
#include <stdio.h>
#include <assert.h>
#include "storage.h"

int main(void)
{
    StationNode *root = NULL;
    AppStatus status;

    status = loadStationsFromCSV(&root, "data/stations.csv");

    assert(status == APP_SUCCESS);
    assert(countStations(root) == 6);

    assert(searchStation(root, 101) != NULL);
    assert(searchStation(root, 106) != NULL);
    assert(searchStation(root, 999) == NULL);

    status = saveStationsToCSV(root, "data/stations_test.csv");

    assert(status == APP_SUCCESS);

    printf("Stations loaded: %d\n", countStations(root));
    printf("Station 101 and 106 found successfully.\n");
    printf("CSV save test passed!\n");

    freeStationTree(root);
    return 0;
}
