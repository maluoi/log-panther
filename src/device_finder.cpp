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
	char buffer     [4096+1];
	char line_buffer[4096+1];
	int32_t line_buffer_pos = 0;

    HANDLE stdout_read  = {};
    HANDLE stdout_write = {};
	SECURITY_ATTRIBUTES saAttr = {};
	saAttr.nLength        = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = true;
	if (!CreatePipe(&stdout_read, &stdout_write, &saAttr, 0)) {
        thread->state = device_finder_state_error;
        return -1;
	}

    PROCESS_INFORMATION proc_info = {};
    STARTUPINFO start_info = {};
	start_info.cb         = sizeof(STARTUPINFO);
	start_info.hStdOutput = stdout_write;
	start_info.dwFlags   |= STARTF_USESTDHANDLES;
	if (!CreateProcess(NULL,
					   (LPSTR)"adb devices -l", // Command line
					   NULL, // Process handle not inheritable
					   NULL, // Thread handle not inheritable
					   TRUE, // Set handle inheritance to TRUE
					   CREATE_NO_WINDOW,    // No creation flags
					   NULL, // Use parent's environment block
					   NULL, // Use parent's starting directory
					   &start_info, // Pointer to STARTUPINFO structure
					   &proc_info) // Pointer to PROCESS_INFORMATION structure
	) {
        thread->state = device_finder_state_error;
        return -1;
	}
	CloseHandle(stdout_write);

    thread->devices.clear();

    bool run = true;
    while (run) {
		// Make sure the process is still running
		DWORD exit_code;
		if (GetExitCodeProcess(proc_info.hProcess, &exit_code)) {
			if (exit_code != STILL_ACTIVE)
				break;
		}

		// Read the data from stdout, and add it to the logs.
		DWORD read;
		if (!ReadFile(stdout_read, buffer, 4096, &read, NULL) || read == 0) {
            Sleep(1);
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

    CloseHandle(stdout_read);
    CloseHandle(proc_info.hProcess);
    CloseHandle(proc_info.hThread);
    
    thread->state = device_finder_state_finished;
    return 1;
}