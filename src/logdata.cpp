
#include "logdata.h"

#include <stdio.h>
#include <string.h>

///////////////////////////////////////////

int           logcat_thread    (void* arg);
logcat_line_t logcat_parse_line(char *line_buffer, char *out_tag);

///////////////////////////////////////////

void logcat_create (logcat_data_t *out_data) {
	*out_data = {};
	out_data->lines_mutex = platform_mutex_create();
}

///////////////////////////////////////////

void logcat_destroy(logcat_data_t *ref_data) {
	for (int i = 0; i < ref_data->lines.count; ++i)
		free(ref_data->lines[i].line);
	for (int i = 0; i < ref_data->tags.count; ++i)
		free(ref_data->tags[i]);
	ref_data->lines.free();
	ref_data->tags .free();
	platform_mutex_destroy(ref_data->lines_mutex);

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

	char command[1024];
	if (device_id == nullptr) {
		snprintf(command, 1024, "adb logcat -T 1");
	} else {
		snprintf(command, 1024, "adb -s %s logcat -T 1", device_id);
	}

	platform_process_result_t proc = platform_process_start(command);
	if (!proc.success) {
		printf("Failed to start logcat process\n");
		return -1;
	}

	out_thread->process = proc.process;
	out_thread->stdout_pipe = proc.stdout_pipe;

	// Create thread to read from the child process' standard output
	out_thread->thread = platform_thread_create(logcat_thread, out_thread);

	if (out_thread->thread == nullptr) {
		printf("Failed to create logcat thread\n");
		platform_process_cleanup(proc.process);
		return -2;
	}

	return 1;
}

///////////////////////////////////////////

void logcat_thread_end(logcat_thread_t *ref_thread) {
	if (ref_thread->run == false) return;

	platform_process_terminate(ref_thread->process);

	ref_thread->run = false;
	platform_thread_join(ref_thread->thread);

	platform_process_cleanup(ref_thread->process);
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
	platform_mutex_lock(data->lines_mutex);
	for (int32_t i = 0; i < data->lines.count; i+=1) free(data->lines[i].line);
	for (int32_t i = 0; i < data->tags.count;  i+=1) free(data->tags [i]);
	data->lines.clear();
	data->tags .clear();
	platform_mutex_unlock(data->lines_mutex);
}

///////////////////////////////////////////

int logcat_thread(void* arg) {
	logcat_thread_t *thread = (logcat_thread_t*)arg;
	char    buffer      [4096+1];
	char    line_buffer [4096+1];
	char    line_buffer2[4096+1];
	char    tag         [4096+1];
	int32_t line_buffer_pos = 0;

	while (thread->run) {
		// Make sure the process is still running
		if (!platform_process_is_running(thread->process)) {
			break;
		}

		// Check if data is available
		int32_t available = platform_pipe_peek(thread->stdout_pipe);
		if (available <= 0) {
			platform_sleep_ms(1);
			continue;
		}

		// Read the data from stdout, and add it to the logs.
		int32_t read = platform_pipe_read(thread->stdout_pipe, buffer, 4096);
		if (read <= 0) continue;

		buffer[read] = '\0';

		for (int i = 0; i < read; ++i) {
			if (buffer[i] == '\n' || line_buffer_pos == 4096) {
				line_buffer[line_buffer_pos] = '\0';
				line_buffer_pos = 0;

				logcat_line_t line_data = logcat_parse_line(line_buffer, tag);

				if (!thread->pause) {
					platform_mutex_lock(thread->data->lines_mutex);
					line_data.tag = logcat_get_tag(thread->data, tag);
					thread->data->lines.add(line_data);
					platform_mutex_unlock(thread->data->lines_mutex);
				}
			} else {
				line_buffer[line_buffer_pos++] = buffer[i];
			}
		}
	}
	thread->run = false;

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