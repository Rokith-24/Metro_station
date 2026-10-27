
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "auth.h"

int main(void)
{
    User user = {0};
    AppStatus status;

    /* Test valid Admin login. */
    status = authenticateUser("admin", "admin123", &user);
    assert(status == APP_SUCCESS);
    assert(user.role == ROLE_ADMIN);
    assert(strcmp(user.username, "admin") == 0);

    /* Test valid Viewer login. */
    status = authenticateUser("viewer", "viewer123", &user);
    assert(status == APP_SUCCESS);
    assert(user.role == ROLE_VIEWER);

    /* Test invalid password. */
    status = authenticateUser("admin", "wrongpass", &user);
    assert(status == APP_PERMISSION_DENIED);

    /* Test role permissions. */
    assert(hasPermission(ROLE_ADMIN, "ADD"));
    assert(hasPermission(ROLE_ADMIN, "DELETE"));
    assert(hasPermission(ROLE_VIEWER, "VIEW"));
    assert(hasPermission(ROLE_VIEWER, "SEARCH"));

    assert(!hasPermission(ROLE_VIEWER, "ADD"));
    assert(!hasPermission(ROLE_VIEWER, "UPDATE"));
    assert(!hasPermission(ROLE_VIEWER, "DELETE"));
    assert(!hasPermission(ROLE_ADMIN, "UNKNOWN"));

    printf("Admin login test passed!\n");
    printf("Viewer login test passed!\n");
    printf("Invalid password test passed!\n");
    printf("Role permission tests passed!\n");

    return 0;
}
