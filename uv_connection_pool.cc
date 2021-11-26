

#include "uv_connection_pool.h"

static void wait_timer_cb(uv_timer_t* timer);
static void update_wait_timer(struct pool *p);
static void connect_to(struct socket_info* info, char * host, int port);
static void start_connect(struct pool* p);
static void write_cb(uv_write_t* req, int status);
static void re_connection_timer_cb(uv_timer_t* timer);
static void start_re_connection_timer(struct pool *p);
static void connect_cb(uv_connect_t* req, int status);
static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf);
static void read_cb(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf);

static void wait_timer_cb(uv_timer_t* timer) {
    struct pool * p = (struct pool *)timer->data;
    struct node * current = p->wait;
    uint64_t now = uv_now(p->loop);
    bool update = false;
    while(current) {
        if (current->timeout >= now) {
            p->wait = current->next;
            current->next = NULL;
            current->cb(TIMEOUT, -1);
            free(current);
            current = p->wait;
            update = true;
        }
    }
    if (update) {
        update_wait_timer(p);
    }
}

static void update_wait_timer(struct pool *p) {
    if (p->wait == NULL) {
        return;
    }
    u_int64_t now = uv_now(p->loop);
    uv_timer_stop(&p->wait_timer);
    uv_timer_start(&p->wait_timer, wait_timer_cb, p->wait->timeout - now, 0);
}

static void re_connection_timer_cb(uv_timer_t* timer) {
    struct pool * p = (struct pool *)timer->data;
    start_connect(p);
}

static void start_re_connection_timer(struct pool *p) {
    if (p->wait == NULL) {
        return;
    }
    u_int64_t now = uv_now(p->loop);
    uv_timer_stop(&p->re_connection_timer);
    uv_timer_start(&p->re_connection_timer, re_connection_timer_cb, p->re_connection_interval, 0);
}

// create a tcp connection pool with config options
struct pool * create_pool(uv_loop_t * loop, struct pool_options * options) {
    struct pool * p = (struct pool *)malloc(sizeof(pool));
    if (p == NULL) {
        return NULL;
    }
    p->loop = loop;
    p->size = options->size;
    p->use = 0;
    p->host = options->host;
    p->port = options->port;
    p->wait = NULL;
    p->re_connection_interval = options->re_connection_interval ? options->re_connection_interval : 1000;
    p->max_re_connection_times = options->max_re_connection_times ? options->max_re_connection_times : 10;
    uv_timer_init(p->loop, &p->wait_timer);
    uv_timer_init(p->loop, &p->re_connection_timer);
    p->wait_timer.data = (void *)p;
    p->re_connection_timer.data = (void *)p;
    int socket_info_mem_size = sizeof(struct socketInfo *) * options->size;
    p->sockets = (struct socket_info **)malloc(socket_info_mem_size);
    if (p->sockets == NULL) {
        free(p);
        return NULL;
    }
    memset(p->sockets, 0, socket_info_mem_size);
    return p;
}

// get a tcp connection from connection pool
int get_socket(struct pool * p) {
    // traverse and find a idle connection
    for (int i = 0; i < p->size; i++) {
        // if the socket is idle, set it to use state, then return it
        if (p->sockets[i] != NULL && p->sockets[i]->state == CONNECTED) {
            p->sockets[i]->state = INUSED;
            return i;
        }
    }
    return -1;
}


static void connect_cb(uv_connect_t* req, int status) {
    struct socket_info* si = (struct socket_info*)req->handle->data;
    struct pool * p = si->p;
    if (status == 0) {
        si->state = CONNECTED;
        struct node * n = p->wait;
        if (n) {
            p->wait = n->next;
            n->cb(OK, si->index);
            free(n);
            update_wait_timer(p);
        }
    } else {
        p->sockets[si->index] = NULL;
        free(si->socket);
        free(si);
        if (p->max_re_connection_times > 0) {
            p->max_re_connection_times--;
            start_connect(p);
        }
    }
    free(req);
}

static void start_connect(struct pool* p) {
    for (int i = 0; i < p->size; i++) {
        if (p->sockets[i] == NULL) {
            struct sockaddr_in dest;
            struct socket_info * si;
            uv_tcp_t* socket;
            uv_connect_t* connect;
            si = (struct socket_info *)malloc(sizeof(struct socket_info *));
            if (si == NULL) {
                goto NO_MEM_ERROR;
            }
            socket = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
            if (socket == NULL) {
                goto NO_MEM_ERROR;
            }
            connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));
            if (connect == NULL) {
                goto NO_MEM_ERROR;
            }
            socket->data = (void *)si;
            uv_tcp_init(p->loop, socket);
            si->socket = socket;
            si->p = p;
            si->index = i;
            si->state = CONNECTING;
            p->sockets[i] = si;
            uv_ip4_addr(p->host, p->port, &dest);
            uv_tcp_connect(connect, socket, (const struct sockaddr*)&dest, connect_cb); 
            return;

        NO_MEM_ERROR:
            if (si) {
                free(si);
            }
            if (socket) {
                free(socket);
            }
            start_re_connection_timer(p);
            return;
        }
    }
}

