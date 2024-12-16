#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/event.h>
#include <sys/socket.h>

#include <cstdint>
#include <vector>

#include "utils.h"

static void connection_io(Conn *conn);
static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd);
static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn);
static bool try_flush_buffer(Conn *conn);
static void state_res(Conn *conn);
static bool try_one_request(Conn *conn, size_t &bytes_read);
static bool try_fill_buffer(Conn *conn);
static void state_req(Conn *conn);

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

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

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd)
{
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        msg("accept() error\n");
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

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static void connection_io(Conn *conn)
{
    if (conn->state == STATE_REQ) {
        state_req(conn);
    }
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn)) {
    }
}

static bool try_fill_buffer(Conn *conn)
{
    // try to fill buffer
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 &&
             errno == EINTR);  // signal delivered before time limit expired
    if (rv < 0 && errno == EAGAIN) {
        // got EAGAIN, stop
        return false;
    }
    if (rv < 0) {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }
    if (rv == 0) {
        if (conn->rbuf_size > 0) {
            msg("unexpected EOF");
        }
        else {
            msg("EOF");
        }
        conn->state = STATE_END;
        return false;
    }
    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));
    size_t bytes_read = 0;
    while (try_one_request(conn, bytes_read)) {
        std::cout << "bytes read: " << bytes_read << std::endl;
    }
    if (bytes_read) {
        size_t remain = conn->rbuf_size - bytes_read;
        if (remain) {
            memmove(conn->rbuf, &conn->rbuf[bytes_read], remain);
        }
        conn->rbuf_size = remain;
    }
    return (conn->state == STATE_REQ);
}

static bool try_one_request(Conn *conn, size_t &bytes_read)
{
    // try to parse a request from buffer
    if (conn->rbuf_size < 4) {
        // not enough data in buffer, retry on next iteration
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        // not enough data in buffer, retry on next iteration
        return false;
    }
    // Do something with client request
    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // Generate echo response
    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    bytes_read += len + 4;

    // remove request from buffer
    /*
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;
    */

    // Change state
    conn->state = STATE_RES;
    state_res(conn);

    // If request fully processed, continue outer loop
    return (conn->state == STATE_REQ);
}

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn)) {
    }
}

static bool try_flush_buffer(Conn *conn)
{
    ssize_t rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN) {
        // got EAGAIN, stop
        return false;
    }
    if (rv < 0) {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        // response fully sent, change state block
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    return true;
}
