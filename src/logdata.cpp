
#include "logdata.h"

#include <stdio.h>

///////////////////////////////////////////

DWORD __stdcall logcat_thread    (void* arg);
logcat_line_t   logcat_parse_line(char *line_buffer, char *out_tag);

///////////////////////////////////////////

void logcat_create (logcat_data_t *out_data) {
	*out_data = {};
	InitializeCriticalSection(&out_data->lines_section);
}

///////////////////////////////////////////

void logcat_destroy(logcat_data_t *ref_data) {
	for (int i = 0; i < ref_data->lines.count; ++i)
		free(ref_data->lines[i].line);
	for (int i = 0; i < ref_data->tags.count; ++i)
		free(ref_data->tags[i]);
	ref_data->lines.free();
	ref_data->tags .free();
	DeleteCriticalSection(&ref_data->lines_section);

	*ref_data = {};
}

///////////////////////////////////////////

int32_t logcat_thread_start(const char *device_id, logcat_thread_t *out_thread, logcat_data_t *out_data){
	*out_thread = {};
	*out_data   = {};
	logcat_create(out_data);
	out_thread->data = out_data;
	out_thread->run = true;
	strncpy(out_data->src_id, device_id, sizeof(out_data->src_id));

	HANDLE stdout_write = {};
	SECURITY_ATTRIBUTES saAttr = {};
	saAttr.nLength        = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = true;
	if (!CreatePipe(&out_thread->stdout_read, &stdout_write, &saAttr, 0)) {
		printf("Stdout CreatePipe");
		return -1;
	}

	char command[1024];
	if (device_id == nullptr) {
		snprintf(command, 1024, "adb logcat -T 0");
	} else {
		snprintf(command, 1024, "adb -s %s logcat -T 0", device_id);
	}

	STARTUPINFO start_info = {};
	start_info.cb = sizeof(STARTUPINFO);
	start_info.hStdOutput = stdout_write;
	start_info.dwFlags |= STARTF_USESTDHANDLES;
	if (!CreateProcess(NULL,
					   (LPSTR)command, // Command line
					   NULL, // Process handle not inheritable
					   NULL, // Thread handle not inheritable
					   TRUE, // Set handle inheritance to TRUE
					   0,    // No creation flags
					   NULL, // Use parent's environment block
					   NULL, // Use parent's starting directory
					   &start_info, // Pointer to STARTUPINFO structure
					   &out_thread->proc_info) // Pointer to PROCESS_INFORMATION structure
	) {
		printf("CreateProcess failed (%d).\n", GetLastError());
		return -2;
	}

	CloseHandle(stdout_write);

	// Create thread to read from the child process' standard output
	DWORD thread_id;
	out_thread->thread = CreateThread(
		NULL,          // default security attributes
		0,             // use default stack size
		logcat_thread, // thread function
		out_thread,    // argument to thread function
		0,             // use default creation flags
		&thread_id     // returns the thread identifier
	);

	if (out_thread->thread == nullptr) {
		printf("CreateThread failed (%d).\n", GetLastError());
		return -3;
	}

	return 1;
}

///////////////////////////////////////////

void logcat_thread_end(logcat_thread_t *ref_thread) {
	if (ref_thread->run == false) return;

	printf("Requesting termination\n");
	TerminateProcess(&ref_thread->proc_info, 1);

	ref_thread->run = false;
	WaitForSingleObject(ref_thread->thread, INFINITE);
	CloseHandle        (ref_thread->thread);

	CloseHandle(ref_thread->proc_info.hProcess);
	CloseHandle(ref_thread->proc_info.hThread);
	CloseHandle(ref_thread->stdout_read);
}

///////////////////////////////////////////

bool logcat_from_file(logcat_data_t *out_data, const char *filename) {
	FILE *fp = fopen(filename, "r");
	if (fp == nullptr) return false;

	// Parse the file one line at a time
	char line_buffer[4096];
	char tag_buffer [4096];
	while (fgets(line_buffer, 4096, fp) != nullptr) {
		logcat_line_t line_data = logcat_parse_line(line_buffer, tag_buffer);
		line_data.tag = logcat_get_tag(out_data, tag_buffer);
		out_data->lines.add(line_data);
	}
	return true;
}

///////////////////////////////////////////

bool logcat_to_file(const logcat_data_t *data, const char *filename) {
	FILE *fp = fopen(filename, "w");
	if (fp == nullptr) return false;

	for (int32_t i = 0; i < data->lines.count; i+=1) {
		const logcat_line_t &line = data->lines[i];

		if (line.severity == 0) fprintf(fp, "%s", line.line);
		else                    fprintf(fp, "%02d-%02d %02d:%02d:%02d.%03d %5d %5d %c %s: %s", line.month, line.day, line.hour, line.minute, line.second, line.millisecond, line.pid, line.tid, line.severity, data->tags[line.tag], line.line);
	}

	fclose(fp);
	return true;
}

