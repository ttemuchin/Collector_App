#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <asm-generic/socket.h>

#define PORT 8080
#define FIFO_NAME "fifo_server1"
#define MAX_CLIENTS 5

typedef struct {
    int socket;
    int client_id;
    int active;
    pthread_t thread;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS] = {0};

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_fd;

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

void get_cpu_info(char *buffer, size_t size) {
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    if (!cpuinfo) {
        snprintf(buffer, size, "Error reading CPU info");
        return;
    }

    char line[256];
    char model_name[256] = "Unknown";
    int cpu_cores = 0;

    while (fgets(line, sizeof(line), cpuinfo)) {
        if (strstr(line, "model name")) {
            strcpy(model_name, strchr(line, ':') + 2);
            model_name[strcspn(model_name, "\n")] = 0;
        }
        if (strstr(line, "processor")) {
            cpu_cores++;
        }
    }
    fclose(cpuinfo);

    snprintf(buffer, size, "CPU Architecture: %s\nLogical Processors: %d", model_name, cpu_cores);
}

void *handle_client(void *arg) {
    ClientInfo *info = (ClientInfo *)arg;
    char log_msg[256];

    char send_msg[256];
    snprintf(send_msg, sizeof(send_msg), "Client entry %d", info->client_id);
    send(info->socket, send_msg, strlen(send_msg), 0);

    sleep(3);
    
    snprintf(log_msg, sizeof(log_msg), "Client %d connected to CPU server", info->client_id);
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

                    int active_clients = 0;
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (clients[i].active) { // == 0
                            active_clients++;
                        }
                    }
                    
                    if (active_clients == 1 && requesting_client == info->client_id) {   
                        log_message("Shutdown request approved");
                        stop_server = 1;
                        shutdown(server_fd, SHUT_RDWR);
                        pthread_mutex_unlock(&clients_mutex);
                        break;
                    }
                    pthread_mutex_unlock(&clients_mutex);
                }
            } else if (valread == 0) {
                break; // Клиент закрыл соединение
            }
        }
        
        // Основная логика отправки данных
        char response[1024];
        get_cpu_info(response, sizeof(response));
        
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
        
        sleep(5);
    }
    
    // Обновляем счетчик активных клиентов
    pthread_mutex_lock(&clients_mutex);

    info->active = 0;
    info->socket = 0;

    pthread_mutex_unlock(&clients_mutex);
    
    close(info->socket);
    snprintf(log_msg, sizeof(log_msg), "Client %d disconnected from CPU server", info->client_id);
    log_message(log_msg);
    
    // free(info);
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
        printf("CPU server is already running\n");
        return 1;
    }
    
    int new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    log_message("CPU server started");
    
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
    
    printf("CPU server listening on port %d\n", PORT);
    
    while (!stop_server) {
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            if (stop_server) break;
            continue;
        }

        // Получаем ID клиента (первое сообщение)
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

        int id_already_used = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && clients[i].client_id == client_id) {
                id_already_used = 1;
                break;
            }
        }

        if (id_already_used) {
            char reject_msg[] = "ID_ALREADY_USED";
            send(new_socket, reject_msg, strlen(reject_msg), 0);
            pthread_mutex_unlock(&clients_mutex);
            close(new_socket);
            continue;
        }

        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                slot = i;
                break;
            }
        }
        
        if (slot == -1) {
            char reject_msg[] = "SERVER_FULL\n";
            send(new_socket, reject_msg, strlen(reject_msg), 0);
            pthread_mutex_unlock(&clients_mutex);
            close(new_socket);
            continue;
        }

        clients[slot].socket = new_socket;
        clients[slot].client_id = client_id;
        clients[slot].active = 1;

        pthread_create(&clients[slot].thread, NULL, handle_client, &clients[slot]);
        pthread_detach(clients[slot].thread);
        pthread_mutex_unlock(&clients_mutex);
    }

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            shutdown(clients[i].socket, SHUT_RDWR);
            close(clients[i].socket);
            pthread_cancel(clients[i].thread);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    close(server_fd);
    log_message("CPU server stopped gracefully");
    return 0;
}
