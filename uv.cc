#include "uv_connection_pool.h"

uv_loop_t * loop = uv_default_loop();
struct pool* p;
int socket_id;
void on_write(int status) {
    printf("on_write %d \n", status);
}

void on_read(int nread, const uv_buf_t * buf) {
    if (nread == -1) {
        return;
    }
    printf("%d %s \n", nread, buf->base);
    printf("%s", get_socket_info(p, socket_id)->ctx);
    free(buf->base);
}

void onconnect(result ret, int id) {
    if (ret != OK) {
        printf("connect error: %d\n", ret);
        return;
    }
    socket_id = id;
    const char * data = "hello"; 
    write_socket(p, id, (char *)data, on_write);
    shutdown_socket(p, socket_id, NULL);
    read_socket(p, id, on_read);
    attach_ctx(p, id, (void *)data);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    const char * host = "127.0.0.1";
    struct pool_options options = {
        10,
        (char *)host,
        8000,
        1000,
        10
    };
    p = create_pool(loop, &options);
    wait_socket(p, onconnect, 10000);
    return uv_run(loop, UV_RUN_DEFAULT);
}
