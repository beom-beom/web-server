#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUF_SIZE 1000
#define HEADER_FMT "HTTP/1.1 %d %s\nContent-Length: %ld\nContent-Type: %s\n\n"
#define NOT_FOUND_CONTENT       "<h1>404 Not Found</h1>\n"
#define SERVER_ERROR_CONTENT    "<h1>500 Internal Server Error</h1>\n"

void fill_header(char *header, int status, long len, char *type);
void error_handling(char *message);
void handle_404(int asock);
void handle_500(int asock);
void *client_handler(void *arg);
void accept_connection(struct sockaddr_in clnt_addr,int clnt_sock,socklen_t clnt_addr_size);
void bind_and_listen(int serv_sock, int backlog, int port);

int main(int argc, char *argv[]) {
    int port;
   int serv_sock;
   struct sockaddr_in clnt_addr;
   socklen_t clnt_addr_size;


   if(argc!=2){
           printf("Usage : %s <port>\n", argv[0]);
           return 0;
      }

   serv_sock = socket(AF_INET, SOCK_STREAM, 0);
   bind_and_listen(serv_sock, 20, atoi(argv[1]));
   accept_connection(clnt_addr,serv_sock,clnt_addr_size);
   return 0;
}
void bind_and_listen(int serv_sock, int backlog, int port){	
	 struct sockaddr_in sin;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);
 	if(bind(serv_sock, (struct sockaddr *)&sin, sizeof(sin)) == -1)
    		error_handling("bind() error");
    	if(listen(serv_sock, backlog) == -1)
    		error_handling("listen() error");
}
void accept_connection(struct sockaddr_in clnt_addr,int clnt_sock,socklen_t clnt_addr_size){

	 while (1) {
        int *serv_sock = malloc(sizeof(int));
        *serv_sock = accept(clnt_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);
        if (*serv_sock < 0) {
            perror("[ERR] failed to accept.\n");
            free(serv_sock);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, (void *)serv_sock) != 0) {
            perror("[ERR] failed to create thread.\n");
            free(serv_sock);
            close(*serv_sock);
        }
    }

    close(clnt_sock);
}
void fill_header(char *header, int status, long len, char *type) {
    char status_text[40];
     switch (status) {
        case 200:
            strcpy(status_text, "OK");
            break;
        case 404:
            strcpy(status_text, "Not Found");
            break;
        case 500:
        default:
            strcpy(status_text, "Internal Server Error");
            break;
    }
    sprintf(header, HEADER_FMT, status, status_text, len, type);
}

void content_type(char *ct_type, char *uri) {
    char *ext = strrchr(uri, '.');
    if (!strcmp(ext, ".html"))
        strcpy(ct_type, "text/html");
    else if (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg"))
        strcpy(ct_type, "image/jpeg");
    else if (!strcmp(ext, ".png"))
        strcpy(ct_type, "image/png");
    else if (!strcmp(ext, ".css"))
        strcpy(ct_type, "text/css");
    else if (!strcmp(ext, ".js"))
        strcpy(ct_type, "text/javascript");
    else if(!strcmp(ext, ".mp3"))
         strcpy(ct_type,"audio/mp3");    
    else
        strcpy(ct_type, "text/plain");
}

void handle_404(int asock) {
    char header[BUF_SIZE];
    fill_header(header, 404, sizeof(NOT_FOUND_CONTENT), "text/html");

    write(asock, header, strlen(header));
    write(asock, NOT_FOUND_CONTENT, sizeof(NOT_FOUND_CONTENT));
}

void handle_500(int asock) {
    char header[BUF_SIZE];
    fill_header(header, 500, sizeof(SERVER_ERROR_CONTENT), "text/html");

    write(asock, header, strlen(header));
    write(asock, SERVER_ERROR_CONTENT, sizeof(SERVER_ERROR_CONTENT));
}

void *client_handler(void *arg) {
    int client_sock = *((int *)arg);
    free(arg);

    char header[BUF_SIZE];
    char buf[BUF_SIZE];

    if (read(client_sock, buf, BUF_SIZE) < 0) {
        perror("[ERR] Failed to read request.\n");
        handle_500(client_sock);
        close(client_sock);
        pthread_exit(NULL);
    }
    char *method = strtok(buf, " ");
    char *uri = strtok(NULL, " ");
    fprintf(stderr, "Connection open\n");
    
    if (method == NULL || uri == NULL) {
        perror("[ERR] Failed to identify method, URI.\n");
        handle_500(client_sock);
        close(client_sock);
        pthread_exit(NULL);
        fprintf(stderr, "Connection closed\n");
    }
    char safe_uri[BUF_SIZE];
    char *local_uri;
    struct stat st;

    strcpy(safe_uri, uri);
   
     local_uri = safe_uri + 1;
     if (stat(local_uri, &st) < 0) {
        handle_404(client_sock);
        close(client_sock);
        pthread_exit(NULL);
        fprintf(stderr, "Connection closed\n");
    }  
    int fd = open(local_uri, O_RDONLY);
    if (fd < 0) {
        handle_500(client_sock);
        close(client_sock);
        pthread_exit(NULL);
        fprintf(stderr, "Connection closed\n");
    }

    int ct_len = st.st_size;
    char ct_type[40];
    content_type(ct_type, local_uri);
    fill_header(header, 200, ct_len, ct_type);
    write(client_sock, header, strlen(header));

    int cnt;
    while ((cnt = read(fd, buf, BUF_SIZE)) > 0)
        write(client_sock, buf, cnt);

    fprintf(stderr, "Connection closed\n");
    close(client_sock);
    pthread_exit(NULL);
}

void error_handling(char *message){
	fputs(message,stderr);
	fputc('\n', stderr);
	return;
}
