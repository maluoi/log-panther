#pragma once

// Platform abstraction layer for cross-platform support
// Handles threading, process management, synchronization, and file dialogs

#include <stdint.h>

///////////////////////////////////////////
// Platform detection

#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
#elif defined(__linux__)
    #define PLATFORM_LINUX
#else
    #error "Unsupported platform"
#endif

///////////////////////////////////////////
// Opaque handle types

typedef void* platform_thread_t;
typedef void* platform_mutex_t;
typedef void* platform_process_t;
typedef void* platform_pipe_t;

///////////////////////////////////////////
// Thread management

// Thread function signature: returns int, takes void* parameter
typedef int (*platform_thread_func_t)(void*);

// Create a new thread
platform_thread_t platform_thread_create(platform_thread_func_t func, void* arg);

// Wait for thread to finish and clean up
void platform_thread_join(platform_thread_t thread);

// Sleep for specified milliseconds
void platform_sleep_ms(int milliseconds);

///////////////////////////////////////////
// Mutex/Synchronization

// Create a mutex
platform_mutex_t platform_mutex_create();

// Lock a mutex
void platform_mutex_lock(platform_mutex_t mutex);

// Unlock a mutex
void platform_mutex_unlock(platform_mutex_t mutex);

// Destroy a mutex
void platform_mutex_destroy(platform_mutex_t mutex);

///////////////////////////////////////////
// Process management

struct platform_process_result_t {
    platform_process_t process;
    platform_pipe_t    stdout_pipe;
    bool               success;
};

// Start a process and capture its stdout
platform_process_result_t platform_process_start(const char* command);

// Check if process is still running
bool platform_process_is_running(platform_process_t process);

// Terminate a process
void platform_process_terminate(platform_process_t process);

// Clean up process resources (call after process has ended)
void platform_process_cleanup(platform_process_t process);

///////////////////////////////////////////
// Pipe/IPC

// Read from a pipe (non-blocking check available data first)
// Returns number of bytes read, 0 if no data available, -1 on error
int32_t platform_pipe_read(platform_pipe_t pipe, char* buffer, int32_t buffer_size);

// Check how many bytes are available to read without blocking
int32_t platform_pipe_peek(platform_pipe_t pipe);

// Close a pipe
void platform_pipe_close(platform_pipe_t pipe);

///////////////////////////////////////////
// File dialogs

// Open a save file dialog. Returns true if user selected a file.
// filename_buffer should be at least 512 bytes
bool platform_file_dialog_save(char* filename_buffer, int32_t buffer_size, const char* title);

///////////////////////////////////////////
// Working directory

// Set the current working directory to the executable's directory
void platform_set_working_dir_to_exe();
