#include "app_finder.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

///////////////////////////////////////////

int app_finder_thread(void* arg);
int app_launcher_thread(void* arg);

///////////////////////////////////////////

bool app_finder_start(app_finder_t *out_finder, const char *device_id) {
    if (out_finder->state == app_finder_state_searching)
        return true;
    out_finder->state = app_finder_state_searching;
    strncpy(out_finder->device_id, device_id, sizeof(out_finder->device_id) - 1);
    out_finder->device_id[sizeof(out_finder->device_id) - 1] = '\0';

    out_finder->thread = platform_thread_create(app_finder_thread, out_finder);

    if (out_finder->thread == nullptr) {
        printf("Failed to create app finder thread\n");
        out_finder->state = app_finder_state_error;
        return false;
    }

    return true;
}

///////////////////////////////////////////

void app_finder_destroy(app_finder_t *ref_finder) {
    for (int32_t i = 0; i < ref_finder->apps.count; i++) {
        // Nothing to free, package is inline
    }
    ref_finder->apps.free();
    *ref_finder = {};
}

///////////////////////////////////////////

int app_finder_thread(void* arg) {
    app_finder_t *finder = (app_finder_t*)arg;
    char buffer     [4096+1];
    char line_buffer[4096+1];
    int32_t line_buffer_pos = 0;

    char command[256];
    snprintf(command, sizeof(command), "adb -s %s shell pm list packages", finder->device_id);

    platform_process_result_t proc = platform_process_start(command);
    if (!proc.success) {
        finder->state = app_finder_state_error;
        return -1;
    }

    finder->apps.clear();

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

        // Read the data from stdout
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

                // Lines look like: "package:com.example.app"
                if (strncmp(line_buffer, "package:", 8) == 0) {
                    app_info_t info = {};
                    strncpy(info.package, line_buffer + 8, sizeof(info.package) - 1);
                    info.package[sizeof(info.package) - 1] = '\0';
                    finder->apps.add(info);
                }
            } else {
                line_buffer[line_buffer_pos++] = buffer[i];
            }
        }
    }

    platform_process_cleanup(proc.process);

    // Sort packages alphabetically for easier browsing
    for (int32_t i = 0; i < finder->apps.count - 1; i++) {
        for (int32_t j = i + 1; j < finder->apps.count; j++) {
            if (strcmp(finder->apps[i].package, finder->apps[j].package) > 0) {
                app_info_t tmp = finder->apps[i];
                finder->apps[i] = finder->apps[j];
                finder->apps[j] = tmp;
            }
        }
    }

    finder->state = app_finder_state_finished;
    return 1;
}

///////////////////////////////////////////

bool app_launcher_start(app_launcher_t *launcher, const char *device_id, const char *package) {
    if (launcher->state == app_launcher_state_launching ||
        launcher->state == app_launcher_state_polling_pid)
        return false;

    launcher->state = app_launcher_state_launching;
    launcher->pid = 0;
    strncpy(launcher->device_id, device_id, sizeof(launcher->device_id) - 1);
    launcher->device_id[sizeof(launcher->device_id) - 1] = '\0';
    strncpy(launcher->package, package, sizeof(launcher->package) - 1);
    launcher->package[sizeof(launcher->package) - 1] = '\0';

    launcher->thread = platform_thread_create(app_launcher_thread, launcher);

    if (launcher->thread == nullptr) {
        printf("Failed to create app launcher thread\n");
        launcher->state = app_launcher_state_error;
        return false;
    }

    return true;
}

///////////////////////////////////////////

int app_launcher_thread(void* arg) {
    app_launcher_t *launcher = (app_launcher_t*)arg;
    char command[512];
    char buffer[256];

    // Launch the app using monkey (finds launcher activity automatically)
    snprintf(command, sizeof(command),
             "adb -s %s shell monkey -p %s -c android.intent.category.LAUNCHER 1",
             launcher->device_id, launcher->package);

    platform_process_result_t proc = platform_process_start(command);
    if (!proc.success) {
        launcher->state = app_launcher_state_error;
        return -1;
    }

    // Wait for monkey to finish
    while (platform_process_is_running(proc.process)) {
        platform_sleep_ms(10);
    }
    platform_process_cleanup(proc.process);

    // Now poll for PID
    launcher->state = app_launcher_state_polling_pid;

    snprintf(command, sizeof(command), "adb -s %s shell pidof %s",
             launcher->device_id, launcher->package);

    // Poll for up to 5 seconds
    for (int attempt = 0; attempt < 50; attempt++) {
        proc = platform_process_start(command);
        if (!proc.success) {
            platform_sleep_ms(100);
            continue;
        }

        // Wait for pidof to finish and read output
        while (true) {
            int32_t available = platform_pipe_peek(proc.stdout_pipe);
            if (available <= 0) {
                if (!platform_process_is_running(proc.process)) {
                    break;
                }
                platform_sleep_ms(1);
                continue;
            }

            int32_t read = platform_pipe_read(proc.stdout_pipe, buffer, sizeof(buffer) - 1);
            if (read > 0) {
                buffer[read] = '\0';
                // pidof returns space-separated PIDs if multiple, take the first
                int pid = 0;
                if (sscanf(buffer, "%d", &pid) == 1 && pid > 0) {
                    launcher->pid = (uint16_t)pid;
                    platform_process_cleanup(proc.process);
                    launcher->state = app_launcher_state_finished;
                    return 1;
                }
            }
        }

        platform_process_cleanup(proc.process);
        platform_sleep_ms(100);
    }

    // Timed out waiting for PID
    launcher->state = app_launcher_state_finished;
    return 1;
}
