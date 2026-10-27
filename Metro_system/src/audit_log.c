
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "audit_log.h"

AppStatus writeAuditLog(
    const char *username,
    const char *action,
    const char *details,
    AppStatus result)
{
    FILE *file;
    time_t currentTime;
    struct tm *timeInfo;
    char timestamp[32];

    if (username == NULL || action == NULL || details == NULL) {
        return APP_INVALID_INPUT;
    }

    currentTime = time(NULL);
    timeInfo = localtime(&currentTime);

    if (timeInfo == NULL ||
        strftime(timestamp, sizeof(timestamp),
                 "%Y-%m-%d %H:%M:%S", timeInfo) == 0) {
        return APP_ERROR;
    }

    file = fopen(AUDIT_FILE, "a");

    if (file == NULL) {
        return APP_ERROR;
    }

    if (fprintf(file, "[%s] user=%s action=%s details=%s result=%d\n",
                timestamp, username, action, details, (int)result) < 0) {
        fclose(file);
        return APP_ERROR;
    }

    if (fclose(file) != 0) {
        return APP_ERROR;
    }

    return APP_SUCCESS;
}
