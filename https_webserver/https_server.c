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
#include <openssl/ssl.h>
#include <openssl/err.h>

#define BUF_SIZE 1024
#define HEADER_FMT "HTTP/1.1 %d %s\nContent-Length: %ld\nContent-Type: %s\n\n"
#define NOT_FOUND_CONTENT       "<h1>404 Not Found</h1>\n"
#define SERVER_ERROR_CONTENT    "<h1>500 Internal Server Error</h1>\n"

#define CERT_FILE "pert.pem"
#define KEY_FILE "key.pem"
SSL_CTX *ctx;
void fill_header(char *header, int status, long len, char *type);
void error_handling(char *message);
void handle_404(SSL *ssl);
void handle_500(SSL *ssl);
void *client_handler(void *arg);
void accept_connection(struct sockaddr_in clnt_addr,int clnt_sock,socklen_t clnt_addr_size);
void bind_and_listen(int serv_sock, int backlog, int port);
void setupServerCtx();
int main(int argc, char *argv[]) {
    int port;
   int serv_sock;
   struct sockaddr_in clnt_addr;
   socklen_t clnt_addr_size;

   if(argc!=2){
           printf("Usage : %s <port>\n", argv[0]);
           return 0;
      }
   setupServerCtx();
   serv_sock = socket(AF_INET, SOCK_STREAM, 0);
   bind_and_listen(serv_sock, 20, atoi(argv[1]));
   accept_connection(clnt_addr,serv_sock,clnt_addr_size);
   return 0;
}
void setupServerCtx(){ //ctx,privatekey file,certificate file setting
	SSL_library_init();
	const SSL_METHOD *method;
	method = TLS_server_method();
	ctx = SSL_CTX_new(method);
	if(SSL_CTX_use_certificate_file(ctx, CERT_FILE, SSL_FILETYPE_PEM) != 1)
		error_handling("cert err");
	if(SSL_CTX_use_PrivateKey_file(ctx, KEY_FILE, SSL_FILETYPE_PEM)	!= 1)
		error_handling("private err");
}
void bind_and_listen(int serv_sock, int backlog, int port){ //socket bind and listen
	 struct sockaddr_in sin;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);
 	if(bind(serv_sock, (struct sockaddr *)&sin, sizeof(sin)) == -1)
    		error_handling("bind() error");
    	if(listen(serv_sock, backlog) == -1)
    		error_handling("listen() error");
}
void accept_connection(struct sockaddr_in clnt_addr,int clnt_sock,socklen_t clnt_addr_size){ //socket accept,create thread and created thread works client_handler

	 while (1) {
        int *serv_sock = malloc(sizeof(int));
        *serv_sock = accept(clnt_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);
        if (*serv_sock < 0) {
            perror("[ERR] failed to accept.\n");
            free(serv_sock);
            continue;
        }
         SSL *ssl = SSL_new(ctx);
      	SSL_set_fd(ssl,*serv_sock);
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, (void *)ssl) != 0) {
            perror("[ERR] failed to create thread.\n");
            free(serv_sock);
            SSL_free(ssl);
            
        }
    }
    close(clnt_sock);
}

void fill_header(char *header, int status, long len, char *type) { //header에 정보 입력
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

void content_type(char *ct_type, char *uri) { //읽고자하는 파일의 컨텐츠 타입 확인
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

void handle_404(SSL *ssl) { // 헤더 상태가 404일때
    char header[BUF_SIZE];
    fill_header(header, 404, sizeof(NOT_FOUND_CONTENT), "text/html");

    SSL_write(ssl, header, strlen(header));
    SSL_write(ssl, NOT_FOUND_CONTENT, sizeof(NOT_FOUND_CONTENT));
}

void handle_500(SSL *ssl) { // 헤더 상태가 500일때
    char header[BUF_SIZE];
    fill_header(header, 500, sizeof(SERVER_ERROR_CONTENT), "text/html");

    SSL_write(ssl, header, strlen(header));
    SSL_write(ssl, SERVER_ERROR_CONTENT, sizeof(SERVER_ERROR_CONTENT));
}

void *client_handler(void *arg) { //client로부터 요청이 들어오면
  
    char header[BUF_SIZE];
    char buf[BUF_SIZE];
   SSL *ssl = (SSL *) arg;

	if (SSL_accept(ssl) <= 0){
		SSL_free(ssl);
		return 0;
	}
   
   fprintf(stderr, "SSL Connection open\n");
    if (SSL_read(ssl, buf, BUF_SIZE) < 0) {
        perror("[ERR] Failed to read request.\n");
        handle_500(ssl);
        SSL_shutdown(ssl);
        fprintf(stderr, "SSL Connection closed\n");
    }

    char *method = strtok(buf, " ");
    char *uri = strtok(NULL, " ");
    if (method == NULL || uri == NULL) {
        perror("[ERR] Failed to identify method, URI.\n");
        handle_500(ssl);
        SSL_shutdown(ssl); 
        fprintf(stderr, "SSL Connection closed\n");
      }
    char safe_uri[BUF_SIZE];
    char *local_uri;
    struct stat st;

    strcpy(safe_uri, uri);
    local_uri = safe_uri + 1;
     if (stat(local_uri, &st) < 0) {
        handle_404(ssl);
        SSL_shutdown(ssl); 
        fprintf(stderr, "SSL Connection closed\n");
    }  
    int fd = open(local_uri, O_RDONLY);
    if (fd < 0) {
         handle_500(ssl);
        SSL_shutdown(ssl);
        fprintf(stderr, "SSL Connection closed\n"); 
       }

    int ct_len = st.st_size;
    char ct_type[40];
    content_type(ct_type, local_uri);
    fill_header(header, 200, ct_len, ct_type);
    SSL_write(ssl, header, strlen(header));

    int cnt;
    while ((cnt = read(fd, buf, BUF_SIZE)) > 0)
        SSL_write(ssl, buf, cnt);

   fprintf(stderr, "SSL Connection closed\n");
   SSL_shutdown(ssl);
}

void error_handling(char *message){
	fputs(message,stderr);
	fputc('\n', stderr);
	return;
}
