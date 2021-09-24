#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <getopt.h>

#define BUFFER_SIZE 327684

// Unix Domain Socket MiTM
// this script should automatize this answer https://superuser.com/a/576404
// and is heavily based on https://www.ibm.com/support/knowledgecenter/SSB23S_1.1.0.2020/gtpc1/unixsock.html
// by @caioluders

char *largs;
char *socketName;
int server_sock, len, rc;
struct sockaddr_un spoofed_sockaddr;
struct winsize w;
char r1_str[1024];
char r2_str[1024];
int replace_i = 0;
char * socket_redirect ;
int redirect_flag = 0;
int static_file_flag = 0;
char socket_static_file[BUFFER_SIZE] ;


void *connection_handler(void *);
int print_full_width(char * s);

void signal_handler(int signo) {
	// If any signal is received the program will undo all changes
	unlink(largs);
	char *socketName = (char*) malloc(strlen(largs) + 3);
	strcpy(socketName, largs);
	strcat(socketName, ".1");
	rename(socketName, largs);
	exit(1);
}

int print_full_width(char * s) {
	// Beautiful print

	int len = strlen(s) ;

	printf("\n");
	for ( int i = 0 ; i < ((w.ws_col-len)/2)-1; i++ ) {
		putchar('-');
	}

	printf(" %s ",s);

	for ( int i = 0 ; i < ((w.ws_col-len)/2)-1 ; i++ ) {
		putchar('-');
	}
	printf("\n");
	return 0;
}

char * strreplace( char * buf , char * s1, char * s2 ) {
	char * s1_buf_pointer = strstr( buf , s1 ) ;

	printf("strstr %d",buf[0]);

	if ( s1_buf_pointer == NULL )
		return buf;
	
	int s1_buf_i = (int) ( s1_buf_pointer - buf );

	char *new_buf = malloc( sizeof(char) * BUFFER_SIZE );

	int j = 0 ;

	for ( int i = 0 ; i < BUFFER_SIZE; i++ ) {

		if ( buf[i-j] == '\0' ) {
			break;
		}

		if ( i == s1_buf_i ) {
			for ( j = 0; j < strlen(s2); j++ ) {
				new_buf[i+j] = s2[j]; 
			}
			i = i + (strlen(s2)-1) ;

		} else {
			if ( j > 0 ) {
				new_buf[i] = buf[s1_buf_i+strlen(s1)+(i-strlen(s2)-s1_buf_i)];	
			} else {
				new_buf[i] = buf[i];
			}
		}
	}

	return new_buf;
}

