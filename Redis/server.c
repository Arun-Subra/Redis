#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/socket.h>
#include "zset.h"

#define MAX_MSG (32 << 20)
#define MAX_ARGS 200000
#define MAX_CONNS 1024

//Top-level command hash map
static struct HMap toplevel;

typedef struct {
    struct HNode hnode; // Hash node for lookup
    char *name; // Name of the command
    bool is_zset; // Whether this command is a ZSet command
    union {
        ZSet *zset; // Pointer to ZSet if this is a ZSet command
        char *cmd; // Pointer to command string if not a ZSet command
    };
} Command;

//Creates a new Command object
static Command *command_create(char *name, bool zset_flag) {
    Command *cmd = malloc(sizeof(Command));
    if (!cmd) {
        perror("malloc Command");
        return NULL;
    }

    cmd->hnode.hcode = hash(name, strlen(name));
    cmd->name = strdup(name);
    if (!cmd->name) {
        perror("strdup name");
        free(cmd);
        return NULL;
    }

    cmd->is_zset = zset_flag;
    if (zset_flag) {
        cmd->zset = malloc(sizeof(ZSet));
        if (!cmd->zset) {
            perror("malloc ZSet");
            free(cmd->name);
            free(cmd);
            return NULL;
        }
        zset_init(cmd->zset);
    } else {
        cmd->cmd = strdup(name);
        if (!cmd->cmd) {
            perror("strdup cmd");
            free(cmd->name);
            free(cmd);
            return NULL;
        }
    }

    return cmd;
}

//Frees a Command object
static void command_free(Command *cmd) {
    if (cmd->is_zset) {
        zset_clear(cmd->zset);
        free(cmd->zset);
    } else {
        free(cmd->cmd);
    }
    free(cmd->name);
    free(cmd);
}

//Compares two Command objects
static bool compare_commands(struct HNode *a, struct HNode *b) {
    Command *ca = container_of(a, Command, hnode);
    Command *cb = container_of(b, Command, hnode);
    return (ca->hnode.hcode == cb->hnode.hcode) && (strcmp(ca->name, cb->name) == 0);
}

//Finds a Command by name
static Command *find_command(char *keyname) {
    Command tmp = {0};
    tmp.hnode.hcode = hash(keyname, strlen(keyname));
    tmp.name         = (char *)keyname; 
    struct HNode *n  = hm_lookup(&toplevel, &tmp.hnode, compare_commands);
    return n ? container_of(n, Command, hnode)
             : NULL;
}

//Prints error
static void msg(char *why) {
    int e = errno;
    fprintf(stderr, "%s: [%d] %s\n", why, e, strerror(e));
}

//Prints error and exits
static void kill(char *msg) {
    fprintf(stderr, "%s: ", msg);
    perror(msg);
    exit(1);
}

//Represents a client connection
typedef struct {
    int fd;
    int read_;
    int write_;
    int close_;
    size_t incoming_len;
    size_t incoming_used;
    uint8_t *incoming_buf;
    size_t outgoing_len;
    size_t outgoing_used;
    uint8_t *outgoing_buf;
} Connection;

//Initializes a new connection
static void conn_init(Connection *conn) {
    conn->fd = -1;
    conn->read_ = false;
    conn->write_ = false;
    conn->close_ = false;
    conn->incoming_len = 1024;
    conn->incoming_used = 0;
    conn->incoming_buf = malloc(conn->incoming_len);
    if (!conn->incoming_buf) kill("malloc incoming_buf");
    conn->outgoing_len = 1024;
    conn->outgoing_used = 0;
    conn->outgoing_buf = malloc(conn->outgoing_len);
    if (!conn->outgoing_buf) kill("malloc outgoing_buf");
}

//Frees connection buffers
static void conn_free(Connection *conn) {
    free(conn->incoming_buf);
    free(conn->outgoing_buf);
}

