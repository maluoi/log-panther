#include "platform.h"

#ifdef PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

///////////////////////////////////////////
// Thread management

struct platform_thread_data_t {
    HANDLE handle;
    platform_thread_func_t func;
    void* arg;
};

static DWORD WINAPI thread_wrapper(LPVOID param) {
    platform_thread_data_t* data = (platform_thread_data_t*)param;
    int result = data->func(data->arg);
    return (DWORD)result;
}

platform_thread_t platform_thread_create(platform_thread_func_t func, void* arg) {
    platform_thread_data_t* data = (platform_thread_data_t*)malloc(sizeof(platform_thread_data_t));
    data->func = func;
    data->arg = arg;

    DWORD thread_id;
    data->handle = CreateThread(
        NULL,
        0,
        thread_wrapper,
        data,
        0,
        &thread_id
    );

    if (data->handle == nullptr) {
        free(data);
        return nullptr;
    }

    return data;
}

void platform_thread_join(platform_thread_t thread) {
    if (thread == nullptr) return;

    platform_thread_data_t* data = (platform_thread_data_t*)thread;
    WaitForSingleObject(data->handle, INFINITE);
    CloseHandle(data->handle);
    free(data);
}

void platform_sleep_ms(int milliseconds) {
    Sleep(milliseconds);
}

///////////////////////////////////////////
// Mutex/Synchronization

platform_mutex_t platform_mutex_create() {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(cs);
    return cs;
}

void platform_mutex_lock(platform_mutex_t mutex) {
    if (mutex == nullptr) return;
    EnterCriticalSection((CRITICAL_SECTION*)mutex);
}

void platform_mutex_unlock(platform_mutex_t mutex) {
    if (mutex == nullptr) return;
    LeaveCriticalSection((CRITICAL_SECTION*)mutex);
}

void platform_mutex_destroy(platform_mutex_t mutex) {
    if (mutex == nullptr) return;
    DeleteCriticalSection((CRITICAL_SECTION*)mutex);
    free((CRITICAL_SECTION*)mutex);
}

///////////////////////////////////////////
// Process management

struct platform_process_data_t {
    PROCESS_INFORMATION proc_info;
    HANDLE stdout_read;
};

platform_process_result_t platform_process_start(const char* command) {
    platform_process_result_t result = {};

    platform_process_data_t* proc_data = (platform_process_data_t*)malloc(sizeof(platform_process_data_t));
    memset(proc_data, 0, sizeof(platform_process_data_t));

    HANDLE stdout_write = nullptr;
    SECURITY_ATTRIBUTES saAttr = {};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    if (!CreatePipe(&proc_data->stdout_read, &stdout_write, &saAttr, 0)) {
        free(proc_data);
        result.success = false;
        return result;
    }

    STARTUPINFO start_info = {};
    start_info.cb = sizeof(STARTUPINFO);
    start_info.hStdOutput = stdout_write;
    start_info.hStdError = stdout_write;
    start_info.dwFlags |= STARTF_USESTDHANDLES;

    // CreateProcess requires non-const command string
    char* cmd_copy = _strdup(command);

    BOOL success = CreateProcess(
        NULL,
        cmd_copy,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &start_info,
        &proc_data->proc_info
    );

    free(cmd_copy);
    CloseHandle(stdout_write);

    if (!success) {
        CloseHandle(proc_data->stdout_read);
        free(proc_data);
        result.success = false;
        return result;
    }

    result.process = proc_data;
    result.stdout_pipe = proc_data->stdout_read;
    result.success = true;
    return result;
}

bool platform_process_is_running(platform_process_t process) {
    if (process == nullptr) return false;

    platform_process_data_t* proc_data = (platform_process_data_t*)process;
    DWORD exit_code;
    if (GetExitCodeProcess(proc_data->proc_info.hProcess, &exit_code)) {
        return exit_code == STILL_ACTIVE;
    }
    return false;
}

void platform_process_terminate(platform_process_t process) {
    if (process == nullptr) return;

    platform_process_data_t* proc_data = (platform_process_data_t*)process;
    TerminateProcess(proc_data->proc_info.hProcess, 1);
}

void platform_process_cleanup(platform_process_t process) {
    if (process == nullptr) return;

    platform_process_data_t* proc_data = (platform_process_data_t*)process;
    CloseHandle(proc_data->proc_info.hProcess);
    CloseHandle(proc_data->proc_info.hThread);
    CloseHandle(proc_data->stdout_read);
    free(proc_data);
}

///////////////////////////////////////////
// Pipe/IPC

int32_t platform_pipe_peek(platform_pipe_t pipe) {
    if (pipe == nullptr) return -1;

    HANDLE h = (HANDLE)pipe;
    char buffer[1];
    DWORD available = 0;
    DWORD read = 0;
    DWORD left = 0;

    BOOL result = PeekNamedPipe(h, buffer, sizeof(buffer), &read, &available, &left);
    if (!result) return -1;

    return (int32_t)available;
}

int32_t platform_pipe_read(platform_pipe_t pipe, char* buffer, int32_t buffer_size) {
    if (pipe == nullptr) return -1;

    HANDLE h = (HANDLE)pipe;
    DWORD read = 0;

    if (!ReadFile(h, buffer, buffer_size, &read, NULL)) {
        return -1;
    }

    return (int32_t)read;
}

void platform_pipe_close(platform_pipe_t pipe) {
    if (pipe == nullptr) return;
    CloseHandle((HANDLE)pipe);
}

///////////////////////////////////////////
// File dialogs

bool platform_file_dialog_save(char* filename_buffer, int32_t buffer_size, const char* title) {
    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(glfwGetCurrentContext());
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.lpstrFile = filename_buffer;
    ofn.nMaxFile = buffer_size;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_DONTADDTORECENT | OFN_OVERWRITEPROMPT;

    filename_buffer[0] = '\0';
    return GetSaveFileNameA(&ofn) != 0;
}

///////////////////////////////////////////
// Working directory

void platform_set_working_dir_to_exe() {
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    // Find the last backslash and terminate there
    char* last_slash = strrchr(path, '\\');
    if (last_slash != nullptr) {
        *last_slash = '\0';
        SetCurrentDirectoryA(path);
    }
}

#endif // PLATFORM_WINDOWS
