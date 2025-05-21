#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define CPU_SERVER_PORT 8080
#define PROCESS_SERVER_PORT 8081
#define SERVER_IP "127.0.0.1"

// Новая структура для хранения информации о клиенте
typedef struct {
    GtkWidget *text_view;
    int socket_fd;
    gboolean active;
    pthread_t thread_id;
    int client_id;  // ID клиента (1-5)
} ServerConnection;

// Глобальные переменные
ServerConnection cpu_connection = {0};
ServerConnection process_connection = {0};
GtkWidget *id_spin;

// Новая структура для передачи данных в idle-функцию
typedef struct {
    GtkWidget *text_view;
    char buffer[2048];
} UpdateData;

// Функция для обновления текста, вызываемая из главного потока
static gboolean update_text_view_idle(gpointer user_data) {
    UpdateData *data = (UpdateData *)user_data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->text_view));
    gtk_text_buffer_set_text(buffer, data->buffer, -1);

    // GtkTextIter start, end;
    // gtk_text_buffer_get_start_iter(buffer, &start);
    // gtk_text_buffer_get_end_iter(buffer, &end);
    // gchar *current_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    // gchar *new_text = g_strconcat(current_text, data->buffer, NULL);
    // gtk_text_buffer_set_text(buffer, new_text, -1);

    // g_free(current_text);
    // g_free(new_text);
    free(data); // Освобождаем память
    return G_SOURCE_REMOVE; // Удаляем источник после выполнения
}

void *receive_data(void *arg) {
    ServerConnection *conn = (ServerConnection *)arg;
    char buffer[2048] = {0};
    
    ssize_t valread = read(conn->socket_fd, buffer, sizeof(buffer) - 1);
    if (valread > 0) {
        buffer[valread] = '\0';
        
        // Создаем структуру для передачи данных
        UpdateData *data = malloc(sizeof(UpdateData));
        data->text_view = conn->text_view;
        strncpy(data->buffer, buffer, sizeof(data->buffer));
        
        // Передаем в главный поток GTK
        g_idle_add(update_text_view_idle, data);
        sleep(5);
    } else {
        conn->active = FALSE;
        return NULL;
    }

    while (conn->active) {
        ssize_t valread = read(conn->socket_fd, buffer, sizeof(buffer) - 1);
        if (valread > 0) {
            buffer[valread] = '\0';
            
            // Создаем структуру для передачи данных
            UpdateData *data = malloc(sizeof(UpdateData));
            data->text_view = conn->text_view;
            strncpy(data->buffer, buffer, sizeof(data->buffer));
            
            // Передаем в главный поток GTK
            g_idle_add(update_text_view_idle, data);
        } else if (valread == 0) {
            g_print("Server disconnected\n");
            conn->active = FALSE;
            break;
        } else {
            perror("read");
            conn->active = FALSE;
            break;
        }
    }
    
    return NULL;
}

