#include "platform.h"

#ifdef PLATFORM_LINUX

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

///////////////////////////////////////////
// Thread management

struct platform_thread_data_t {
    pthread_t thread;
    platform_thread_func_t func;
    void* arg;
};

static void* thread_wrapper(void* param) {
    platform_thread_data_t* data = (platform_thread_data_t*)param;
    int result = data->func(data->arg);
    return (void*)(intptr_t)result;
}

platform_thread_t platform_thread_create(platform_thread_func_t func, void* arg) {
    platform_thread_data_t* data = (platform_thread_data_t*)malloc(sizeof(platform_thread_data_t));
    data->func = func;
    data->arg = arg;

    int result = pthread_create(&data->thread, nullptr, thread_wrapper, data);
    if (result != 0) {
        free(data);
        return nullptr;
    }

    return data;
}

void platform_thread_join(platform_thread_t thread) {
    if (thread == nullptr) return;

    platform_thread_data_t* data = (platform_thread_data_t*)thread;
    pthread_join(data->thread, nullptr);
    free(data);
}

void platform_sleep_ms(int milliseconds) {
    usleep(milliseconds * 1000);
}

///////////////////////////////////////////
// Mutex/Synchronization

platform_mutex_t platform_mutex_create() {
    pthread_mutex_t* mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(mutex, nullptr);
    return mutex;
}

void platform_mutex_lock(platform_mutex_t mutex) {
    if (mutex == nullptr) return;
    pthread_mutex_lock((pthread_mutex_t*)mutex);
}

void platform_mutex_unlock(platform_mutex_t mutex) {
    if (mutex == nullptr) return;
    pthread_mutex_unlock((pthread_mutex_t*)mutex);
}

void platform_mutex_destroy(platform_mutex_t mutex) {
    if (mutex == nullptr) return;
    pthread_mutex_destroy((pthread_mutex_t*)mutex);
    free((pthread_mutex_t*)mutex);
}

///////////////////////////////////////////
// Process management

struct platform_process_data_t {
    pid_t pid;
    int stdout_pipe_fd;
};

platform_process_result_t platform_process_start(const char* command) {
    platform_process_result_t result = {};

    platform_process_data_t* proc_data = (platform_process_data_t*)malloc(sizeof(platform_process_data_t));
    proc_data->pid = -1;
    proc_data->stdout_pipe_fd = -1;

    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        free(proc_data);
        result.success = false;
        return result;
    }

    pid_t pid = fork();
    if (pid == -1) {
        // Fork failed
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        free(proc_data);
        result.success = false;
        return result;
    }

    if (pid == 0) {
        // Child process
        close(pipe_fds[0]); // Close read end

        // Redirect stdout and stderr to pipe
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        // Execute command using shell
        execl("/bin/sh", "sh", "-c", command, (char*)nullptr);

        // If execl returns, it failed
        exit(1);
    }

    // Parent process
    close(pipe_fds[1]); // Close write end

    // Set read end to non-blocking
    int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);

    proc_data->pid = pid;
    proc_data->stdout_pipe_fd = pipe_fds[0];

    result.process = proc_data;
    result.stdout_pipe = (void*)(intptr_t)pipe_fds[0];
    result.success = true;
    return result;
}

bool platform_process_is_running(platform_process_t process) {
    if (process == nullptr) return false;

    platform_process_data_t* proc_data = (platform_process_data_t*)process;
    if (proc_data->pid == -1) return false;

    int status;
    pid_t result = waitpid(proc_data->pid, &status, WNOHANG);

    if (result == 0) {
        // Process is still running
        return true;
    } else if (result == proc_data->pid) {
        // Process has exited
        return false;
    } else {
        // Error or invalid pid
        return false;
    }
}

void platform_process_terminate(platform_process_t process) {
    if (process == nullptr) return;

    platform_process_data_t* proc_data = (platform_process_data_t*)process;
    if (proc_data->pid != -1) {
        kill(proc_data->pid, SIGTERM);
    }
}

void platform_process_cleanup(platform_process_t process) {
    if (process == nullptr) return;

    platform_process_data_t* proc_data = (platform_process_data_t*)process;

    // Wait for process to exit if still running
    if (proc_data->pid != -1) {
        int status;
        waitpid(proc_data->pid, &status, 0);
    }

    if (proc_data->stdout_pipe_fd != -1) {
        close(proc_data->stdout_pipe_fd);
    }

    free(proc_data);
}

///////////////////////////////////////////
// Pipe/IPC

int32_t platform_pipe_peek(platform_pipe_t pipe) {
    if (pipe == nullptr) return -1;

    int fd = (int)(intptr_t)pipe;

    // Use ioctl to check available bytes
    int available = 0;
    if (ioctl(fd, FIONREAD, &available) == -1) {
        return 0;
    }

    return available;
}

int32_t platform_pipe_read(platform_pipe_t pipe, char* buffer, int32_t buffer_size) {
    if (pipe == nullptr) return -1;

    int fd = (int)(intptr_t)pipe;
    ssize_t bytes_read = read(fd, buffer, buffer_size);

    if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available (non-blocking)
            return 0;
        }
        return -1;
    }

    return (int32_t)bytes_read;
}

void platform_pipe_close(platform_pipe_t pipe) {
    if (pipe == nullptr) return;
    int fd = (int)(intptr_t)pipe;
    close(fd);
}

///////////////////////////////////////////
// File dialogs

bool platform_file_dialog_save(char* filename_buffer, int32_t buffer_size, const char* title) {
    // On Linux, we'll use a simple console-based fallback
    // In a real application, you might want to use:
    // - zenity (GTK-based file dialog)
    // - kdialog (KDE-based file dialog)
    // - A portable ImGui file dialog library

    // For now, try zenity if available
    FILE* fp = popen("which zenity 2>/dev/null", "r");
    if (fp != nullptr) {
        char result[128];
        if (fgets(result, sizeof(result), fp) != nullptr) {
            pclose(fp);

            // zenity is available, use it
            char command[1024];
            snprintf(command, sizeof(command),
                     "zenity --file-selection --save --confirm-overwrite --title=\"%s\" 2>/dev/null",
                     title);

            fp = popen(command, "r");
            if (fp != nullptr) {
                if (fgets(filename_buffer, buffer_size, fp) != nullptr) {
                    // Remove trailing newline
                    size_t len = strlen(filename_buffer);
                    if (len > 0 && filename_buffer[len - 1] == '\n') {
                        filename_buffer[len - 1] = '\0';
                    }
                    pclose(fp);
                    return filename_buffer[0] != '\0';
                }
                pclose(fp);
            }
        } else {
            pclose(fp);
        }
    }

    // Fallback: prompt in terminal
    printf("\n%s\n", title);
    printf("Enter filename: ");
    fflush(stdout);

    if (fgets(filename_buffer, buffer_size, stdin) != nullptr) {
        // Remove trailing newline
        size_t len = strlen(filename_buffer);
        if (len > 0 && filename_buffer[len - 1] == '\n') {
            filename_buffer[len - 1] = '\0';
        }
        return filename_buffer[0] != '\0';
    }

    return false;
}

///////////////////////////////////////////
// Working directory

#include <linux/limits.h>

void platform_set_working_dir_to_exe() {
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len == -1) return;

    path[len] = '\0';

    // Find the last slash and terminate there
    char* last_slash = strrchr(path, '/');
    if (last_slash != nullptr) {
        *last_slash = '\0';
        chdir(path);
    }
}

#endif // PLATFORM_LINUX