//Closes and frees a connection
static void conn_close(Connection *conn, Connection **fd2conn) {
    close(conn->fd);
    fd2conn[conn->fd] = NULL;
    conn_free(conn);
    free(conn);
    fprintf(stderr, "Connection closed\n");
}

//Sets file descriptor to non-blocking
static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        kill("error in fcntl");
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        kill("error in fcntl");
    }
}

//Appends data to the outgoing buffer
static void buf_append(Connection *conn, uint8_t *data, size_t len) {
    while (conn->outgoing_used + len > conn->outgoing_len) {
        size_t new_len = conn->outgoing_len * 2;
        while (conn->outgoing_used + len > new_len) {
            new_len *= 2;
        }
        uint8_t *new_buf = realloc(conn->outgoing_buf, new_len);
        if (!new_buf) kill("realloc outgoing_buf");
        conn->outgoing_buf = new_buf;
        conn->outgoing_len = new_len;
    }
    memcpy(conn->outgoing_buf + conn->outgoing_used, data, len);
    conn->outgoing_used += len;
}

//Consumes bytes from incoming buffer
static void buf_consume(Connection *conn, size_t n) {
    memmove(conn->incoming_buf, conn->incoming_buf + n, conn->incoming_used - n);
    conn->incoming_used -= n;
}

//Accepts a new client connection
static Connection *accepter(int fd) {
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t a_len = sizeof(client_addr);
    int cfd = accept(fd, (struct sockaddr *)&client_addr, &a_len);
    if (cfd < 0) {
        msg("error in accepting socket");
        return NULL;
    }
    fprintf(stderr, "new client\n");

    fd_set_nb(cfd);

    Connection *conn = malloc(sizeof(Connection));
    if (!conn) {
        close(cfd);
        msg("malloc Connection");
        return NULL;
    }
    conn_init(conn);
    conn->fd = cfd;
    conn->read_ = true;
    return conn;
}

//Reads a 32-bit integer in network byte order
static bool read_u32(uint8_t **cur, uint8_t *end, uint32_t *out) {
    if (*cur + 4 > end) {
        return false;
    }
    uint32_t net;
    memcpy(&net, *cur, 4);
    *cur += 4;
    *out = ntohl(net); 
    return true;
}

//Reads a string of length n from buffer
static bool read_str(uint8_t **cur, uint8_t *end, size_t n, char **out) {
    if (*cur + n > end) {
        return false;
    }
    *out = malloc(n + 1);
    memcpy(*out, *cur, n);
    (*out)[n] = '\0';
    *cur += n;
    return true;
}

//Parses a request into string arguments
static int32_t parse_req(uint8_t *data, size_t size, char ***out, size_t *out_count) {
    uint8_t *end = data + size;
    uint8_t *cur = data;
    uint32_t nstr = 0;
    if (!read_u32(&cur, end, &nstr)) {
        return -1;
    }
    if (nstr > MAX_ARGS) {
        return -1;
    }

    *out = malloc(nstr * sizeof(char *));
    *out_count = 0;

    for (size_t i = 0; i < nstr; ++i) {
        uint32_t len = 0;
        if (!read_u32(&cur, end, &len)) {
            for (size_t j = 0; j < *out_count; ++j) {
                free((*out)[j]);
            }
            free(*out);
            return -1;
        }
        if (!read_str(&cur, end, len, &(*out)[*out_count])) {
            for (size_t j = 0; j < *out_count; ++j) {
                free((*out)[j]);
            }
            free(*out);
            return -1;
        }
        (*out_count)++;
    }
    if (cur != end) {
        for (size_t j = 0; j < *out_count; ++j) {
            free((*out)[j]);
        }
        free(*out);
        return -1;
    }
    return 0;
}

//Response status codes
enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

typedef struct {
    uint32_t status;
    size_t len;
    uint8_t *data;
} Response;

//Sets response to an error message
static void reply_err(Response *resp, const char *msg) {
    resp->status = RES_ERR;
    resp->len    = strlen(msg);
    resp->data   = malloc(resp->len);
    memcpy(resp->data, msg, resp->len);
}

