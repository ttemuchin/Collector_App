#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>

#define LOG_DIR "logs"
#define SERVER1_LOG "logs/server1.log"
#define SERVER2_LOG "logs/server2.log"
#define FIFO_SERVER1 "fifo_server1"
#define FIFO_SERVER2 "fifo_server2"

void ensure_log_dir_exists() {
    mkdir(LOG_DIR, 0777);
}

void *handle_server1_logs(void *arg) {
    int fd;
    char buffer[1024];
    
    if (access(FIFO_SERVER1, F_OK) == -1) {
        if (mkfifo(FIFO_SERVER1, 0666) == -1) {
            perror("mkfifo failed");
            exit(1);
        }
    }

    fd = open(FIFO_SERVER1, O_RDONLY);
    if (fd == -1) {
            perror("open FIFO_SERVER1 failed");
            exit(1);
        }

    FILE *log_file = fopen(SERVER1_LOG, "a");
    if (!log_file) {
        perror("Failed to open server1 log file");
        exit(1);
    }
    
    while (1) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer)-1);

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char time_str[20];
            strftime(time_str, 20, "%Y-%m-%d %H:%M:%S", tm_info);
            
            fprintf(log_file, "[%s] %s\n", time_str, buffer);
            fflush(log_file);
        }
    }
    
    close(fd);
    fclose(log_file);
    return NULL;
}

void *handle_server2_logs(void *arg) {
    int fd;
    char buffer[1024];

    if (access(FIFO_SERVER2, F_OK) == -1) {
        if (mkfifo(FIFO_SERVER2, 0666) == -1) {
            perror("mkfifo failed");
            exit(1);
        }
    }

    fd = open(FIFO_SERVER2, O_RDONLY);
    if (fd == -1) {
            perror("open FIFO_SERVER2 failed");
            exit(1);
        }
    
    FILE *log_file = fopen(SERVER2_LOG, "a");
    if (!log_file) {
        perror("Failed to open server2 log file");
        exit(1);
    }
    
    while (1) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer)-1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char time_str[20];
            strftime(time_str, 20, "%Y-%m-%d %H:%M:%S", tm_info);
            
            fprintf(log_file, "[%s] %s\n", time_str, buffer);
            fflush(log_file);
        }
    }
    
    close(fd);
    fclose(log_file);
    return NULL;
}

int main() {
    ensure_log_dir_exists();
    
    pthread_t thread1, thread2;
    
    pthread_create(&thread1, NULL, handle_server1_logs, NULL);
    pthread_create(&thread2, NULL, handle_server2_logs, NULL);
    
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    
    return 0;
}