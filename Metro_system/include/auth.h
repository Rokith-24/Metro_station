
#ifndef AUTH_H
#define AUTH_H

#include "common.h"

typedef struct {
    char username[USERNAME_MAX];
    UserRole role;
} User;

AppStatus authenticateUser(
    const char *username,
    const char *password,
    User *authenticatedUser
);

int hasPermission(UserRole role, const char *action);

#endif
