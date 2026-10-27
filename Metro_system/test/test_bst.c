
#include <stdio.h>
#include <assert.h>
#include "bst.h"

static Station makeStation(int id, const char *name)
{
    Station s = {0};

    s.id = id;
    snprintf(s.name, sizeof(s.name), "%s", name);
    s.distance = (float)id / 10.0f;
    s.fare = 10.0f;
    s.platform = 1;

    return s;
}

int main(void)
{
    StationNode *root = NULL;

    assert(insertStation(&root, makeStation(30, "Central")) == APP_SUCCESS);
    assert(insertStation(&root, makeStation(20, "Park")) == APP_SUCCESS);
    assert(insertStation(&root, makeStation(10, "Airport")) == APP_SUCCESS);

    /* LL rotation should make 20 the root. */
    assert(root != NULL);
    assert(root->station.id == 20);

    assert(searchStation(root, 10) != NULL);
    assert(searchStation(root, 30) != NULL);
    assert(searchStation(root, 99) == NULL);

    assert(countStations(root) == 3);

    assert(deleteStation(&root, 20) == APP_SUCCESS);
    assert(searchStation(root, 20) == NULL);
    assert(countStations(root) == 2);

    freeStationTree(root);

    printf("All basic AVL tree tests passed!\n");
    return 0;
}