gboolean connect_to_server(ServerConnection *conn, int port) {
    if (conn->active) {
        return TRUE;
    }
    
    struct sockaddr_in serv_addr;
    
    if ((conn->socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        return FALSE;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("invalid address");
        close(conn->socket_fd);
        return FALSE;
    }
    
    if (connect(conn->socket_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connection failed");
        close(conn->socket_fd);
        return FALSE;
    }

    char id_msg[32];
    int client_id = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(id_spin));
    snprintf(id_msg, sizeof(id_msg), "%d", client_id);
    send(conn->socket_fd, id_msg, strlen(id_msg), 0);

    char server_response[32] = {0};
    ssize_t valread = read(conn->socket_fd, server_response, sizeof(server_response)-1);
    if (valread > 0) {
        server_response[valread] = '\0';
        if (strstr(server_response, "ID_ALREADY_USED")) {
            g_print("This client (%d) is already in use\n", client_id);
            close(conn->socket_fd);
            return FALSE;
        }
        if (strstr(server_response, "SERVER_FULL")) {
            g_print("This server is full\n");
            close(conn->socket_fd);
            return FALSE;
        }
    }

    conn->client_id = client_id;
    conn->active = TRUE;
    pthread_create(&conn->thread_id, NULL, receive_data, conn);
    
    return TRUE;
}

void disconnect_from_server(ServerConnection *conn) {
    if (conn->active) {
        const char *disconnect_cmd = "DISCONNECT\n";
        send(conn->socket_fd, disconnect_cmd, strlen(disconnect_cmd), 0);

        conn->active = FALSE;
        pthread_cancel(conn->thread_id);
        close(conn->socket_fd);
    }
}

void on_cpu_connect_clicked(GtkWidget *widget, gpointer data) {
    if (connect_to_server(&cpu_connection, CPU_SERVER_PORT)) {
        g_print("Connected to CPU server\n");
    } else {
        g_print("Failed to connect to CPU server\n");
        cpu_connection.active = FALSE; //////
    }
}

void on_cpu_disconnect_clicked(GtkWidget *widget, gpointer data) {
    disconnect_from_server(&cpu_connection);
    g_print("Disconnected from CPU server\n");
}

void on_process_connect_clicked(GtkWidget *widget, gpointer data) {
    if (connect_to_server(&process_connection, PROCESS_SERVER_PORT)) {
        g_print("Connected to Process server\n");
    } else {
        g_print("Failed to connect to Process server\n");
    }
}

void on_process_disconnect_clicked(GtkWidget *widget, gpointer data) {
    disconnect_from_server(&process_connection);
    g_print("Disconnected from Process server\n");
}

void on_window_destroy(GtkWidget *widget, gpointer data) {
    disconnect_from_server(&cpu_connection);
    disconnect_from_server(&process_connection);
    gtk_main_quit();
}

void send_server_command(int port, const char *command) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        return;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("invalid address");
        close(sock);
        return;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connection failed");
        close(sock);
        return;
    }
    send(sock, command, strlen(command), 0);
    close(sock);
}

int is_server_running(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        close(sock);
        return 0;
    }
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return 0;
    }
    
    close(sock);
    return 1;
}

void on_cpu_server_off(GtkWidget *widget, gpointer data) {
    if (!cpu_connection.active)
    {
        g_print("Not connected to CPU server\n");
        return;
    }
    char command[32];
    snprintf(command, sizeof(command), "SHUTDOWN %d", cpu_connection.client_id);
    send(cpu_connection.socket_fd, command, strlen(command), 0);

    usleep(100000);
}

void on_cpu_server_on(GtkWidget *widget, gpointer data) {
    if (!is_server_running(CPU_SERVER_PORT)) {
        system("./s1 &");
        g_print("CPU server started\n");
    } else {
        g_print("CPU server is already running\n");
    }
}

void on_process_server_off(GtkWidget *widget, gpointer data) {
    if (!process_connection.active)
    {
        g_print("Not connected to Process server\n");
        return;
    }
    char command[32];
    snprintf(command, sizeof(command), "SHUTDOWN %d", process_connection.client_id);
    send(process_connection.socket_fd, command, strlen(command), 0);
}

