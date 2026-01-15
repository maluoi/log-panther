#pragma once

#include "array.h"
#include "platform.h"

///////////////////////////////////////////

enum app_finder_state_ {
    app_finder_state_none,
    app_finder_state_error,
    app_finder_state_searching,
    app_finder_state_finished
};

struct app_info_t {
    char package[256];
};

struct app_finder_t {
    app_finder_state_    state;
    array_t<app_info_t>  apps;
    platform_thread_t    thread;
    char                 device_id[64];
};

bool app_finder_start  (app_finder_t *out_finder, const char *device_id);
void app_finder_destroy(app_finder_t *ref_finder);

///////////////////////////////////////////

enum app_launcher_state_ {
    app_launcher_state_none,
    app_launcher_state_launching,
    app_launcher_state_polling_pid,
    app_launcher_state_finished,
    app_launcher_state_error
};

struct app_launcher_t {
    app_launcher_state_ state;
    platform_thread_t   thread;
    char                device_id[64];
    char                package[256];
    uint16_t            pid;
};

bool app_launcher_start(app_launcher_t *launcher, const char *device_id, const char *package);
