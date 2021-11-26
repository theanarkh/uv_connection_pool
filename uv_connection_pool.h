#ifndef UV_CONNECTION_POOL_H
#define UV_CONNECTION_POOL_H

#include <string.h>
#include <uv.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define CHECK(p, id) \
if (p->sockets[id] == NULL) { \
    return SOCKET_NOT_EXSIT; \
}

// code of call function
enum result {
    OK,
    TIMEOUT,
    NO_MEM,
    SOCKET_NOT_EXSIT,
    CONNECT_FAIL
};

typedef void (*get_socket_cb)(result state, int id);
typedef void (*write_socket_cb)(int status);
typedef void (*read_socket_cb)(int nread, const uv_buf_t* buf);
typedef void (*shutdown_socket_cb)(int status);
typedef void (*close_socket_cb)();

// state of connection
enum socket_state {
    UNCONNECTED,
    CONNECTING,
    CONNECTED,
    INUSED,
};

struct close_socket_ctx {
    struct socket_info * si;
    close_socket_cb cb;
};

// represent a item of waiting for connection in wait queue of pool
struct node {
    long timeout;
    get_socket_cb cb;
    struct node* next;
};

// represent a connection information
struct socket_info
{
    void * ctx;
    struct pool* p;
    uv_tcp_t* socket;
    socket_state state; 
    int index;
};

// pool
struct pool
{   
    void * ctx;
    uv_loop_t * loop;
    struct socket_info** sockets;
    int size;
    int use;
    char* host;
    int port;
    struct node* wait;
    uv_timer_t wait_timer;
    u_int64_t re_connection_interval;
    int max_re_connection_times;
    uv_timer_t re_connection_timer;
};

struct pool_options {
    int size;
    char* host;
    int port;
    u_int64_t re_connection_interval;
    int max_re_connection_times;
};

struct pool * create_pool(uv_loop_t * loop, struct pool_options * options);
struct socket_info * get_socket_info(struct pool * p, int id); 
int get_socket(struct pool * p);
int wait_socket(struct pool * p, get_socket_cb cb, uint64_t timeout);
int put_socket(struct pool *p, int id);
int close_socket(struct pool *p, int id, close_socket_cb cb);
int shutdown_socket(struct pool *p, int id, shutdown_socket_cb cb);
int read_socket(struct pool *p, int id, read_socket_cb cb);
int write_socket(struct pool* p, int id, char * data, write_socket_cb cb);
int attach_ctx(struct pool* p, int id, void * ctx);
int detach_ctx(struct pool* p, int id, void * ctx);

# endif
