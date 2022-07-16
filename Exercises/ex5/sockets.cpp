#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <cstring>

#define MAX_CLIENTS 5
#define MAXHOSTNAME 256
#define SEND_DATA 1
#define READ_DATA 0

char error_msg1[] = "system error: unable to get host by name server";
char error_msg2[] = "system error: unable to open server socket";
char error_msg3[] = "system error: unable to bind server socket";
char error_msg4[] = "system error: unable to accept connection";
char error_msg5[] = "system error: unable to get host by name client";
char error_msg6[] = "system error: unable to open client socket";
char error_msg7[] = "system error: unable to connect to client socket";
char error_msg8[] = "system error: unable to send/read bytes";

/**
 * Helper function for establishing a server
 * @param port_num port number
 * @return socket fd
 */
int establish(unsigned short port_num) {
    char myname[MAXHOSTNAME + 1];
    int s;
    struct sockaddr_in sa{};
    struct hostent *hp;

    // hostnet initialization
    gethostname(myname, MAXHOSTNAME);
    hp = gethostbyname(myname);
    if (hp == nullptr) {
        fprintf(stderr, "%s\n", error_msg1);
        exit(1);
    }

    //sockaddrr_in initlization
    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = hp->h_addrtype;
    /* this is our host address */
    memcpy(&sa.sin_addr, hp->h_addr, hp->h_length);
    /* this is our port number */
    sa.sin_port = htons((u_short)port_num);
    /* create socket */
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "%s\n", error_msg2);
        exit(1);
    }
    if (bind(s, (struct sockaddr *) &sa, sizeof(struct sockaddr_in)) < 0) {
        close(s);
        fprintf(stderr, "%s\n", error_msg3);
        exit(1);
    }
    listen(s, MAX_CLIENTS); /* max # of queued connects */
    return s;
}

/**
 * Wait for a connection to occur on a socket created
 * with establish
 * @param s socket
 * @return new socket descriptor
 */
int get_connection(int s) {
    int t; /* socket of connection */

    if ((t = accept(s, nullptr, nullptr)) < 0) {
        fprintf(stderr, "%s\n", error_msg4);
        exit(1);
    }
    return t;
}

/**
 * Helper function that initializes the socket
 * of a client
 * @param hostname hostname
 * @param port_num portnum
 * @return socket fd
 */
int call_socket(char *hostname, unsigned short port_num) {
    struct sockaddr_in sa{};
    struct hostent *hp;
    int s;

    if ((hp = gethostbyname(hostname)) == nullptr) {
        fprintf(stderr, "%s\n", error_msg5);
        exit(1);
    }

    memset(&sa, 0, sizeof(sa));
    memcpy((char *) &sa.sin_addr, hp->h_addr, hp->h_length);
    sa.sin_family = hp->h_addrtype;
    sa.sin_port = htons((u_short) port_num);

    if ((s = socket(hp->h_addrtype, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "%s\n", error_msg6);
        exit(1);
    }

    if (connect(s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        close(s);
        fprintf(stderr, "%s\n", error_msg7);
        exit(1);
    }
    return s;
}

/**
 * Helper function for sending and reading data
 * between server and client
 * @param s socket
 * @param buf buffer to read to/from
 * @param n num of bytes
 * @param send_or_read 0 for reading 1 for sending
 * @return
 */
int send_read_data(int s, char *buf, int n, int send_or_read) {
    int bcount;       /* counts bytes read or send */
    int br;               /* bytes read or send this pass */
    bcount = 0;
    while (bcount < n) { /* loop until full buffer */
        if (send_or_read == READ_DATA) {
            br = (int) (read(s, buf, (n - bcount)));
        } else { // send_or_read == SEND_DATA
            br = (int) (send(s, buf, n - bcount, 0));
        }
        if (br > 0) {
            bcount += br;
            buf += br;
        }
        if (br < 1) {
            fprintf(stderr, "%s\n", error_msg8);
            exit(1);
        }
    }
    return bcount;
}

/**
 * Main function that distinguishes between server/client
 * and operates them.
 * @param argc num of arguments
 * @param argv arguments
 * @return 0
 */
int main(int argc, char *argv[]) {
    char buffer[MAXHOSTNAME] = {0};
    unsigned short port_num = strtoul(argv[2], nullptr, 0);

    if (argc == 3) {
        // server:
        int sock = establish(port_num);

        while (true) {
            int client_fd = get_connection(sock);
            send_read_data(client_fd, buffer, MAXHOSTNAME, READ_DATA);
            system(buffer);
            close(client_fd);
            for (int i = 0; i < MAXHOSTNAME; ++i) {
                buffer[i] = 0;
            }
        }
    } else {
        // client:
        char myname[MAXHOSTNAME + 1];
        gethostname(myname, MAXHOSTNAME);
        int sock = call_socket(myname, port_num);
        int argv_ind = 3;
        int i = 0;
        while (argv_ind < argc) {
            for (int j = 0; j < strlen(argv[argv_ind]); ++j) {
                buffer[i++] = *(argv[argv_ind] + j);
            }
            buffer[i++] = *" ";
            argv_ind++;
        }
        send_read_data(sock, buffer, MAXHOSTNAME, SEND_DATA);

    }
    return 0;
}

