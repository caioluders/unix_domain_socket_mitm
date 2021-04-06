#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#define BUFFER_SIZE 2048

// Unix Domain Socket Sniffer
// this script should automatize this answer https://superuser.com/a/576404
// and is heavily based on https://www.ibm.com/support/knowledgecenter/SSB23S_1.1.0.2020/gtpc1/unixsock.html
// by @caioluders

char **largs;
char *socketName;
int server_sock, len, rc;
struct sockaddr_un spoofed_sockaddr;

void *connection_handler(void *);

void signal_handler(int signo) {
	// If any signal is received the program will undo all changes
	unlink(largs[1]);
	char *socketName = (char*) malloc(strlen(largs[1]) + 3);
	strcpy(socketName, largs[1]);
	strcat(socketName, ".1");
	rename(socketName, largs[1]);
	exit(1);
}

int main(int argc, char *argv[]){

	int client_sock, *new_sock;
	struct sockaddr_un server_sockaddr, client_sockaddr, peer_sockaddr;

	signal(SIGHUP, signal_handler);
	signal(SIGKILL, signal_handler);
	signal(SIGSTOP, signal_handler);
	signal(SIGUSR1, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGPIPE, signal_handler);

	int backlog = 10;

	if (argc <= 1) {
		perror("Need filename\n");
		exit(1);
	}

	if (access(argv[1], F_OK) != 0) {
		perror("");
		exit(1);
	}

	largs = argv;

	memset(&spoofed_sockaddr, 0, sizeof(struct sockaddr_un));
	memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));
	memset(&client_sockaddr, 0, sizeof(struct sockaddr_un));

	printf("Unix Domain Socket Sniffer\n");
	printf("by @caioluders\n");

	// rename current socket to socket.1
	char *socketName = (char*) malloc(strlen(argv[1]) + 3);
	strcpy(socketName, argv[1]);
	strcat(socketName, ".1\0");

	printf("%s\n", socketName);
	printf("%s\n", argv[1]);

	rename(argv[1], socketName);

	// create new socket
	server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_sock == -1) {
		printf("SOCKET ERROR\n");
		exit(1);
	}

	spoofed_sockaddr.sun_family = AF_UNIX;
	strcpy(spoofed_sockaddr.sun_path, socketName);

	strcat(argv[1],"\0");
	server_sockaddr.sun_family = AF_UNIX;
	strcpy(server_sockaddr.sun_path, argv[1]);
	len = sizeof(server_sockaddr);

	rc = bind(server_sock, (struct sockaddr *) &server_sockaddr, len);
	if (rc == -1) {
		printf("BIND ERROR\n");
		close(server_sock);
		exit(1);
	}

	// listen
	rc = listen(server_sock, backlog);
	if (rc == -1) {
		printf("LISTEN ERROR\n");
		close(server_sock);
		exit(1);
	}
	printf("socket listening...\n");


	while (  ( client_sock = accept(server_sock, (struct sockaddr *) &server_sockaddr, &len) ) ) {
		
		printf("new connection\n");

		pthread_t sniffer_thread;
		new_sock = malloc(1) ;
		* new_sock = client_sock ;

		// new thread to handle the new connection
		if ( pthread_create( &sniffer_thread , NULL , connection_handler , (void*) new_sock ) < 0 ) {
			perror("could not create thread");
			return 1; 
		}


	}

	if ( client_sock < 0 ) {
		perror("accept failed");
		return 1;
	}


	return 0;
}

void * connection_handler(void * sock_desc) {
	// thread 
	int sock = * (int*) sock_desc ;
	int read_size, rs2, spoofed_sock ;
	int bytes_rec = 0;
	char * message, client_message[BUFFER_SIZE] ;
	char buf[2048];

	// send to real socket
	spoofed_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (spoofed_sock == -1) {
		printf("SOCKET ERROR\n");
		exit(-1);
	}

	rc = connect(spoofed_sock, (struct sockaddr *) &spoofed_sockaddr, strlen(spoofed_sockaddr.sun_path) + sizeof(spoofed_sockaddr.sun_family));
	if (rc == -1) {
		printf("CONNECT ERROR\n");
		close(spoofed_sock);
	}

	read_size = recv(sock, buf, BUFFER_SIZE,0 );

	printf("---- nginx -> me ----\n");
	for (int i = 0; i < BUFFER_SIZE; i++) {
		printf("%c", buf[i]);
	}

	printf("-------------------\n");

	send(spoofed_sock, buf, BUFFER_SIZE, 0);

	// nginx -> me -> spoofed socket

	printf("writed\n") ;

	memset(buf, 0, BUFFER_SIZE);

	printf("received\n") ;
	printf("---- me <- php-pfm ----\n");
	// me < - spoofed socket
	printf("---- nginx <- me ----\n");
	printf("writed again\n") ;

	bytes_rec = recv(spoofed_sock, buf, BUFFER_SIZE, 0 );
	for (int i = 0; i < BUFFER_SIZE; i++) {
		printf("%c",buf[i]);
	}
	send(sock, buf, BUFFER_SIZE, 0);

	shutdown(sock,SHUT_RDWR);
	shutdown(spoofed_sock,SHUT_RDWR);
	
	free(sock_desc) ;
	return 0 ;
}