void on_process_server_on(GtkWidget *widget, gpointer data) {
    if (!is_server_running(CPU_SERVER_PORT)) {
        system("./s2 &");
        g_print("Process server started\n");
    } else {
        g_print("Process server is already running\n");
    }
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *cpu_connect_btn, *cpu_disconnect_btn;
    GtkWidget *process_connect_btn, *process_disconnect_btn;
    GtkWidget *cpu_label, *process_label;
    GtkWidget *cpu_scrolled_window, *process_scrolled_window;
    GtkWidget *cpu_text_view, *process_text_view;

    gtk_init(&argc, &argv);
    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), "COLLECTOR - Client");
    gtk_window_set_default_size(GTK_WINDOW(window), 770, 230);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    
    // CPU Server section
    cpu_label = gtk_label_new("Server 1");
    gtk_grid_attach(GTK_GRID(grid), cpu_label, 0, 0, 2, 1);
    
    cpu_connect_btn = gtk_button_new_with_label("Connect to S1");
    g_signal_connect(cpu_connect_btn, "clicked", G_CALLBACK(on_cpu_connect_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), cpu_connect_btn, 0, 1, 1, 1);
    
    cpu_disconnect_btn = gtk_button_new_with_label("Disconnect from S1");
    g_signal_connect(cpu_disconnect_btn, "clicked", G_CALLBACK(on_cpu_disconnect_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), cpu_disconnect_btn, 1, 1, 1, 1);
    
    cpu_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(cpu_scrolled_window, TRUE);
    gtk_widget_set_vexpand(cpu_scrolled_window, TRUE);
    gtk_grid_attach(GTK_GRID(grid), cpu_scrolled_window, 0, 2, 2, 1);
    
    cpu_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(cpu_text_view), FALSE);

    gtk_container_add(GTK_CONTAINER(cpu_scrolled_window), cpu_text_view);
    cpu_connection.text_view = cpu_text_view;
    
    // Process Server section
    process_label = gtk_label_new("Server2");
    gtk_grid_attach(GTK_GRID(grid), process_label, 2, 0, 2, 1);
    
    process_connect_btn = gtk_button_new_with_label("Connect to S2");
    g_signal_connect(process_connect_btn, "clicked", G_CALLBACK(on_process_connect_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), process_connect_btn, 2, 1, 1, 1);
    
    process_disconnect_btn = gtk_button_new_with_label("Disconnect from S2");
    g_signal_connect(process_disconnect_btn, "clicked", G_CALLBACK(on_process_disconnect_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), process_disconnect_btn, 3, 1, 1, 1);
    
    process_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(process_scrolled_window, TRUE);
    gtk_widget_set_vexpand(process_scrolled_window, TRUE);
    gtk_grid_attach(GTK_GRID(grid), process_scrolled_window, 2, 2, 2, 1);
    
    process_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(process_text_view), FALSE);

    gtk_container_add(GTK_CONTAINER(process_scrolled_window), process_text_view);
    process_connection.text_view = process_text_view;
 
    // Добавляем спиннер для ID клиента
    GtkWidget *id_label = gtk_label_new("Client ID:");
    id_spin = gtk_spin_button_new_with_range(1, 5, 1);
    gtk_grid_attach(GTK_GRID(grid), id_label, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), id_spin, 2, 4, 1, 1);

    // Добавляем кнопки управления сервером
    GtkWidget *cpu_server_on_btn = gtk_button_new_with_label("Start S1");
    g_signal_connect(cpu_server_on_btn, "clicked", G_CALLBACK(on_cpu_server_on), NULL);
    gtk_grid_attach(GTK_GRID(grid), cpu_server_on_btn, 0, 3, 1, 1);

    GtkWidget *cpu_server_off_btn = gtk_button_new_with_label("Stop S1");
    g_signal_connect(cpu_server_off_btn, "clicked", G_CALLBACK(on_cpu_server_off), NULL);
    gtk_grid_attach(GTK_GRID(grid), cpu_server_off_btn, 1, 3, 1, 1);

    GtkWidget *process_server_on_btn = gtk_button_new_with_label("Start S2");
    g_signal_connect(process_server_on_btn, "clicked", G_CALLBACK(on_process_server_on), NULL);
    gtk_grid_attach(GTK_GRID(grid), process_server_on_btn, 2, 3, 1, 1);

    GtkWidget *process_server_off_btn = gtk_button_new_with_label("Stop S2");
    g_signal_connect(process_server_off_btn, "clicked", G_CALLBACK(on_process_server_off), NULL);
    gtk_grid_attach(GTK_GRID(grid), process_server_off_btn, 3, 3, 1, 1);

    gtk_widget_show_all(window);
    gtk_main();
    
    return 0;
}