//Frees response data
static void response_free(Response *resp) {
    free(resp->data);
}

//Handles GET command
static void handle_get(char **cmd, size_t cmd_count, Response *resp) {
    if (cmd_count != 2) {
        resp->status = RES_ERR;
        resp->len = 0;
        resp->data = NULL;
        return;
    }

    Command *cobj = find_command(cmd[1]);
    if (!cobj) {
        resp->status = RES_NX;
        resp->len = 0;
        resp->data = NULL;
        return;
    }

    if (cobj->is_zset) {
        reply_err(resp, "ERR key exists and is not a string");
        return;
    }

    char *val = cobj->cmd;
    resp->status = RES_OK;
    resp->len = strlen(val);
    resp->data = malloc(resp->len);
    if (!resp->data) {
        kill("malloc response data");
    }
    memcpy(resp->data, val, resp->len);
}

//Handles SET command
static void handle_set(char **cmd, size_t cmd_count, Response *resp) {
    if (cmd_count != 3) {
        resp->status = RES_ERR;
        resp->len    = 0;
        resp->data   = NULL;
        return;
    }

    Command *cmd_obj = find_command(cmd[1]);

    if (!cmd_obj) {
        cmd_obj = command_create(cmd[1], false);
        if (!cmd_obj) {
            reply_err(resp, "ERR internal allocation failure");
            return;
        }

        free(cmd_obj->cmd);
        cmd_obj->cmd = strdup(cmd[2]);
        if (!cmd_obj->cmd) {
            kill("strdup cmd");
        }
        hm_insert(&toplevel, &cmd_obj->hnode);
    } else {
        if (cmd_obj->is_zset) {
            reply_err(resp, "ERR key exists and is not a string");
            return;
        }
        free(cmd_obj->cmd);
        cmd_obj->cmd = strdup(cmd[2]);
        if (!cmd_obj->cmd) {
            kill("strdup cmd");
        }
    }

    resp->status = RES_OK;
    resp->len    = 0;
    resp->data   = NULL;
}

//Handles DELETE command
static void handle_delete(char **cmd, size_t cmd_count, Response *resp) {
    if (cmd_count != 2) {
        resp->status = RES_ERR;
        return;
    }

    Command *cobj = find_command(cmd[1]);
    if (!cobj) {
        resp->status = RES_NX;
        resp->len    = 0;
        resp->data   = NULL;
        return;
    }

    hm_delete(&toplevel, &cobj->hnode, compare_commands);
    command_free(cobj);

    resp->status = RES_OK;
    resp->len    = 0;
    resp->data   = NULL;
}



static void handle_zadd(char **cmd, size_t cmd_count, Response *resp) {
    if (cmd_count != 4) {
        reply_err(resp, "ERR wrong number of arguments for 'zadd'");
        return;
    }

    //Parse score
    errno = 0;
    char *endptr = NULL;
    double score = strtod(cmd[2], &endptr);
    if (errno != 0 || *endptr != '\0') {
        reply_err(resp, "ERR value is not a valid float");
        return;
    }

    //Lookup or create the ZSet
    Command *cobj = find_command(cmd[1]);
    if (cobj) {
        //If it exists, it must be a sorted set
        if (!cobj->is_zset) {
            reply_err(resp, "ERR key exists and is not a sorted set");
            return;
        }
    } else {
        cobj = command_create(cmd[1], true);
        if (!cobj) { reply_err(resp, "ERR internal allocation failure"); return; }
        hm_insert(&toplevel, &cobj->hnode);
    }
    
    //Insert into the ZSet
    if (!zset_insert(cobj->zset, cmd[3], strlen(cmd[3]), score)) {
        reply_err(resp, "ERR zadd failed");
        return;
    }
    resp->status = RES_OK;
    const char *ok = "OK";
    resp->len    = 2;
    resp->data   = malloc(2);
    memcpy(resp->data, ok, 2);
}

