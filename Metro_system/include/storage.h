
#ifndef STORAGE_H
#define STORAGE_H

#include "bst.h"

AppStatus loadStationsFromCSV(
    StationNode **root,
    const char *filename
);

AppStatus saveStationsToCSV(
    const StationNode *root,
    const char *filename
);

#endif
