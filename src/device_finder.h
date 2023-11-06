#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "array.h"

///////////////////////////////////////////

enum device_finder_state_ {
    device_finder_state_none,
    device_finder_state_error,
    device_finder_state_searching,
    device_finder_state_finished
};

struct device_info_t {
    char id[64];
    char model[64];
};

struct device_finder_t {
    device_finder_state_   state;
    array_t<device_info_t> devices;
	HANDLE                 thread;
};

bool device_finder_start  (device_finder_t *out_finder);
void device_finder_destroy(device_finder_t *ref_finder);