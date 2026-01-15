#include "device_finder.h"

#include <stdio.h>
#include <string.h>

///////////////////////////////////////////

int device_finder_thread(void* arg);

///////////////////////////////////////////

bool device_finder_start  (device_finder_t *out_finder) {
    if (out_finder->state == device_finder_state_searching)
        return true;
    out_finder->state = device_finder_state_searching;

	out_finder->thread = platform_thread_create(device_finder_thread, out_finder);

	if (out_finder->thread == nullptr) {
		printf("Failed to create device finder thread\n");
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

int device_finder_thread(void* arg) {
	device_finder_t *thread = (device_finder_t*)arg;
	char buffer     [4096+1];
	char line_buffer[4096+1];
	int32_t line_buffer_pos = 0;

	platform_process_result_t proc = platform_process_start("adb devices -l");
	if (!proc.success) {
        thread->state = device_finder_state_error;
        return -1;
	}

    thread->devices.clear();

    while (true) {
		// Check if data is available
		int32_t available = platform_pipe_peek(proc.stdout_pipe);
		if (available <= 0) {
			// No data available - check if process is done
			if (!platform_process_is_running(proc.process)) {
				break; // Process done AND no more data
			}
			platform_sleep_ms(1);
			continue;
		}

		// Read the data from stdout, and add it to the logs.
		int32_t read = platform_pipe_read(proc.stdout_pipe, buffer, 4096);
		if (read <= 0) {
            platform_sleep_ms(1);
            continue;
        }

		buffer[read] = '\0';
        for (int i = 0; i < read; ++i) {
			if (buffer[i] == '\n' || buffer[i] == '\r' || line_buffer_pos == 4096) {
                if (line_buffer_pos == 0) continue;
				line_buffer[line_buffer_pos] = '\0';
				line_buffer_pos = 0;

                if (string_startswith(line_buffer, "List")) continue;
                if (string_startswith(line_buffer, "*")) continue;

                device_info_t info = {};
                if (sscanf(line_buffer, "%s", info.id) == 0) {
                    thread->state = device_finder_state_error;
                    return -1;
                }

                char    id     [64];
                char    product[64];
                char    model  [64];
                char    device [64];
                int32_t transport_id;
                sscanf(line_buffer, "%s device product:%s model:%s device:%s transport_id:%d", id, product, info.model, device, &transport_id);

                thread->devices.add(info);
			} else {
				line_buffer[line_buffer_pos++] = buffer[i];
			}
		}
	}

    platform_process_cleanup(proc.process);

    thread->state = device_finder_state_finished;
    return 1;
}