int main(int argc, char *argv[]){

	int client_sock, *new_sock;
	struct sockaddr_un server_sockaddr, client_sockaddr, peer_sockaddr;
	int backlog = 10;

	int c;
	int option_index = 0;

	static struct option long_options[] = {
		{"replace", required_argument, NULL, 'r'},
		{"proxy", required_argument, NULL, 'p'},
		{"static", required_argument, NULL, 's'},
		{"help", no_argument, NULL, 'h'},
		{ 0 }
	};

	signal(SIGHUP, signal_handler);
	signal(SIGKILL, signal_handler);
	signal(SIGSTOP, signal_handler);
	signal(SIGUSR1, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGPIPE, signal_handler);


	printf("Unix Domain Socket MiTM\n");
	printf("by @caioluders\n");
	

	if (argc <= 1) {
		perror("[!] Need socket name\n");
		exit(1);
	}

	printf("%s",argv[1]);

	if (access(argv[1], F_OK) != 0) {
		perror("[!] No such Socket");
		exit(1);
	}


	char * first_socket = (char*) malloc( strlen(argv[1])  + 3);
	strcpy( first_socket , argv[1] );

	while ( (c = getopt_long(argc, argv, "hr:s:p:", long_options, &option_index) ) != -1 ) {

		switch (c) {
			case 'h':
				const char *basename = strrchr(argv[0], '/');
				basename = basename ? basename + 1 : argv[0];

				printf("Usage: %s SOCKET [OPTION]\n",basename);
				printf("Unix Domain Socket tool to achieve local MiTM, can sniff and alter every connection on a given socket.\n\n");
				printf("	-r, --replace=STRING1 --replace=STRING2\tReplace STRING1 with STRING2 on EVERY response.\n");
				printf("	-p, --proxy=SOCKET\t\t\tProxy every request to a different socket.\n");
				printf("	-s, --static=FILENAME\t\t\tAnswer every request with a RAW response from a file.\n");
				printf("	-h, --help\t\t\t\tPrint this help and exit.\n");
				return 0;
			case 'p':
				printf("proxy option");
				if (optarg) {
					socket_redirect = (char*) malloc(strlen(optarg));
					strcpy(socket_redirect, optarg);
					redirect_flag = 1;
				}
					
				break;
			case 'r':
				if (optarg) {
					if ( replace_i == 0 ) {
						strcpy( r1_str, optarg );
						replace_i = 1;	
					} else {
						strcpy( r2_str, optarg );
						replace_i = 2;
					}
				}
				break;
			case 's':
				printf("static option");
				if (optarg) {
					if (access( optarg, F_OK) != 0){
						perror("[!] No such file!\n");
						exit(1);
					}
					static_file_flag = 1;
					FILE *fp = fopen(optarg, "r");
					fseek(fp, 0, SEEK_END);
					int file_size = ftell(fp);
					fseek(fp, 0, SEEK_SET);
					size_t newLen = fread(socket_static_file, sizeof(char),file_size, fp);
					fclose(fp);
					printf("%s\n",socket_static_file);
				}
				break;	
			default:
				printf("?? getopt returned character code 0%o ??\n", c);
		}

	}

	if ( replace_i == 1) {
		perror("[!] Needs second 'replace' option\n");
		exit(1);
	}

	largs = first_socket;

	memset(&spoofed_sockaddr, 0, sizeof(struct sockaddr_un));
	memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));
	memset(&client_sockaddr, 0, sizeof(struct sockaddr_un));

	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

	// rename current socket to socket.1
	char *socketName = (char*) malloc(strlen(first_socket) + 3);
	strcpy(socketName, first_socket);
	strcat(socketName, ".1\0");

	printf("[?] Renamed %s to %s\n", first_socket, socketName);

	rename(first_socket, socketName);

	// create new socket
	server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_sock == -1) {
		printf("SOCKET ERROR\n");
		exit(1);
	}

	spoofed_sockaddr.sun_family = AF_UNIX;
	strcpy(spoofed_sockaddr.sun_path, socketName);

	strcat(first_socket,"\0");
	server_sockaddr.sun_family = AF_UNIX;
	strcpy(server_sockaddr.sun_path, first_socket);
	len = sizeof(server_sockaddr);

	rc = bind(server_sock, (struct sockaddr *) &server_sockaddr, len);
	if (rc == -1) {
		printf("BIND ERROR\n");
		close(server_sock);
		exit(1);
	}

	printf("[?] Bind spoofed socket %s\n", first_socket);
	// listen
	rc = listen(server_sock, backlog);
	if (rc == -1) {
		printf("LISTEN ERROR\n");
		close(server_sock);
		exit(1);
	}
	printf("[?] Spoofed socket is listening...\n");


	while (  ( client_sock = accept(server_sock, (struct sockaddr *) &server_sockaddr, &len) ) ) {
		
		printf("[?] New connection\n");

		pthread_t sniffer_thread;
		new_sock = malloc(1) ;
		* new_sock = client_sock ;

		// new thread to handle the new connection
		if ( pthread_create( &sniffer_thread , NULL , connection_handler , (void*) new_sock ) < 0 ) {
			perror("[!] Could not create thread");
			return 1; 
		}


	}

	if ( client_sock < 0 ) {
		perror("[!] Accept failed");
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
	char * buf = (char*) malloc( sizeof(char) *BUFFER_SIZE);

	// send to real socket
	spoofed_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (spoofed_sock == -1) {
		printf("[!] SOCKET ERROR\n");
		exit(-1);
	}

	rc = connect(spoofed_sock, (struct sockaddr *) &spoofed_sockaddr, strlen(spoofed_sockaddr.sun_path) + sizeof(spoofed_sockaddr.sun_family));
	if (rc == -1) {
		printf("[!] CONNECT ERROR\n");
		close(spoofed_sock);
	}

	read_size = recv(sock, buf, BUFFER_SIZE, 0);

	print_full_width(largs);
	for (int i = 0; i < BUFFER_SIZE; i++) {
		printf("%c", buf[i]);
	}

	print_full_width("New Packet");

	send(spoofed_sock, buf, BUFFER_SIZE, 0);

	memset(buf, 0, BUFFER_SIZE);

	print_full_width(spoofed_sockaddr.sun_path);

	if ( static_file_flag == 1 ){
		strcpy( buf, socket_static_file );
	} else {
		bytes_rec = recv(spoofed_sock, buf, BUFFER_SIZE, 0 );
	}

	for (int i = 0; i < BUFFER_SIZE; i++) {
		printf("%c",buf[i]);
	}

	if ( replace_i == 2 ) {
		strcpy( buf, strreplace( buf , r1_str, r2_str ));
	}

	send(sock, buf, BUFFER_SIZE, 0);


	shutdown(sock,SHUT_RDWR);
	shutdown(spoofed_sock,SHUT_RDWR);
	
	free(sock_desc) ;
	return 0 ;
}
