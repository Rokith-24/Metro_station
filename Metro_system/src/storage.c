
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "storage.h"

#define CSV_LINE_MAX 256

static int parseStation(char *line, Station *station)
{
    char *token;
    char *end;
    long id;
    long platform;
    float distance;
    float fare;

    if (line == NULL || station == NULL) {
        return 0;
    }

    token = strtok(line, ",");
    if (token == NULL || strcmp(token, "id") == 0) {
        return 0;
    }

    errno = 0;
    id = strtol(token, &end, 10);
    if (errno != 0 || *end != '\0' || id <= 0 || id > 2147483647L) {
        return 0;
    }

    token = strtok(NULL, ",");
    if (token == NULL || strlen(token) == 0 ||
        strlen(token) >= STATION_NAME_MAX) {
        return 0;
    }

    memset(station, 0, sizeof(*station));
    memcpy(station->name, token, strlen(token));

    token = strtok(NULL, ",");
    if (token == NULL) {
        return 0;
    }

    errno = 0;
    distance = strtof(token, &end);
    if (errno != 0 || *end != '\0' || !isfinite(distance) || distance < 0.0f) {
        return 0;
    }

    token = strtok(NULL, ",");
    if (token == NULL) {
        return 0;
    }

    errno = 0;
    fare = strtof(token, &end);
    if (errno != 0 || *end != '\0' || !isfinite(fare) || fare < 0.0f) {
        return 0;
    }

    token = strtok(NULL, ",");
    if (token == NULL) {
        return 0;
    }

    errno = 0;
    platform = strtol(token, &end, 10);
    if (errno != 0 || *end != '\0' || platform <= 0 ||
        platform > 2147483647L) {
        return 0;
    }

    if (strtok(NULL, ",") != NULL) {
        return 0;
    }

    station->id = (int)id;
    station->distance = distance;
    station->fare = fare;
    station->platform = (int)platform;

    return 1;
}

static AppStatus writeStation(FILE *file, const Station *station)
{
    if (fprintf(file, "%d,%s,%.2f,%.2f,%d\n",
                station->id,
                station->name,
                station->distance,
                station->fare,
                station->platform) < 0) {
        return APP_ERROR;
    }

    return APP_SUCCESS;
}

static AppStatus writeTreeInOrder(FILE *file, const StationNode *root)
{
    AppStatus status;

    if (root == NULL) {
        return APP_SUCCESS;
    }

    status = writeTreeInOrder(file, root->left);
    if (status != APP_SUCCESS) {
        return status;
    }

    status = writeStation(file, &root->station);
    if (status != APP_SUCCESS) {
        return status;
    }

    return writeTreeInOrder(file, root->right);
}

AppStatus loadStationsFromCSV(StationNode **root, const char *filename)
{
    FILE *file;
    char line[CSV_LINE_MAX];
    unsigned long lineNumber = 0;
    AppStatus overallStatus = APP_SUCCESS;

    if (root == NULL || filename == NULL) {
        return APP_INVALID_INPUT;
    }

    file = fopen(filename, "r");
    if (file == NULL) {
        return APP_ERROR;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        Station station;
        size_t length = strlen(line);
        int hasNewline = (length > 0 && line[length - 1] == '\n');

        /*
         * fgets() may return only a chunk of a physical CSV row when the row
         * is longer than the buffer. Do not feed the remaining chunks back
         * through the parser as if they were separate records. Consume the
         * rest of that physical row and report exactly one invalid record.
         */
        if (!hasNewline && length == sizeof(line) - 1) {
            int next = fgetc(file);

            if (next != EOF) {
                while (next != '\n' && next != EOF) {
                    next = fgetc(file);
                }
                lineNumber++;
                fprintf(stderr,
                        "Invalid station record at line %lu (row too long)\n",
                        lineNumber);
                overallStatus = APP_ERROR;
                continue;
            }
            /* EOF immediately after the buffer: this is the final chunk,
             * not a row with more unread data. Parse it normally below. */
        }

        lineNumber++;

        if (hasNewline) {
            line[length - 1] = '\0';
        }

        length = strlen(line);
        if (length > 0 && line[length - 1] == '\r') {
            line[length - 1] = '\0';
        }

        if (lineNumber == 1 && strcmp(line, "id,name,distance,fare,platform") == 0) {
            continue;
        }

        if (!parseStation(line, &station)) {
            fprintf(stderr, "Invalid station record at line %lu\n", lineNumber);
            overallStatus = APP_ERROR;
            continue;
        }

        {
            AppStatus status = insertStation(root, station);

            if (status != APP_SUCCESS) {
                fprintf(stderr, "Could not load station ID %d at line %lu\n",
                        station.id, lineNumber);
                overallStatus = status;
            }
        }
    }

    if (ferror(file)) {
        overallStatus = APP_ERROR;
    }

    if (fclose(file) != 0) {
        overallStatus = APP_ERROR;
    }

    return overallStatus;
}

AppStatus saveStationsToCSV(const StationNode *root, const char *filename)
{
    FILE *file;
    AppStatus status;

    if (filename == NULL) {
        return APP_INVALID_INPUT;
    }

    file = fopen(filename, "w");
    if (file == NULL) {
        return APP_ERROR;
    }

    if (fprintf(file, "id,name,distance,fare,platform\n") < 0) {
        fclose(file);
        return APP_ERROR;
    }

    status = writeTreeInOrder(file, root);

    if (fclose(file) != 0) {
        status = APP_ERROR;
    }

    return status;
}
