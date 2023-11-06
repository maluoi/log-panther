#include "device_finder.h"

#include <stdio.h>

///////////////////////////////////////////

DWORD __stdcall device_finder_thread(void* arg);

///////////////////////////////////////////

bool device_finder_start  (device_finder_t *out_finder) {
    if (out_finder->state == device_finder_state_searching)
        return true;
    out_finder->state = device_finder_state_searching;

    // Create thread to read from the child process' standard output
	DWORD thread_id;
	out_finder->thread = CreateThread(
		NULL,          // default security attributes
		0,             // use default stack size
		device_finder_thread, // thread function
		out_finder,    // argument to thread function
		0,             // use default creation flags
		&thread_id     // returns the thread identifier
	);

	if (out_finder->thread == nullptr) {
		printf("CreateThread failed (%d).\n", GetLastError());
        out_finder->state = device_finder_state_error;
		return false;
	}

    return true;
}

///////////////////////////////////////////

void device_finder_destroy(device_finder_t *ref_finder) {
    ref_finder->devices.free();
    *ref_finder = {};
}

///////////////////////////////////////////

bool string_startswith(const char *a, const char *is) {
	while (*is != '\0') {
		if (*a == '\0' || *is != *a)
			return false;
		a++;
		is++;
	}
	return true;
}

///////////////////////////////////////////

DWORD __stdcall device_finder_thread(void* arg) {
	device_finder_t *thread = (device_finder_t*)arg;
	char buffer [4096];

    FILE *pipe;

    // Open a pipe to the command
    pipe = _popen("adb devices -l", "r");
    if (pipe == nullptr) {
        thread->state = device_finder_state_error;
        return 0;
    }

    thread->devices.clear();
    // Read the output a line at a time - output it.
    while (fgets(buffer, sizeof(buffer), pipe) && !ferror( pipe ) && !feof( pipe ) ) {
        if (strcmp(buffer, "\n") == 0) break;
        if (string_startswith(buffer, "List")) continue;
        if (string_startswith(buffer, "*")) continue;

        device_info_t info = {};
        
        if (sscanf(buffer, "%s", info.id) == 0) {
            thread->state = device_finder_state_error;
            return 0;
        }

        char    id     [64];
        char    product[64];
        char    model  [64];
        char    device [64];
        int32_t transport_id;
        sscanf(buffer, "%s device product:%s model:%s device:%s transport_id:%d", id, product, info.model, device, &transport_id);

        thread->devices.add(info);
    }

    // Close the pipe
    if (_pclose(pipe) == -1) {
        thread->state = device_finder_state_error;
        return 0;
    }

    thread->state = device_finder_state_finished;
    return 1;
}