//Handle zrange command
static void handle_zrange(char **cmd, size_t cmd_count, Response *resp) {
    // Default to error until proven otherwise
    resp->status = RES_ERR;
    resp->len    = 0;
    resp->data   = NULL;

    // Expect exactly: zrange <key> <start> <end>
    if (cmd_count != 4) {
        return;
    }

    // Lookup key
    Command *cobj = find_command(cmd[1]);
    if (!cobj) {
        // key not found → nil
        resp->status = RES_NX;
        return;
    }
    if (!cobj->is_zset) {
        // wrong type
        reply_err(resp, "ERR key exists and is not a sorted set");
        return;
    }

    // Parse start
    char *endptr;
    errno = 0;
    int64_t start = strtoll(cmd[2], &endptr, 10);
    if (errno != 0 || *endptr != '\0') {
        reply_err(resp, "ERR value is not an integer or out of range");
        return;
    }

    // Parse end
    errno = 0;
    int64_t end = strtoll(cmd[3], &endptr, 10);
    if (errno != 0 || *endptr != '\0') {
        reply_err(resp, "ERR value is not an integer or out of range");
        return;
    }

    // Fetch the range
    size_t result_count = 0;
    ZNode **results = zset_range(cobj->zset, start, end, &result_count);
    if (!results || result_count == 0) {
        // Empty range is not an error
        if (results) free(results);
        resp->status = RES_OK;
        return;
    }

    size_t total_len = 4;
    for (size_t i = 0; i < result_count; i++) {
        total_len += 4 + results[i]->keylen;
    }

    uint8_t *data = malloc(total_len);
    if (!data) {
        free(results);
        return;
    }

    uint8_t *ptr = data;

    uint32_t cnt_net = htonl((uint32_t)result_count);
    memcpy(ptr, &cnt_net, 4);
    ptr += 4;

    for (size_t i = 0; i < result_count; i++) {
        uint32_t slen = htonl((uint32_t)results[i]->keylen);
        memcpy(ptr, &slen, 4);
        ptr += 4;
        memcpy(ptr, results[i]->key, results[i]->keylen);
        ptr += results[i]->keylen;
    }

    free(results);

    // Return successful response
    resp->status = RES_OK;
    resp->len    = total_len;
    resp->data   = data;
}

//Handle zremove command
static void handle_zremove(char **cmd, size_t cmd_count, Response *resp) {
    //zrem <key> <member>
    if (cmd_count != 3) {
        reply_err(resp, "ERR wrong number of arguments for 'zrem'");
        return;
    }

    // Lookup key
    Command *cobj = find_command(cmd[1]);
    if (!cobj) {
        resp->status = RES_NX;
        resp->len = 0;
        resp->data = NULL;
        return;
    }
    if (!cobj->is_zset) {
        //Wrong type
        reply_err(resp, "ERR key exists and is not a sorted set");
        return;
    }

    //Remove the member from the ZSet
    ZNode *zn = zset_lookup(cobj->zset, cmd[2], strlen(cmd[2]));
    if (!zn) {
        //Member not found → nil
        resp->status = RES_NX;
        resp->len = 0;
        resp->data = NULL;
        return;
    }
    printf("Removing member '%s' with score %.17g\n", zn->key, zn->score);

    zset_delete(cobj->zset, zn);

    resp->status = RES_OK;
    resp->len = 0;
    resp->data = NULL;
}

//Handle zscore command
static void handle_zscore(char **cmd, size_t cmd_count, Response *resp) {
    // Expect exactly: zscore <key> <member>
    if (cmd_count != 3) {
        reply_err(resp, "ERR wrong number of arguments for 'zscore'");
        return;
    }

    // Lookup key
    Command *cobj = find_command(cmd[1]);
    if (!cobj) {
        resp->status = RES_NX;
        resp->len = 0;
        resp->data = NULL;
        return;
    }
    if (!cobj->is_zset) {
        // wrong type
        reply_err(resp, "ERR key exists and is not a sorted set");
        return;
    }

    // Lookup the member in the ZSet
    ZNode *zn = zset_lookup(cobj->zset, cmd[2], strlen(cmd[2]));
    if (!zn) {
        resp->status = RES_NX;
        resp->len = 0;
        resp->data = NULL;
        return;
    }

    // Return the score
    char score_str[32];
    int len = snprintf(score_str, sizeof(score_str), "%.17g", zn->score);
    resp->status = RES_OK;
    resp->len = len;
    resp->data = malloc(len);
    memcpy(resp->data, score_str, len);
}

