#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "array.h"

///////////////////////////////////////////

struct logcat_line_t {
	uint8_t  month;
	uint8_t  day;
	uint8_t  hour;
	uint8_t  minute;
	uint8_t  second;
	uint8_t  severity;
    uint16_t millisecond;
	uint16_t pid;
	uint16_t tid;
	uint16_t tag;
	uint64_t time;
	char    *line;
};

struct logcat_data_t {
	int32_t                lines_last;
	array_t<logcat_line_t> lines;
	array_t<char *>        tags;
    CRITICAL_SECTION       lines_section;
	char                   src_id[64];
};

struct logcat_thread_t {
    logcat_data_t         *data;
	HANDLE                 stdout_read;
	HANDLE                 thread;
	PROCESS_INFORMATION    proc_info;
	bool                   run;
	bool                   pause;
};

void     logcat_create      (      logcat_data_t *out_data);
int32_t  logcat_thread_start(const char *opt_device_id, logcat_thread_t *out_thread, logcat_data_t *out_data);
void     logcat_thread_end  (      logcat_thread_t *ref_thread);
bool     logcat_from_file   (      logcat_data_t   *out_data, const char *filename);
bool     logcat_to_file     (const logcat_data_t   *data, const char *filename);
void     logcat_destroy     (      logcat_data_t   *ref_data);
bool     logcat_to_file     (const logcat_data_t   *data);
uint16_t logcat_get_tag     (      logcat_data_t   *data, char *tag);
void     logcat_clear       (      logcat_data_t   *ref_data);