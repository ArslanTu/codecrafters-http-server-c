#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_THREADS 10

struct Request {
    char method[8];
    char path[1024];
    char protocol[32];
};

static const struct option long_options[] = {
    {"directory", required_argument, NULL, 'd'},
};

int handle_request(const int *server_fd, const char *directory) {
    struct sockaddr_in client_addr;
    printf("Waiting for a client to connect...\n");
    unsigned int client_addr_len = sizeof(client_addr);

    int client_socket_fd =
        accept(*server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    printf("Client connected\n");

    // receive request
    char raw_request[1024];
    recv(client_socket_fd, raw_request, sizeof(raw_request), 0);

    // parse request
    char *first_splash_r = strchr(raw_request, (int)'\r');
    if (first_splash_r == NULL) {
        printf("Invalid request\n%s\n", raw_request);
        return 0;
    }
    unsigned int start_line_len = first_splash_r - raw_request;
    char start_line[1024];
    memcpy(start_line, raw_request, start_line_len);

    struct Request request;
    sscanf(
        start_line, "%s %s %s", request.method, request.path, request.protocol);

    printf(
        "Request: %s %s %s\n", request.method, request.path, request.protocol);

    // send response
    if (strcmp(request.path, "/") == 0) {
        char response[] = "HTTP/1.1 200 OK\r\n\r\n";
        send(client_socket_fd, response, strlen(response), 0);
    } else if (strncmp(request.path, "/echo", sizeof("/echo") - 1) == 0) {
        // make body
        char response_body[1024];
        sscanf(request.path, "/echo/%s", response_body);
        unsigned int body_len = strlen(response_body);

        // make header prefix
        char response_header_prefix[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: ";

        // make response
        char response[1024];
        sprintf(
            response,
            "%s%u\r\n\r\n%s",
            response_header_prefix,
            body_len,
            response_body);

        send(client_socket_fd, response, strlen(response), 0);

        printf("Response:\n%s\n", response);
        printf("Sended size: %ld\n", strlen(response));
    } else if (
        strncmp(request.path, "/user-agent", sizeof("/user-agent") - 1) == 0) {
        // get User-Agent value from the raw_request
        char *user_agent_start = strstr(raw_request, "User-Agent: ");
        if (user_agent_start == NULL) {
            printf("User-Agent not found\n");
            return 0;
        }
        user_agent_start += sizeof("User-Agent: ") - 1;
        char *user_agent_end = strchr(user_agent_start, (int)'\r');
        if (user_agent_end == NULL) {
            printf("User-Agent not found\n");
            return 0;
        }
        unsigned int user_agent_len = user_agent_end - user_agent_start;
        char user_agent[1024];
        memcpy(user_agent, user_agent_start, user_agent_len);
        user_agent[user_agent_len] = '\0';

        // make header prefix
        char response_header_prefix[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: ";

        // make response
        char response[1024];
        sprintf(
            response,
            "%s%u\r\n\r\n%s",
            response_header_prefix,
            user_agent_len,
            user_agent);

        send(client_socket_fd, response, strlen(response), 0);

        printf("Response:\n%s\n", response);
        printf("Sended size: %ld\n", strlen(response));

    } else if (strncmp(request.path, "/files", sizeof("/files") - 1) == 0) {
        char *file_name = request.path + sizeof("/files/") - 1;
        // if the file doesn't exist, send 404
        char file_path[1024];
        sprintf(file_path, "%s%s", directory, file_name);
        printf("File path: %s\n", file_path);
        FILE *file = fopen(file_path, "r");
        if (file == NULL) {
            char response[] =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Length: 0\r\n\r\n";
            send(client_socket_fd, response, strlen(response), 0);
            return 0;
        }
        // file exists, send the content
        char response_header_prefix[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: ";

        // cal the file size
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        rewind(file);

        // read the file content to a char list
        char file_content[file_size + 1];
        fread(file_content, sizeof(char), file_size, file);
        file_content[file_size] = '\0';
        unsigned int file_content_len = strlen(file_content);

        // close file
        fclose(file);

        // make response
        char response[4096];
        sprintf(
            response,
            "%s%u\r\n\r\n%s",
            response_header_prefix,
            file_content_len,
            file_content);
        send(client_socket_fd, response, strlen(response), 0);
    } else {
        char response[] =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 0\r\n\r\n";
        send(client_socket_fd, response, strlen(response), 0);
    }
    return 1;
}

void *handle_request_thread(void *args) {
    int ***arg_list = (int ***)args;
    const int *server_fd = *(arg_list[0]);
    const char *directory = *((char(*)[64])(arg_list[1]));
    while (1) {
        handle_request(server_fd, directory);
    }
}

int main(int argc, char *argv[]) {
    // Disable output buffering
    setbuf(stdout, NULL);

    // You can use print statements as follows for debugging, they'll be visible
    // when running tests.
    printf("Logs from your program will appear here!\n");

    // Uncomment this block to pass the first stage
    //

    // parse directory arg if exists
    char directory[64];

    int opt = 0;
    while ((opt = getopt_long(argc, argv, "d:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                strcpy(directory, optarg);
                printf("Directory: %s\n", directory);
            default:
                break;
        }
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Socket creation failed: %s...\n", strerror(errno));
        return 1;
    }

    // Since the tester restarts your program quite often, setting REUSE_PORT
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) <
        0) {
        printf("SO_REUSEPORT failed: %s \n", strerror(errno));
        return 1;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr = {htonl(INADDR_ANY)},
    };

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) !=
        0) {
        printf("Bind failed: %s \n", strerror(errno));
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        printf("Listen failed: %s \n", strerror(errno));
        return 1;
    }

    pthread_t threads[MAX_THREADS];
    int **args[2];
    int *server_fd_ptr = &server_fd;
    int **directory_ptr = (int **)(&directory);
    args[0] = &server_fd_ptr;
    args[1] = directory_ptr;

    for (int i = 0; i < MAX_THREADS; ++i) {
        pthread_t thread;
        pthread_create(&thread, NULL, handle_request_thread, (void *)args);
        threads[i] = thread;

        // such code is wrong
        // because the value of `thread` is determined by `pthread_creat` call
        // you should not put `thread` into `threads` before it is determined
        // pthread_t thread;
        // threads[i] = thread;
        // pthread_create(&thread, NULL, handle_request_thread, &server_fd);
    }

    for (int i = 0; i < MAX_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    close(server_fd);

    return 0;
}
