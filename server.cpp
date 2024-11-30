#include "utils.h"

#include <poll.h>
#include <cstdint>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>


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

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        std::cout << "accept() error\n";
        return -1;
    }

    // set new connection fd to non-blocking
    fd_set_nb(connfd);
    // create Conn struct for new connection fd 
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn) {
        close(connfd);
        return -1;
    }
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
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

    // map of client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // set listen fd to nonblocking mode
    fd_set_nb(fd);

    // the event loop
    std::vector<struct pollfd> poll_args;
    while (true) {
        // reset arguments of poll()
        poll_args.clear();
        // listening fd is first
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        for (Conn *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            // For each fd connection, create the relevant poll() arg
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }
        // poll for active fds 
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0) {
            die("poll()");
        }

        // process active connections (skip the first listening fd)
        for (size_t i = 1; i < poll_args.size(); ++i) {
            if (poll_args[i].revents) {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END) {
                    // client closed normally, or something bad
                    // destroy connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        if (poll_args[0].revents) {
            (void)accept_new_conn(fd2conn, fd);
        }
    }
    return 0;
}
