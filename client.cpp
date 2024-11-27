#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

void die(const std::string& s) {
    std::cout << "Failure: " << s << "\n";
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);

    int rv = connect(fd, (const sockaddr *)&addr, sizeof(addr));

    char msg[] = "hello";
    write(fd, msg, strlen(msg));

    char rbuf[64] = {};
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        die("read()");
    }

    printf("server says: %s\n", rbuf);
    close(fd);

    return 0;
}
