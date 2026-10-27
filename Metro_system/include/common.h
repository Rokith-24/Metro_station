
#ifndef COMMON_H
#define COMMON_H

#define STATION_NAME_MAX 50
#define USERNAME_MAX 32
#define PASSWORD_MAX 128
#define ROLE_MAX 16

#define STATIONS_FILE "data/stations.csv"
#define USERS_FILE    "data/users.csv"
#define AUDIT_FILE    "data/audit.log"

typedef enum {
    APP_SUCCESS = 0,
    APP_ERROR,
    APP_INVALID_INPUT,
    APP_NOT_FOUND,
    APP_DUPLICATE,
    APP_PERMISSION_DENIED
} AppStatus;

typedef enum {
    ROLE_VIEWER = 0,
    ROLE_ADMIN
} UserRole;

#endif
