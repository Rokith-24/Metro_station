
#include <stdio.h>
#include <assert.h>
#include "audit_log.h"

int main(void)
{
    AppStatus status;

    status = writeAuditLog(
        "admin",
        "LOGIN",
        "Authentication successful",
        APP_SUCCESS
    );

    assert(status == APP_SUCCESS);

    status = writeAuditLog(
        "viewer",
        "DELETE",
        "Permission denied",
        APP_PERMISSION_DENIED
    );

    assert(status == APP_SUCCESS);

    printf("Audit log write test passed!\n");
    printf("Two audit entries written successfully.\n");

    return 0;
}