//Handle zcard command
static void handle_zcard(char **cmd, size_t cmd_count, Response *resp) {
    // Expect exactly: zcard <key>
    if (cmd_count != 2) {
        reply_err(resp, "ERR wrong number of arguments for 'zcard'");
        return;
    }

    // Lookup key
    Command *cobj = find_command(cmd[1]);
    if (!cobj) {
        resp->status = RES_NX;
        resp->len = 0;
        resp->data = NULL;
        return;
    }
    if (!cobj->is_zset) {
        // wrong type
        reply_err(resp, "ERR key exists and is not a sorted set");
        return;
    }

    // Return the cardinality of the ZSet
    uint32_t card = htonl((uint32_t)cobj->zset->root->count);
    resp->status = RES_OK;
    resp->len = 4;
    resp->data = malloc(4);
    memcpy(resp->data, &card, 4);
}

//Function pointer type for command handlers
typedef void (*command_handler_t)(char **cmd, size_t cmd_count, Response *resp);

//Struct for command name and handler mapping
typedef struct {
    char *name;
    command_handler_t handler;
} CommandEntry;

//Table of supported commands
static CommandEntry command_table[] = {
    {"get", handle_get},
    {"set", handle_set},
    {"del", handle_delete},
    {"zadd", handle_zadd},
    {"zrange", handle_zrange},
    {"zrem", handle_zremove},
    {"zscore", handle_zscore},
    {"zcard", handle_zcard},
};

//Macro for size of command table
#define COMMAND_TABLE_SIZE (sizeof(command_table) / sizeof(command_table[0]))

//Dispatch request to appropriate handler
static void do_request(char **cmd, size_t cmd_count, Response *resp) {
    if (cmd_count == 0) {
        resp->status = RES_ERR;
        resp->len = 0;
        resp->data = NULL;
        return;
    }
    for (size_t i = 0; i < COMMAND_TABLE_SIZE; ++i) {
        if (strcmp(cmd[0], command_table[i].name) == 0) {
            command_table[i].handler(cmd, cmd_count, resp);
            return;
        }
    }
    msg("unknown command");
    resp->status = RES_ERR;
    resp->len = 0;
    resp->data = NULL;
}

//Send response over connection
static void make_response(Response *resp, Connection *conn) {
    uint32_t resp_len = 8 + (uint32_t)resp->len;
    uint32_t resp_len_net = htonl(resp_len);
    buf_append(conn, (uint8_t *)&resp_len_net, 4);

    // status
    uint32_t status_net = htonl(resp->status);
    buf_append(conn, (uint8_t *)&status_net, 4);

    // data‐length
    uint32_t data_len_net = htonl((uint32_t)resp->len);
    buf_append(conn, (uint8_t *)&data_len_net, 4);

    // actual data (if any)
    if (resp->len > 0) {
        buf_append(conn, resp->data, resp->len);
    }
}


//Try processing one request from connection
static bool try_one_request(Connection *conn) {
    if (conn->incoming_used < 4) {
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, conn->incoming_buf, 4);
    len = ntohl(len);
    if (len > MAX_MSG) {
        msg("too long\n");
        conn->close_ = true;
        return false;
    }
    if (4 + len > conn->incoming_used) {
        return false;
    }

    char **cmd = NULL;
    size_t cmd_count = 0;
    if (parse_req(&conn->incoming_buf[4], len, &cmd, &cmd_count) < 0) {
        msg("bad request\n");
        conn->close_ = true;
        return false;
    }

    Response resp = {0};
    do_request(cmd, cmd_count, &resp);
    make_response(&resp, conn);
    for (size_t i = 0; i < cmd_count; ++i) {
        free(cmd[i]);
    }
    free(cmd);
    response_free(&resp);

    buf_consume(conn, 4 + len);
    return true;
}


