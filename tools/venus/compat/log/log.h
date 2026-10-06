/* AOSP's <log/log.h>, as much of it as virglrenderer's Mesa util code uses, on the NDK's liblog. */
#pragma once
#include <android/log.h>
#define LOG_PRI(prio, tag, ...) __android_log_print(prio, tag, __VA_ARGS__)
