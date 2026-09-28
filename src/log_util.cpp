#include "log_util.h"

void log_startup(const char* msg) {
    LOG_I("[STARTUP] %s", msg);
}

void log_error(const char* msg) {
    LOG_E("[ERROR] %s", msg);
}

