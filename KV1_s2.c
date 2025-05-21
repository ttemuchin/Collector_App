#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <signal.h>
#include <asm-generic/socket.h>

#define PORT 8081
#define FIFO_NAME "fifo_server2"
#define MAX_CLIENTS 5

typedef struct {
    int socket;
    int client_id;
    pthread_t thread;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS] = {0};
int active_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

volatile sig_atomic_t stop_server = 0;

void handle_signal(int sig) {
    stop_server = 1;
}

void log_message(const char *message) {
    if (access(FIFO_NAME, F_OK) == -1) {
        if (mkfifo(FIFO_NAME, 0666) == -1) {
            perror("mkfifo failed");
            return;
        }
    }
    
    int fd = open(FIFO_NAME, O_WRONLY);
    if (fd != -1) {
        write(fd, message, strlen(message) + 1);
        close(fd);
    }
}

int count_processes() {
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    
    dir = opendir("/proc");
    if (!dir) {
        perror("opendir");
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        char *endptr;
        long pid = strtol(entry->d_name, &endptr, 10);
        
        if (endptr != entry->d_name && *endptr == '\0' && pid > 0) {
            char path[262];
            struct stat statbuf;
            snprintf(path, sizeof(path), "/proc/%s", entry->d_name);

            if (stat(path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                count++;
            }
        }
    }
    
    closedir(dir);
    return count;
}

void get_kernel_version(char *buffer, size_t size) {
    struct utsname uts;
    if (uname(&uts) == -1) {
        snprintf(buffer, size, "Error getting kernel version");
        return;
    }
    snprintf(buffer, size, "%s", uts.release);
}

void *handle_client(void *arg) {
    ClientInfo *info = (ClientInfo *)arg;
    char log_msg[256];

    char send_msg[256];
    snprintf(send_msg, sizeof(send_msg), "Client entry %d", info->client_id);
    send(info->socket, send_msg, strlen(send_msg), 0);
    
    snprintf(log_msg, sizeof(log_msg), "Client %d connected to Process server", info->client_id);
    log_message(log_msg);

    char buffer[1024] = {0};
    
    while (!stop_server) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(info->socket, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        if (select(info->socket + 1, &read_fds, NULL, NULL, &tv) > 0) {
            ssize_t valread = read(info->socket, buffer, sizeof(buffer) - 1);
            if (valread > 0) {
                buffer[valread] = '\0';
                if (strstr(buffer, "DISCONNECT")) {
                    break;
                }
                if (strstr(buffer, "SHUTDOWN")) {
                    int requesting_client;
                    sscanf(buffer, "SHUTDOWN %d", &requesting_client);
                    
                    pthread_mutex_lock(&clients_mutex);
                    if (active_clients == 1 && requesting_client == info->client_id) {
                        pthread_mutex_unlock(&clients_mutex);
                        log_message("Shutdown request approved");
                        stop_server = 1;
                        break;
                    }
                    pthread_mutex_unlock(&clients_mutex);
                }
            } else if (valread == 0) {
                break;
            }
        }
        
        char response[1024];
        int processes = count_processes();
        char kernel_version[256];
        get_kernel_version(kernel_version, sizeof(kernel_version));
        
        snprintf(response, sizeof(response), 
                "System Processes: %d\nKernel Version: %s", 
                processes, kernel_version);

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[20];
        strftime(time_str, 20, "%Y-%m-%d %H:%M:%S", tm_info);
        
        char full_response[2048];
        snprintf(full_response, sizeof(full_response), "[%s]\n%s", time_str, response);
        
        if (send(info->socket, full_response, strlen(full_response), 0) <= 0) {
            perror("send failed");
            break;
        }
        
        sleep(4);
    }
    
    pthread_mutex_lock(&clients_mutex);
    active_clients--;
    pthread_mutex_unlock(&clients_mutex);
    
    close(info->socket);
    snprintf(log_msg, sizeof(log_msg), "Client %d disconnected from Process server", info->client_id);
    log_message(log_msg);
    
    free(info);
    return NULL;
}

int is_server_running() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) return 0;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        close(sock);
        return 1;
    }
    
    close(sock);
    return 0;
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (is_server_running()) {
        printf("Process server is already running\n");
        return 1;
    }
    
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    log_message("Process server started");
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("Process server listening on port %d\n", PORT);
    
    while (!stop_server) {
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) continue;

        char client_id_buf[32] = {0};
        int valread = read(new_socket, client_id_buf, sizeof(client_id_buf)-1);
        if (valread <= 0) {
            close(new_socket);
            continue;
        }
        
        int client_id = atoi(client_id_buf);
        if (client_id < 1 || client_id > 5) {
            close(new_socket);
            continue;
        }
        
        pthread_mutex_lock(&clients_mutex);
        if (active_clients >= MAX_CLIENTS) {
            pthread_mutex_unlock(&clients_mutex);
            close(new_socket);
            continue;
        }
        
        ClientInfo *info = malloc(sizeof(ClientInfo));
        info->socket = new_socket;
        info->client_id = client_id;
        active_clients++;
        
        pthread_create(&info->thread, NULL, handle_client, info);
        pthread_detach(info->thread);
        pthread_mutex_unlock(&clients_mutex);
    }
    
    log_message("Process server stopped gracefully");

    close(server_fd);
    return 0;
}