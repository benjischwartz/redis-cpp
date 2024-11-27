#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

void die(const std::string& s) {
    std::cout << "Failure: " << s << "\n";
}

static void respond(int connfd) {
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        die("read()");
        return;
    }
    printf("client says: %s\n", rbuf);
    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf));
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // 0.0.0.0:1234
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr)); 
    if (rv) {
        die("bind()");
    }

    // listen
    rv = listen(fd, SOMAXCONN);     // backlog arg = size of queue
    if (rv) {
        die("listen()");
    }

    // Print out server's port
    struct sockaddr_in a;
    socklen_t len = sizeof(a);
    getsockname(fd, (sockaddr *)&a, &len);
    printf("Server address: %s.%d\n", inet_ntoa (a.sin_addr), htons(a.sin_port));

    // accept connections
    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) {
            continue;
        }
        respond(connfd);
        close(connfd);
    }
}