// wait for a idle connection in wait queue of pool
int wait_socket(struct pool * p, get_socket_cb cb, uint64_t timeout) {
    struct node * n = (struct node *)malloc(sizeof(struct node *));
    if (n == NULL) {
        return NO_MEM;
    }
    uint64_t now = uv_now(p->loop);
    n->cb = cb;
    n->timeout = timeout + now;
    if (p->wait == NULL) {
        n->next = NULL;
        p->wait = n;
        update_wait_timer(p);
    } else {
        struct node * current = p->wait;
        struct node * prev = NULL;
        do {
            if (n->timeout < current->timeout) {
                break;
            }
            prev = current;
            current = current->next;
        } while(current);
        n->next = current;
        prev->next = n;
        if (prev == NULL) {
            update_wait_timer(p);
        }
    }
    start_connect(p);
    return OK;
}

// put socket when you don not need it
int put_socket(struct pool *p, int id) {
    CHECK(p, id);
    p->sockets[id]->state = CONNECTED;
    return OK;
}

static void close_cb(uv_handle_t* handle) {
    struct close_socket_ctx * ctx = (struct close_socket_ctx *)handle->data;
    struct socket_info * si = ctx->si;
    close_socket_cb cb = ctx->cb;
    si->p->sockets[si->index] = NULL;
    free(si->socket);
    free(handle);
    free(si);
    free(ctx);
    if (cb) {
        cb();
    }
}

// close socket
int close_socket(struct pool *p, int id, close_socket_cb cb) {
    CHECK(p, id);
    struct socket_info* s = p->sockets[id];
    struct close_socket_ctx * ctx = (struct close_socket_ctx *)malloc(sizeof(struct close_socket_ctx));
    ctx->cb = cb;
    ctx->si = s;
    s->socket->data = (void*)ctx;
    uv_close((uv_handle_t*)s->socket, close_cb);
    return OK;
}

void shutdown_cb(uv_shutdown_t *req, int status) {
    shutdown_socket_cb cb = (shutdown_socket_cb)req->data;
    free(req);
    if (cb) {
        cb(status);
    }
}

int shutdown_socket(struct pool *p, int id, shutdown_socket_cb cb) {
    CHECK(p, id);
    struct socket_info* s = p->sockets[id];
    uv_shutdown_t* req = (uv_shutdown_t*)malloc(sizeof(uv_shutdown_t));
    if (req == NULL) {
        return NO_MEM;
    }
    req->data = (void *)cb;
    return uv_shutdown(req, (uv_stream_t*)s->socket, shutdown_cb);
}

static void alloc_cb(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    buf->base = (char *)malloc(size);
    buf->len = size;
}

static void read_cb(uv_stream_t* tcp, ssize_t nread, const uv_buf_t* buf) {
    read_socket_cb cb = (read_socket_cb)tcp->data;
    if (cb) {
        tcp->data = NULL;
        cb(nread, buf);
    }
}

int read_socket(struct pool *p, int id, read_socket_cb cb) {
    CHECK(p, id);
    struct socket_info* s = p->sockets[id];
    s->socket->data = (void *)cb;
    uv_read_start((uv_stream_t *)s->socket, alloc_cb, read_cb);
}

static void write_cb(uv_write_t* req, int status) {
    write_socket_cb cb = (write_socket_cb)req->data;
    free(req);
    if (cb) {
        cb(status);
    }
}

int write_socket(struct pool* p, int id, char * data, write_socket_cb cb) {
    CHECK(p, id);
    struct socket_info* s = p->sockets[id];
    uv_buf_t buf = uv_buf_init(data, strlen(data));
    uv_write_t * write_req = (uv_write_t *)malloc(sizeof(uv_write_t));
    if (write_req == NULL) {
        return NO_MEM;
    }
    write_req->data = (void *)cb;
    uv_write(write_req, (uv_stream_t *)s->socket, &buf, 1, write_cb);
}

int attach_ctx(struct pool* p, int id, void * ctx) {
    CHECK(p, id);
    struct socket_info* s = p->sockets[id];
    s->ctx = ctx;
    return OK;
}

int detach_ctx(struct pool* p, int id, void * ctx) {
    CHECK(p, id);
    struct socket_info* s = p->sockets[id];
    s->ctx = NULL;
    return OK;
}


