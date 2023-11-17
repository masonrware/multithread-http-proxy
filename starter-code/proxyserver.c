#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "proxyserver.h"


/*
 * Constants
 */
#define RESPONSE_BUFSIZE 10000

/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    return;
}

/*
 * forward the client request to the fileserver and
 * forward the fileserver response to the client
 */
void serve_request(int client_fd) {

    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }

    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);

    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        printf("Failed to connect to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    // successfully connected to the file server
    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));

    // forward the client request to the fileserver
    int bytes_read = read(client_fd, buffer, RESPONSE_BUFSIZE);
    int ret = http_send_data(fileserver_fd, buffer, bytes_read);
    if (ret < 0) {
        printf("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");

    } else {
        // forward the fileserver response to the client
        while (1) {
            int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
            if (bytes_read <= 0) // fileserver_fd has been closed, break
                break;
            ret = http_send_data(client_fd, buffer, bytes_read);
            if (ret < 0) { // write failed, client_fd has been closed
                break;
            }
        }
    }

    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // Free resources and exit
    free(buffer);
}


/*
    Create an array of pthread_t type with the length equal to numlisteners
    For each thread in the array, instantiate the thread with Pthread_create
        - What function do we pass each thread?
            - listen() system call?
        - Should we join these threads to the main thread, or have them function independently?

    split serve_forever into listen_forever, serve_forever
    each listener bound to a different port

    worker threads and listener threads
        - worker calls serve_forever
        - listener calls listen_forever
*/

// pass args into a thread
struct ListenerThreadArgs {
    int* client_fd;
    int port;
};

struct WorkerThreadArgs {
    int* server_fd;
    int port;
};

// I think we can remove this fd since each
// int server_fd;

// array to access thread arguments globally, corresponds to num_listener
struct ListenerThreadArgs* listener_args_array;
// array to access thread arguments globally, corresponds to num_worker
struct WorkerThreadArgs* worker_args_array;


// TODO
// take in void* args, convert to struct pointer for ThreadArgs
void listen_forever(void* listener_args){
    struct ListenerThreadArgs *args = (struct ListenerThreadArgs *) listener_args;

    return;
}

/*
 * opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */

// take in void* args, convert to struct pointer for ThreadArgs
void serve_forever(void* worker_args) {
    struct WorkerThreadArgs *args = (struct WorkerThreadArgs *) worker_args;

    // create a socket to listen
    *args->server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (*args->server_fd == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(*args->server_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }

    // pass in port with server_fd, set proxy_port to port that is passed in
    // create argument struct to pass to thread so we can access port and server_fd
    int proxy_port = listener_ports[0];
    // create the full address of this proxyserver
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(proxy_port); // listening port

    // bind the socket to the address and port number specified in
    if (bind(*args->server_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }
    
    /*
    * BELOW IS LISTENING CODE
    */

    // starts waiting for the client to request a connection
    if (listen(*args->server_fd, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", proxy_port);

    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_fd;
    while (1) {
        client_fd = accept(*args->server_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length); // listener threads
        if (client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);

        serve_request(client_fd); // worker threads

        // close the connection to the client
        shutdown(client_fd, SHUT_WR);
        close(client_fd);
    }

    shutdown(*args->server_fd, SHUT_RDWR);
    close(*args->server_fd);
}

/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 1;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    printf("\t---- Setting ----\n");
    printf("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        printf(" %d", listener_ports[i]);
    printf(" ]\n");
    printf("\t%d workers\n", num_listener);
    printf("\tfileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    printf("\tmax queue size  %d\n", max_queue_size);
    printf("\t  ----\t----\t\n");
}

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    // close listener server fds
    for (int i = 0; i < num_listener; i++) {
        // if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");

        // modified to close each server file descriptor
        if (close(*listener_args_array[i].server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    // close worker client fds
    for (int i = 0; i < num_workers; i++) {
        // if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");

        // modified to close each client file descriptor
        if (close(*worker_args_array[i].client_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    free(listener_ports);
    exit(0);
}

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("-l", argv[i]) == 0) {
            num_listener = atoi(argv[++i]);
            free(listener_ports);
            // give each listener a unique port starting from specified port
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++) {
                listener_ports[j] = atoi(argv[++i]);
            }
        } else if (strcmp("-w", argv[i]) == 0) {
            num_workers = atoi(argv[++i]);
        } else if (strcmp("-q", argv[i]) == 0) {
            max_queue_size = atoi(argv[++i]);
        } else if (strcmp("-i", argv[i]) == 0) {
            fileserver_ipaddr = argv[++i];
        } else if (strcmp("-p", argv[i]) == 0) {
            fileserver_port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();

    // make space for lists of Thread Arguments
    listener_args_array = (struct ListenerThreadArgs*) malloc(sizeof(struct ListenerThreadArgs) * num_listener);
    worker_args_array = (struct WorkerThreadArgs*) malloc(sizeof(struct WorkerThreadArgs) * num_workers);

    // create listener threads for num_listener
    pthread_t listeners[num_listener];
    for (int i = 0; i < num_listener; i++){

        struct ListenerThreadArgs args;
        args.port = listener_ports[i];
        listener_args_array[i] = args; // place struct in listeners array

        if (pthread_create(&listeners[i], NULL, listen_forever, (void*) &listener_args_array[i]) != 0){
            fprintf(stderr, "Failed to create thread\n");
            return 1;
        }
        // should we join these?
    }

    // create worker threads for num_workers
    pthread_t workers[num_workers];
    for (int i = 0; i < num_workers; i++){
        struct WorkerThreadArgs args;
        // workers don't need a port (I think)
        // MW: I think workers DO need a port -- the port of the target. Of course, this is unchanging, but it can't hurt to save it for consistency?
        args.port = fileserver_port;
        worker_args_array[i] = args;

        // pass in a struct so each worker can create its own socket
        if (pthread_create(&workers[i], NULL, serve_forever, (void*) &worker_args_array[i]) != 0){
            fprintf(stderr, "Failed to create thread\n");
            return 1;
        }
        // should we join these?
    }

    // serve_forever(&server_fd);

    return EXIT_SUCCESS;
}
