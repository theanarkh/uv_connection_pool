build:
	gcc -o uv uv.cc uv_connection_pool.cc -I /usr/local/libuv/include -L/usr/local/libuv/lib -luv -g 