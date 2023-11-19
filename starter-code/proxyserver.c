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
#include "safequeue.h"


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
struct PriorityQueue pq;
// pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
// pthread_cond_t fill = PTHREAD_COND_INITIALIZER;
// pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty;
pthread_cond_t fill;
pthread_mutex_t mutex;
pthread_mutex_t qlock;
int count = 0;

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
        int c = 0;
        while (1) {
            if (c % 50 == 0) printf("serve request...\n");
            c++;
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

// pass args into a thread
struct ListenerThreadArgs {
    int proxy_fd;
    int client_fd;
    int port;
};

// array to access thread arguments globally, corresponds to num_listener
struct ListenerThreadArgs* listener_args_array;

// take in void* args, convert to struct pointer for ThreadArgs
void* listen_forever(void* listener_args){
    printf("Listen forever\n");
    struct ListenerThreadArgs *args = (struct ListenerThreadArgs *) listener_args;

    // create a socket to listen
    args->proxy_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (args->proxy_fd == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(args->proxy_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }

    // assign socket thread-specific port
    int proxy_port = args->port;

    // create the full address of this proxyserver
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(proxy_port); // listening port

    // bind the socket to the address and port number specified in
    if (bind(args->proxy_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }
    
    // starts waiting for the client to request a connection
    if (listen(args->proxy_fd, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", proxy_port);

    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int c = 0;
    while (1) {
        if (c % 50 == 0) printf("listening...\n");
        c++;
        args->client_fd = accept(args->proxy_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length); // listener threads
        if (args->client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);
        
        struct parsed_request *request = malloc(sizeof(struct parsed_request));
        request = parse_client_request(args->client_fd);

        // request is a GET_JOB request
        if(strcmp(request->path, GETJOBCMD)==0) {
            printf("GETJOB\n");
            if (pq.size > 0){
                printf("PQ NOT EMPTY\n");
                int payload_fd;
                pthread_mutex_lock(&mutex);
                payload_fd = get_work(&pq).data;
                count-=1;
                pthread_mutex_unlock(&mutex);

                struct parsed_request *qpop = malloc(sizeof(struct parsed_request));
                qpop = parse_client_request(payload_fd);

                // don't know what 3rd arg should be
                int ret = http_send_data(args->client_fd, qpop->path, 64);
                if (ret < 0) {
                    printf("Failed to send request to the file server\n");
                    send_error_response(args->client_fd, BAD_GATEWAY, "Bad Gateway");
                }

                // close the connection to the client
                shutdown(payload_fd, SHUT_WR);
                close(payload_fd);

                shutdown(args->client_fd, SHUT_WR);
                close(args->client_fd);
            }
            else {
                printf("PQ EMPTY\n");
                send_error_response(args->client_fd, QUEUE_EMPTY, "Queue Empty");
                shutdown(args->client_fd, SHUT_WR);
                close(args->client_fd);
            }
        } 
        // request is a GET request
        else {
            printf("LISTEN LOCK\n");
            pthread_mutex_lock(&mutex);
            int count = 0;
            while(count == max_queue_size) {
                // if (count % 50 == 0) printf("[listen] Wating...\n");
                count+=1;
                pthread_cond_wait(&empty, &mutex);
            }
            pthread_mutex_lock(&qlock);
            
            if(add_work(&pq, args->client_fd, request->priority) < 0) {
                send_error_response(args->client_fd, QUEUE_FULL, "Queue Full");
                pthread_mutex_unlock(&qlock);
            } else {
                pthread_mutex_unlock(&qlock);
                count+=1;
            }

            pthread_cond_signal(&fill);
            pthread_mutex_unlock(&mutex);
            printf("LISTEN UNLOCK\n");
        }
    }

    return NULL;
}

/*
 * opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */

// take in void* args, convert to struct pointer for ThreadArgs
void* serve_forever(void* null) {
    printf("Serve forever\n");
    int payload_fd;
    while(1) {
        printf("SERVE LOCK\n");
        pthread_mutex_lock(&mutex);
        int count = 0;
        while(count == 0) {
            // if (count % 50 == 0) printf("[serve] Waiting...\n");
            count+=1;
            pthread_cond_wait(&fill, &mutex);
        }
        pthread_mutex_lock(&qlock);
        payload_fd = get_work(&pq).data;
        pthread_mutex_unlock(&qlock);

        count-=1;
        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&mutex);
        printf("SERVE UNLOCK 1\n");

        serve_request(payload_fd);

        struct parsed_request *payload = malloc(sizeof(struct parsed_request));
        payload = parse_client_request(payload_fd);

        if(payload->delay > 0) {
            sleep(payload->delay);
        }

        // close the connection to the client
        shutdown(payload_fd, SHUT_WR);
        close(payload_fd);
    }
    pthread_mutex_unlock(&mutex);
    printf("SERVE UNLOCK 2\n");

    shutdown(payload_fd, SHUT_RDWR);
    close(payload_fd);
    return null;
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
    // close listener proxy fds and client fds
    for (int i = 0; i < num_listener; i++) {
        if (close(listener_args_array[i].proxy_fd) < 0) perror("Failed to close proxy_fd (ignoring)\n");
        if (close(listener_args_array[i].client_fd) < 0) perror("Failed to close client_fd (ignoring)\n");
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
    printf("Main\n");
    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&qlock, NULL);
    pthread_cond_init(&empty, NULL);
    pthread_cond_init(&fill, NULL);

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
            create_queue(&pq, max_queue_size, 0);
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
    printf("Parsed\n");

    // make space for lists of Thread Arguments
    // TODO: REMEMBER TO FREE THIS EVENTUALLY
    listener_args_array = (struct ListenerThreadArgs*) malloc(sizeof(struct ListenerThreadArgs) * num_listener);

    printf("Creating listener threads\n");
    // create listener threads for num_listener
    pthread_t listeners[num_listener];
    for (int i = 0; i < num_listener; i++){

        struct ListenerThreadArgs args;
        args.port = listener_ports[i];
        listener_args_array[i] = args; // place struct in listeners array

        if (pthread_create(&listeners[i], NULL, (void *) listen_forever, (void*) &listener_args_array[i]) != 0){
            fprintf(stderr, "Failed to create thread\n");
            return 1;
        }
    }
    printf("Created %d listeners\n", num_listener);

    // create worker threads for num_workers
    pthread_t workers[num_workers];
    for (int i = 0; i < num_workers; i++){
        // pass in a struct so each worker can create its own socket
        if (pthread_create(&workers[i], NULL, (void *) serve_forever, NULL) != 0){
            fprintf(stderr, "Failed to create thread\n");
            return 1;
        }
    }
    printf("Created %d workers\n", num_workers);

    // join on listeners
    for (int i = 0; i < num_listener; i++) {
        pthread_join(listeners[i], NULL);
    }
    printf("Joined all listeners\n");

    // join on workers
    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL);
    }
    printf("Joined all workers, returning from main\n");

    return EXIT_SUCCESS;
}
