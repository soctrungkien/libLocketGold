#ifndef LOG_UTIL_H
#define LOG_UTIL_H

#include <android/log.h>

#define TAG "LocketGold"

#define LOG_D(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOG_I(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOG_W(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOG_E(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

void log_startup(const char* msg);
void log_error(const char* msg);

#endif // LOG_UTIL_H