//Try to write from outgoing buffer to client
static void conn_handle_write(Connection *conn) {
    ssize_t r = write(conn->fd, conn->outgoing_buf, conn->outgoing_used);
    if (r < 0 && errno == EAGAIN) {
        return;
    }
    if (r < 0) {
        msg("error in writing");
        conn->close_ = true;
        return;
    }

    memmove(conn->outgoing_buf, conn->outgoing_buf + r, conn->outgoing_used - r);
    conn->outgoing_used -= r;

    if (conn->outgoing_used == 0) {
        conn->read_ = true;
        conn->write_ = false;
    }
}

//Try to read from client into incoming buffer
static void conn_handle_read(Connection *conn) {
    if (conn->incoming_used >= conn->incoming_len) {
        conn->incoming_len *= 2;
        conn->incoming_buf = realloc(conn->incoming_buf, conn->incoming_len);
    }
    ssize_t r = read(conn->fd, conn->incoming_buf + conn->incoming_used, 
                      conn->incoming_len - conn->incoming_used);
    if (r < 0 && errno == EAGAIN) {
        return;
    }
    if (r < 0) {
        msg("error in reading");
        conn->close_ = true;
        return;
    }
    if (r == 0) {
        if (conn->incoming_used == 0) {
            msg("closed client");
        } else {
            msg("EOF");
        }
        conn->close_ = true;
        return;
    }

    conn->incoming_used += r;
    while (try_one_request(conn)) {}

    if (conn->outgoing_used > 0) {
        conn->read_ = false;
        conn->write_ = true;
        conn_handle_write(conn);
    }
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        kill("socket()");
    }
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int rv = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rv) {
        kill("bind()");
    }

    fd_set_nb(fd);

    rv = listen(fd, SOMAXCONN);
    if (rv) {
        kill("listen()");
    }

    hm_init(&toplevel);

    Connection **fd2conn = calloc(1024, sizeof(Connection *));
    struct pollfd *poll_args = malloc(1024 * sizeof(struct pollfd));

    while (true) {

        size_t poll_count = 0;
        poll_args[poll_count++] = (struct pollfd){fd, POLLIN, 0};

        for (int i = 0; i < 1024; ++i) {
            if (fd2conn[i] == NULL) {
                continue;
            }
            poll_args[poll_count] = (struct pollfd){fd2conn[i]->fd, POLLERR, 0};
            if (fd2conn[i]->read_) {
                poll_args[poll_count].events |= POLLIN;
            }
            if (fd2conn[i]->write_) {
                poll_args[poll_count].events |= POLLOUT;
            }
            poll_count++;
        }

        rv = poll(poll_args, poll_count, -1);
        if (rv < 0 && errno == EINTR) {
            continue;
        }
        if (rv < 0) {
            kill("poll");
        }
        if (poll_args[0].revents) {
            Connection *conn = accepter(fd);
            if (conn) {
                if (conn->fd >= MAX_CONNS) {
                    msg("too many clients");
                    conn_close(conn, fd2conn);
                } else {
                    fd2conn[conn->fd] = conn;
                }
            }
        }

        for (size_t i = 1; i < poll_count; ++i) {
            uint32_t ready = poll_args[i].revents;
            if (ready == 0) {
                continue;
            }
            Connection *conn = fd2conn[poll_args[i].fd];
            if (ready & POLLIN) conn_handle_read(conn);
            if (ready & POLLOUT) conn_handle_write(conn);
            if ((ready & POLLERR) || conn->close_) {
                conn_close(conn, fd2conn);
            }
        }
    }

    free(fd2conn);
    free(poll_args);
    return 0;
}