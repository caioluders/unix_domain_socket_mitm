#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#define BUFFER_SIZE 2048

// Unix Domain Socket Sniffer
// this script should automatize this answer https://superuser.com/a/576404
// and is heavily based on https://www.ibm.com/support/knowledgecenter/SSB23S_1.1.0.2020/gtpc1/unixsock.html
// by caioluders
char **largs;

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

	int server_sock, client_sock, spoofed_sock, len, rc;
	int bytes_rec = 0;
	struct sockaddr_un server_sockaddr;
	struct sockaddr_un client_sockaddr;
	struct sockaddr_un spoofed_sockaddr;

	signal(SIGHUP, signal_handler);
	signal(SIGKILL, signal_handler);
	signal(SIGSTOP, signal_handler);
	signal(SIGUSR1, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGABRT, signal_handler);

	char buf[2048];
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
	memset(buf, 0, BUFFER_SIZE);

	// rename current socket to .1
	char *socketName = (char*) malloc(strlen(argv[1]) + 3);
	strcpy(socketName, argv[1]);
	strcat(socketName, ".1");

	printf("%s\n", socketName);
	printf("%s\n", argv[1]);

	rename(argv[1], socketName);

	// create the new socket
	server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_sock == -1) {
		printf("SOCKET ERROR\n");
		exit(1);
	}

	server_sockaddr.sun_family = AF_UNIX;
	strcpy(server_sockaddr.sun_path, argv[1]);
	len = sizeof(server_sockaddr);

	unlink(argv[1]);

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

	// send to real socket
	spoofed_sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (spoofed_sock == -1) {
		printf("SOCKET ERROR\n");
		exit(-1);
	}

	spoofed_sockaddr.sun_family = AF_UNIX;
	snprintf(spoofed_sockaddr.sun_path, PATH_MAX - 1, socketName);

	client_sock = accept(server_sock, (struct sockaddr *) &client_sockaddr, &len);
	if (client_sock == -1) {
		printf("ACCEPT ERROR\n");
		close(client_sock);
		close(server_sock);
		exit(1);
	}

	rc = connect(spoofed_sock, (struct sockaddr *) &spoofed_sockaddr, sizeof(spoofed_sockaddr));
	if (rc == -1) {
		printf("CONNECT ERROR\n");
		close(server_sock);
		close(client_sock);
		close(spoofed_sock);
	}

	// while true ?
	while (1) {
		// accept
		

		// read, nginx -> me
		bytes_rec = recv(client_sock, buf, sizeof(buf), 0);
		if (bytes_rec == -1) {
			printf("RECV ERROR\n");
			close(client_sock);
			close(server_sock);
			exit(1);
		}

		printf("---- nginx-> me ----\n");

		for (int i = 0; i < BUFFER_SIZE; i++) {
			printf("%c", buf[i]);
		}

		printf("-------------------\n");


		// nginx -> me -> spoofed socket
		write(spoofed_sock, buf, strlen(buf) );

		printf("writed\n") ;

		memset(buf, 0, BUFFER_SIZE);
		// me < - spoofed socket
		bytes_rec = recv(spoofed_sock, buf, strlen(buf), 0);

		printf("received\n") ;

		for (int i = 0; i < BUFFER_SIZE; i++) {
			printf("%c",buf[i]);
		}

		// nginx < - me

		send(client_sock, buf, strlen(buf), 0);
		printf("writed again\n") ;
		memset(buf, 0, BUFFER_SIZE);
	}

	return 0;
}