///////////////////////////////////////////

uint16_t logcat_get_tag(logcat_data_t *data, char *tag) {
	for (size_t i = 0; i < data->tags.count; i++)
	{
		if (strcmp(data->tags[i], tag) == 0)
			return i;
	}
	
	char *new_tag = (char*)malloc(strlen(tag) + 1);
	strcpy(new_tag, tag);
	data->tags.add(new_tag);
	return data->tags.count - 1;
}

///////////////////////////////////////////

void logcat_clear(logcat_data_t *data) {
	EnterCriticalSection(&data->lines_section);
	for (int32_t i = 0; i < data->lines.count; i+=1) free(data->lines[i].line);
	for (int32_t i = 0; i < data->tags.count;  i+=1) free(data->tags [i]);
	data->lines.clear();
	data->tags .clear();
	LeaveCriticalSection(&data->lines_section);
}

///////////////////////////////////////////

DWORD __stdcall logcat_thread(void* arg) {
	printf("logcat thread started\n");

	logcat_thread_t *thread = (logcat_thread_t*)arg;
	char    buffer      [4096+1];
	char    line_buffer [4096+1];
	char    line_buffer2[4096+1];
	char    tag         [4096+1];
	int32_t line_buffer_pos = 0;

	while (thread->run) {
		// Make sure the process is still running
		DWORD exit_code;
		if (GetExitCodeProcess(thread->proc_info.hProcess, &exit_code)) {
			if (exit_code != STILL_ACTIVE)
				break;
		}

		// Check to see if the process has written anything to stdout, we want
		// to skip reading and keep the thread interactive if not.
		DWORD peekRead;
		DWORD peekAvailable;
		DWORD peekLeft;
		BOOL  result = PeekNamedPipe(
			thread->stdout_read, // Handle to the pipe
			buffer,              // Pointer to the buffer to receive data
			sizeof(buffer),      // Size of the buffer
			&peekRead,           // Pointer to the variable to receive the number of bytes read
			&peekAvailable,      // Pointer to the variable to receive the total number of bytes available to read
			&peekLeft // Pointer to the variable to receive the number of bytes remaining in this message
		);
		if (!result || peekAvailable == 0) {
			Sleep(1);
			continue;
		}

		// Read the data from stdout, and add it to the logs.
		DWORD read;
		bool  succeed = ReadFile(thread->stdout_read, buffer, 4096, &read, NULL);
		if (!succeed || read == 0) continue;

		buffer[read] = '\0';

		for (int i = 0; i < read; ++i) {
			if (buffer[i] == '\n' || line_buffer_pos == 4096) {
				line_buffer[line_buffer_pos] = '\0';
				line_buffer_pos = 0;

				logcat_line_t line_data = logcat_parse_line(line_buffer, tag);

				if (!thread->pause) {
					EnterCriticalSection(&thread->data->lines_section);
					line_data.tag = logcat_get_tag(thread->data, tag);
					thread->data->lines.add(line_data);
					LeaveCriticalSection(&thread->data->lines_section);
				}
			} else {
				line_buffer[line_buffer_pos++] = buffer[i];
			}
		}
	}
	thread->run = false;
	
	printf("logcat thread ended\n");

	return 0;
}

///////////////////////////////////////////

// line parsing extracted from logcat_from_file and logcat_thread
logcat_line_t logcat_parse_line(char *line_buffer, char *out_tag) {
	logcat_line_t result = {};

	if (line_buffer[0] >= '0' && line_buffer[0] <= '9') {
		int32_t pid, tid;
		int32_t m, d, h, min, s, ms;
		int32_t scanned = 0;
		sscanf(line_buffer, "%d-%d %d:%d:%d.%d %d %d %c %s %n", &m, &d, &h, &min, &s, &ms, &pid, &tid, &result.severity, out_tag, &scanned);
		result.month  = m;
		result.day    = d;
		result.hour   = h;
		result.minute = min;
		result.second = s;
		result.millisecond = ms;
		result.pid    = pid;
		result.tid    = tid;

		size_t tag_len = strlen(out_tag);
		while (tag_len > 0 && out_tag[tag_len - 1] == ':') tag_len--;
		out_tag[tag_len] = '\0';

		const char *copy = line_buffer + scanned;
		result.line = (char*)malloc(strlen(copy) + 1);
		strcpy(result.line, copy);
	} else {
		out_tag[0] = '\0';
		result.line = (char*)malloc(strlen(line_buffer) + 1);
		strcpy(result.line, line_buffer);
	}
	if (result.line == nullptr) {
		printf("err: %s\n", line_buffer);
		result.line = nullptr;
	}
	return result;
}