#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "utils.h"

static int32_t one_request(int connfd)
{
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            std::cout << "EOF\n";
        }
        else {
            die("read()");
        }
        return err;
    }
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // little endian
    if (len > k_max_msg) {
        die("too long");
        return -1;
    }
    // request body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        die("read()");
        return err;
    }
    // do something
    rbuf[4 + len] = '\0';
    std::cout << "client says: " << &rbuf[4] << '\n';
    // reply with same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}

int main()
{
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
    rv = listen(fd, SOMAXCONN);  // backlog arg = size of queue
    if (rv) {
        die("listen()");
    }

    // Print out server's port
    struct sockaddr_in a;
    socklen_t len = sizeof(a);
    getsockname(fd, (sockaddr *)&a, &len);
    printf("Server address: %s.%d\n", inet_ntoa(a.sin_addr), htons(a.sin_port));

    // accept connections
    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) {
            continue;  // error
        }
        // only serve one client connection at once
        while (true) {
            int32_t err = one_request(connfd);
            if (err) {
                break;
            }
        }
        close(connfd);
    }
}
