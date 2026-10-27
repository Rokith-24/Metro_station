
#ifndef AUDIT_LOG_H
#define AUDIT_LOG_H

#include "common.h"

AppStatus writeAuditLog(const char *username,
                        const char *action,
                        const char *details,
                        AppStatus result);

#endif
