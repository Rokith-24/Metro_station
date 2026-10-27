
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "common.h"
#include "station.h"
#include "bst.h"
#include "storage.h"
#include "auth.h"
#include "audit_log.h"

#define INPUT_MAX 128

static void trimInput(char *text)
{
    size_t length;

    if (text == NULL) {
        return;
    }

    length = strlen(text);

    while (length > 0 &&
           (text[length - 1] == '\n' ||
            text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
}

static int readLine(const char *prompt, char *buffer, size_t size)
{
    int ch;
    size_t length;

    if (prompt != NULL) {
        printf("%s", prompt);
    }

    if (fgets(buffer, (int)size, stdin) == NULL) {
        return 0;
    }

    length = strlen(buffer);

    if (length > 0 && buffer[length - 1] != '\n') {
        ch = getchar();

        if (ch != '\n' && ch != EOF) {
            while (ch != '\n' && ch != EOF) {
                ch = getchar();
            }

            printf("Input is too long. Please try again.\n");
            return -1;
        }
    }

    trimInput(buffer);
    return 1;
}

static int readInteger(const char *prompt, int *value)
{
    char input[INPUT_MAX];
    char *end;
    long number;
    int result;

    result = readLine(prompt, input, sizeof(input));

    if (result <= 0) {
        return result;
    }

    errno = 0;
    number = strtol(input, &end, 10);

    if (input[0] == '\0' || errno != 0 ||
        *end != '\0' || number < -2147483647L - 1 ||
        number > 2147483647L) {
        printf("Invalid integer. Please try again.\n");
        return -1;
    }

    *value = (int)number;
    return 1;
}

static int readFloatValue(const char *prompt, float *value)
{
    char input[INPUT_MAX];
    char *end;
    float number;
    int result;

    result = readLine(prompt, input, sizeof(input));

    if (result <= 0) {
        return result;
    }

    errno = 0;
    number = strtof(input, &end);

    if (input[0] == '\0' || errno != 0 ||
        *end != '\0' || !isfinite(number)) {
        printf("Invalid number. Please try again.\n");
        return -1;
    }

    *value = number;
    return 1;
}

static int readStationName(char *name, size_t size)
{
    int result = readLine("Station name: ", name, size);

    if (result <= 0) {
        return result;
    }

    if (name[0] == '\0' || strchr(name, ',') != NULL) {
        printf("Name cannot be empty or contain a comma.\n");
        return -1;
    }

    return 1;
}

static int readStationDetails(Station *station, int includeId)
{
    int result;

    if (includeId) {
        result = readInteger("Station ID: ", &station->id);
        if (result <= 0) {
            return result;
        }
    }

    result = readStationName(station->name, sizeof(station->name));
    if (result <= 0) {
        return result;
    }

    result = readFloatValue("Distance: ", &station->distance);
    if (result <= 0) {
        return result;
    }

    result = readFloatValue("Fare: ", &station->fare);
    if (result <= 0) {
        return result;
    }

    result = readInteger("Platform number: ", &station->platform);
    if (result <= 0) {
        return result;
    }

    if (station->id <= 0 || station->distance < 0.0f ||
        station->fare < 0.0f || station->platform <= 0) {
        printf("ID and platform must be positive; distance and fare cannot be negative.\n");
        return -1;
    }

    return 1;
}

static void logEvent(const char *username,
                     const char *action,
                     const char *details,
                     AppStatus result)
{
    if (writeAuditLog(username, action, details, result) != APP_SUCCESS) {
        fprintf(stderr, "Warning: could not write to the audit log.\n");
    }
}

static void showStationSummary(const StationNode *root)
{
    int id;

    printf("\nTotal stations: %d\n", countStations(root));

    if (findMinimumStationId(root, &id)) {
        printf("Minimum station ID: %d\n", id);
    } else {
        printf("Minimum station ID: unavailable (tree is empty)\n");
    }

    if (findMaximumStationId(root, &id)) {
        printf("Maximum station ID: %d\n", id);
    } else {
        printf("Maximum station ID: unavailable (tree is empty)\n");
    }
}

static void viewStation(const StationNode *root)
{
    int id;
    int result;
    StationNode *node;

    result = readInteger("Enter station ID to search: ", &id);

    if (result == 0) {
        return;
    }

    if (result < 0) {
        return;
    }

    node = searchStation((StationNode *)root, id);

    if (node == NULL) {
        printf("Station ID %d was not found.\n", id);
        return;
    }

    displayStation(&node->station);
}

static void addStation(StationNode **root, const User *user)
{
    Station station;
    AppStatus status;
    int result;

    if (!hasPermission(user->role, "ADD")) {
        printf("Permission denied: Admin access is required.\n");
        logEvent(user->username, "ADD", "Permission denied",
                 APP_PERMISSION_DENIED);
        return;
    }

    memset(&station, 0, sizeof(station));

    result = readStationDetails(&station, 1);
    if (result <= 0) {
        return;
    }

    status = insertStation(root, station);

    if (status != APP_SUCCESS) {
        printf(status == APP_DUPLICATE
               ? "A station with that ID already exists.\n"
               : "Could not add station (invalid data or memory error).\n");

        logEvent(user->username, "ADD", "Station insertion failed", status);
        return;
    }

    status = saveStationsToCSV(*root, STATIONS_FILE);

    if (status != APP_SUCCESS) {
        printf("Warning: station added in memory, but saving to CSV failed.\n");
        logEvent(user->username, "ADD", "CSV save failed", status);
        return;
    }

    printf("Station added and saved successfully.\n");
    logEvent(user->username, "ADD", "Station added successfully",
             APP_SUCCESS);
}

static void updateStationDetails(StationNode **root, const User *user)
{
    int id;
    int result;
    StationNode *node;
    Station updated;
    AppStatus status;
    char details[INPUT_MAX];

    if (!hasPermission(user->role, "UPDATE")) {
        printf("Permission denied: Admin access is required.\n");
        logEvent(user->username, "UPDATE", "Permission denied",
                 APP_PERMISSION_DENIED);
        return;
    }

    result = readInteger("Enter station ID to update: ", &id);
    if (result <= 0) {
        return;
    }

    node = searchStation(*root, id);

    if (node == NULL) {
        printf("Station not found.\n");
        logEvent(user->username, "UPDATE", "Station not found",
                 APP_NOT_FOUND);
        return;
    }

    updated = node->station;

    printf("Enter the new details for station ID %d.\n", id);

    result = readStationDetails(&updated, 0);
    if (result <= 0) {
        return;
    }

    status = updateStation(*root, id, updated);

    if (status != APP_SUCCESS) {
        printf("Could not update station.\n");
        logEvent(user->username, "UPDATE", "Update failed", status);
        return;
    }

    status = saveStationsToCSV(*root, STATIONS_FILE);

    if (status != APP_SUCCESS) {
        printf("Warning: station updated in memory, but saving to CSV failed.\n");
        logEvent(user->username, "UPDATE", "CSV save failed", status);
        return;
    }

    snprintf(details, sizeof(details), "Station ID %d updated", id);
    printf("Station updated and saved successfully.\n");
    logEvent(user->username, "UPDATE", details, APP_SUCCESS);
}

static void deleteStationById(StationNode **root, const User *user)
{
    int id;
    int result;
    AppStatus status;
    char details[INPUT_MAX];

    if (!hasPermission(user->role, "DELETE")) {
        printf("Permission denied: Admin access is required.\n");
        logEvent(user->username, "DELETE", "Permission denied",
                 APP_PERMISSION_DENIED);
        return;
    }

    result = readInteger("Enter station ID to delete: ", &id);
    if (result <= 0) {
        return;
    }

    status = deleteStation(root, id);

    if (status != APP_SUCCESS) {
        printf("Station not found or could not be deleted.\n");
        logEvent(user->username, "DELETE", "Deletion failed", status);
        return;
    }

    status = saveStationsToCSV(*root, STATIONS_FILE);

    if (status != APP_SUCCESS) {
        printf("Warning: station deleted in memory, but saving to CSV failed.\n");
        logEvent(user->username, "DELETE", "CSV save failed", status);
        return;
    }

    snprintf(details, sizeof(details), "Station ID %d deleted", id);
    printf("Station deleted and changes saved successfully.\n");
    logEvent(user->username, "DELETE", details, APP_SUCCESS);
}

static void showMenu(UserRole role)
{
    printf("\n========== METRO MANAGEMENT SYSTEM ==========\n");
    printf("1. Display all stations\n");
    printf("2. Search station by ID\n");
    printf("3. Station summary\n");

    if (role == ROLE_ADMIN) {
        printf("4. Add station\n");
        printf("5. Update station\n");
        printf("6. Delete station\n");
    }

    printf("7. Logout\n");
    printf("0. Exit application\n");
    printf("=============================================\n");
}

static int runUserSession(StationNode **root, const User *user)
{
    int choice;
    int result;

    for (;;) {
        showMenu(user->role);

        result = readInteger("Choose an option: ", &choice);

        if (result == 0) {
            return 0;
        }

        if (result < 0) {
            continue;
        }

        switch (choice) {
            case 1:
                displayStationsInOrder(*root);
                break;

            case 2:
                viewStation(*root);
                break;

            case 3:
                showStationSummary(*root);
                break;

            case 4:
                if (user->role == ROLE_ADMIN) {
                    addStation(root, user);
                } else {
                    printf("Permission denied.\n");
                    logEvent(user->username, "ADD",
                             "Permission denied", APP_PERMISSION_DENIED);
                }
                break;

            case 5:
                if (user->role == ROLE_ADMIN) {
                    updateStationDetails(root, user);
                } else {
                    printf("Permission denied.\n");
                    logEvent(user->username, "UPDATE",
                             "Permission denied", APP_PERMISSION_DENIED);
                }
                break;

            case 6:
                if (user->role == ROLE_ADMIN) {
                    deleteStationById(root, user);
                } else {
                    printf("Permission denied.\n");
                    logEvent(user->username, "DELETE",
                             "Permission denied", APP_PERMISSION_DENIED);
                }
                break;

            case 7:
                logEvent(user->username, "LOGOUT",
                         "User logged out", APP_SUCCESS);
                printf("Logged out successfully.\n");
                return 1;

            case 0:
                logEvent(user->username, "EXIT",
                         "Application exit requested", APP_SUCCESS);
                return 0;

            default:
                printf("Invalid option. Please choose again.\n");
                break;
        }
    }
}

int main(void)
{
    StationNode *root = NULL;
    AppStatus status;
    int running = 1;

    printf("=============================================\n");
    printf("          METRO MANAGEMENT SYSTEM\n");
    printf("=============================================\n");

    status = loadStationsFromCSV(&root, STATIONS_FILE);

    if (status != APP_SUCCESS) {
        fprintf(stderr,
                "Error: station data could not be loaded completely.\n"
                "Check data/stations.csv before restarting.\n");
        freeStationTree(root);
        return EXIT_FAILURE;
    }

    printf("Loaded %d stations.\n", countStations(root));

    while (running) {
        char username[USERNAME_MAX];
        char password[PASSWORD_MAX];
        int result;
        User user = {0};

        printf("\n--------------- LOGIN ---------------\n");

        result = readLine("Username: ", username, sizeof(username));

        if (result == 0) {
            break;
        }

        if (result < 0) {
            continue;
        }

        result = readLine("Password: ", password, sizeof(password));

        if (result == 0) {
            break;
        }

        if (result < 0) {
            continue;
        }

        status = authenticateUser(username, password, &user);

        if (status != APP_SUCCESS) {
            printf("Login failed. Check your credentials.\n");
            logEvent(username, "LOGIN", "Login failed", status);
            continue;
        }

        logEvent(user.username, "LOGIN", "Login successful", APP_SUCCESS);

        printf("\nWelcome, %s!\n",
               user.username);
        printf("Role: %s\n",
               user.role == ROLE_ADMIN ? "Admin" : "Viewer");

        running = runUserSession(&root, &user);

        if (running) {
            printf("You can now sign in with another account.\n");
        }
    }

    freeStationTree(root);

    printf("\nMetro Management System closed.\n");
    return EXIT_SUCCESS;
}
