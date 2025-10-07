#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


//Fatal error handler that prints message and exits
static void kill(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

//Read exactly n bytes from socket into buffer
static int32_t read_all(int sock, char *buffer, size_t n) {
    while (n > 0) {
        ssize_t r = read(sock, buffer, n);
        if (r <= 0) {
        return -1;
        }
        n -= (size_t) r;
    buffer += r;
    }
    return 0;
}

//Write exactly n bytes from buffer to socket
static int32_t write_all(int sock, char *buffer, size_t n) {
    while (n > 0) {
        ssize_t r = write(sock, buffer, n);
        if (r <= 0) {
            return -1;
        }
        n -= (size_t) r;
        buffer += r;
    }
    return 0;
}

const size_t max_msg = 4096;

//Encode and send a command array to the server
static int32_t send_req(int sock, char **cmd, int cmd_count) {
    uint32_t len = 4;
    for (int i = 0; i < cmd_count; i++) { // post increment
        len += 4 + strlen(cmd[i]);
    }
    if (len > max_msg) {
        return -1;
    }

    char *wbuf = malloc(4 + max_msg);
    if (!wbuf) kill("malloc");

    uint32_t len_net = htonl(len);
    memcpy(wbuf, &len_net, 4);
    
    uint32_t n_net = htonl((uint32_t)cmd_count);
    memcpy(wbuf + 4, &n_net, 4);
    size_t cur = 8;
    for (int i = 0; i < cmd_count; ++i) {
        uint32_t p = (uint32_t)strlen(cmd[i]);
        uint32_t p_net = htonl(p);
        memcpy(wbuf + cur, &p_net, 4);
        memcpy(wbuf + cur + 4, cmd[i], p);
        cur += 4 + p;
    }

    int32_t rv = write_all(sock, wbuf, 4 + len);
    free(wbuf);
    return rv;
}

#include <ctype.h>  // for isprint

bool is_printable_ascii(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        // Allow printable characters plus space
        if (!isprint(data[i])) {
            return false;
        }
    }
    return true;
}


//Read and decode a response from the server
// Read and decode a response from the server
static int32_t read_res(int sock) {
    enum { HDR_LEN = 4, STATUS_LEN = 4, DLEN_LEN = 4 };
    static const size_t MAX_BUF = HDR_LEN + max_msg;
    uint8_t  rbuf[MAX_BUF];
    uint32_t netlen, len;

    // Read outer length
    if (read_all(sock, (char*)rbuf, HDR_LEN) < 0) {
        perror("read length");
        return -1;
    }
    memcpy(&netlen, rbuf, HDR_LEN);
    len = ntohl(netlen);

    if (len < STATUS_LEN + DLEN_LEN || len > max_msg) {
        fprintf(stderr, "invalid response length %u\n", len);
        return -1;
    }

    // Read payload (status + data_len + payload)
    if (read_all(sock, (char*)rbuf + HDR_LEN, len) < 0) {
        perror("read payload");
        return -1;
    }

    uint32_t netstatus, status;
    memcpy(&netstatus, rbuf + HDR_LEN, STATUS_LEN);
    status = ntohl(netstatus);

    uint32_t netdlen, dlen;
    memcpy(&netdlen, rbuf + HDR_LEN + STATUS_LEN, DLEN_LEN);
    dlen = ntohl(netdlen);

    if (dlen > len - (STATUS_LEN + DLEN_LEN)) {
        fprintf(stderr, "invalid data length %u\n", dlen);
        return -1;
    }

    uint8_t *payload = rbuf + HDR_LEN + STATUS_LEN + DLEN_LEN;

    // Detect if payload is multi-bulk response
    bool is_multi = false;
    uint32_t count = 0;
    if (dlen >= 4) {
        uint32_t netcount;
        memcpy(&netcount, payload, 4);
        count = ntohl(netcount);

        size_t expected = 4;
        uint8_t *p = payload + 4;
        bool ok = true;

        for (uint32_t i = 0; i < count; i++) {
            if ((size_t)(p - payload) + 4 > dlen) { ok = false; break; }
            uint32_t netel, elen;
            memcpy(&netel, p, 4);
            elen = ntohl(netel);
            if ((size_t)(p - payload) + 4 + elen > dlen) { ok = false; break; }
            p += 4 + elen;
            expected += 4 + elen;
        }

        if (ok && expected == dlen) {
            is_multi = true;
        }
    }

    printf("server says: [%u]", status);

    if (is_multi) {
        uint8_t *p = payload + 4;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t netel, elen;
            memcpy(&netel, p, 4);
            elen = ntohl(netel);
            p += 4;
            printf(" %.*s", elen, p);
            p += elen;
        }
        printf("\n");
    } else {
        if (dlen == 4 && !is_printable_ascii(payload, dlen)) {
            // Probably an integer or binary data, print as uint32_t
            uint32_t netval;
            memcpy(&netval, payload, 4);
            uint32_t val = ntohl(netval);
            printf(" %u\n", val);
        } else if (dlen > 0) {
            // Printable ASCII string
            printf(" %.*s\n", dlen, payload);
        } else {
            printf("\n");
        }
    }

    return 0;
}


int main(int argc, char **argv) {
    //Open TCP socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        kill("socket()");
    }

    //Connect to 127.0.0.1:1234
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int r = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (r) kill("connect");

    //Prepare argv into command list
    int cmd_count = argc - 1;
    char **cmd = NULL;
    if (cmd_count > 0) {
        cmd = malloc(cmd_count * sizeof(char *));
        if (!cmd) kill("malloc");

        for (int i = 0; i < cmd_count; ++i) {
            cmd[i] = argv[i + 1];
        }
    }

    //Send request and receive response
    int32_t err = send_req(fd, cmd, cmd_count);
    
    if (!err) err = read_res(fd);
    if (cmd) free(cmd);

    close(fd);
    return 0;
}