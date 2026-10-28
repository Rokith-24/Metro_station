
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "auth.h"

#define USER_LINE_MAX 256

static void trimNewline(char *text)
{
    size_t length;

    if (text == NULL) {
        return;
    }

    length = strlen(text);

    while (length > 0 &&
           (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
}

AppStatus authenticateUser(
    const char *username,
    const char *password,
    User *authenticatedUser)
{
    FILE *file;
    char line[USER_LINE_MAX];

    if (username == NULL || password == NULL ||
        authenticatedUser == NULL ||
        username[0] == '\0' || password[0] == '\0') {
        return APP_INVALID_INPUT;
    }

    file = fopen(USERS_FILE, "r");

    if (file == NULL) {
        return APP_ERROR;
    }

    /* Skip the CSV header. */
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return APP_ERROR;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *storedUsername;
        char *storedPassword;
        char *storedRole;

        trimNewline(line);

        storedUsername = strtok(line, ",");
        storedPassword = strtok(NULL, ",");
        storedRole = strtok(NULL, ",");

        if (storedUsername == NULL ||
            storedPassword == NULL ||
            storedRole == NULL ||
            strtok(NULL, ",") != NULL) {
            continue;
        }

        if (strcmp(username, storedUsername) == 0 &&
            strcmp(password, storedPassword) == 0) {

            if (strcmp(storedRole, "ADMIN") == 0) {
                authenticatedUser->role = ROLE_ADMIN;
            } else if (strcmp(storedRole, "VIEWER") == 0) {
                authenticatedUser->role = ROLE_VIEWER;
            } else {
                fclose(file);
                return APP_ERROR;
            }

            if (strlen(storedUsername) >= USERNAME_MAX) {
                fclose(file);
                return APP_ERROR;
            }

            strcpy(authenticatedUser->username, storedUsername);

            fclose(file);
            return APP_SUCCESS;
        }
    }

    if (ferror(file)) {
        fclose(file);
        return APP_ERROR;
    }

    fclose(file);
    return APP_PERMISSION_DENIED;
}

int hasPermission(UserRole role, const char *action)
{
    if (action == NULL) {
        return 0;
    }

    if (strcmp(action, "VIEW") == 0 ||
        strcmp(action, "SEARCH") == 0) {
        return role == ROLE_ADMIN || role == ROLE_VIEWER;
    }

    if (strcmp(action, "ADD") == 0 ||
        strcmp(action, "UPDATE") == 0 ||
        strcmp(action, "DELETE") == 0) {
        return role == ROLE_ADMIN;
    }
    if (strcmp(action, "VALIDATE") == 0) {
        return role == ROLE_ADMIN;
    }

    return 0;
}
