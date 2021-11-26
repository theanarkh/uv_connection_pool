#include "uv_connection_pool.h"

uv_loop_t * loop = uv_default_loop();
struct pool* p;

void on_write(int status) {
    printf("on_write %d \n", status);
}


void on_read(int nread, const uv_buf_t * buf) {
    if (nread == -1) {
        return;
    }
    printf("%d %s \n", nread, buf->base);
    free(buf->base);
    // write_socket((uv_tcp_t *)data, "hello", on_write);
}

void onconnect(result ret, int id) {
    if (ret != OK) {
        printf("connect error");
        return;
    }
    write_socket(p, id, "hello", on_write);
    read_socket(p, id, on_read);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    struct pool_options options = {
        10,
        "127.0.0.1",
        8000,
        10,
        10
    };
    p = create_pool(loop, &options);
    int id = get_socket(p);
    if (id == -1) {
        wait_socket(p, onconnect, 1000);
    } else {
        onconnect(OK, id);
    }
    return uv_run(loop, UV_RUN_DEFAULT);
}
