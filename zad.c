#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <uv.h>
#include <time.h>

/*
    Written by: Alen Jelavic      
    Description: This program monitors a file, displays the contents of the file to a terminal
        when a timer runs out, saves the timestamps when the file has been modified to a log file,
        fills the file with the information from TCP or UDP packets from 2 different ports.
    Known problems: I couldn't get the files to open in O_APPEND, thus the contents is always rewritten. 
            When the main file is edited manually, the polling callback functio is called two times no matter
            what event_flag is used and causes a segmentation fault. I have tried putting a mutex there, but it looked up. 
*/

//parameters
#define TCP_IP "127.0.0.1"
#define TCP_PORT 8000
#define DEFAULT_BACKLOG 128
#define UDP_IP "127.0.0.1"
#define UDP_PORT 8001
#define TIMER 5000
#define MAIN_FILE "zad_x"
#define LOG_FILE "zad_x.log"

//global variables
uv_loop_t *loop;

uv_timer_t show_user;

uv_mutex_t mutex;

uv_fs_t open_req;
uv_fs_t open_req_poll; 
uv_fs_t open_req_tcp; 
uv_fs_t open_req_udp; 
uv_fs_t read_req;
uv_fs_t write_req;
uv_fs_t write_req_poll;
uv_fs_t close_req_poll;

static char buffer[1024];
static uv_buf_t iob;
static char buffer2[1024];
static uv_buf_t iob2;

struct sockaddr_in addr;
typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;


void on_read(uv_fs_t *req);
void on_write(uv_fs_t *req);
void on_open(uv_fs_t *req);

void on_write_poll(uv_fs_t *req);
void on_open_poll(uv_fs_t *req);

//callback function when an UDP packet arives
void on_read_udp(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
    static uv_buf_t iob_temp;
    static char buffer_temp[1024];
    if (nread < 0) {
        fprintf(stderr, "Read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t*) req, NULL);
        free(buf->base);
        return;
    }
    iob_temp = uv_buf_init(buffer_temp, sizeof(buffer_temp));
    iob_temp.base = buf->base;
    iob_temp.len = strlen(buf->base);
    //uv_mutex_lock(&mutex);
    uv_fs_open(loop, &open_req_udp, MAIN_FILE, O_RDWR, 0, NULL);
    uv_fs_write(loop, &open_req_udp, open_req_udp.result, &iob_temp, 1, -1, NULL);  
    uv_fs_close(loop, &open_req_udp, open_req_udp.result, NULL);
    //uv_mutex_unlock(&mutex);
    free(buf->base);
}

//callback function 3 when the timer runs out; determining  whether the file should be closed or if the writing to the terminal continues
void on_read(uv_fs_t *req) {
    if (req->result < 0) {
        fprintf(stderr, "Read error: %s\n", uv_strerror(req->result));
    }
    else if (req->result == 0) {
        uv_fs_t close_req;
        uv_fs_close(loop, &close_req, open_req.result, NULL);
        //uv_mutex_unlock(&mutex);
    }
    else if (req->result > 0) {
        iob.len = req->result;
        uv_fs_write(loop, &write_req, 1, &iob, 1, -1, on_write);
    }
}

//callback function 4 when the timer runs out; continuing to read from the main file
void on_write(uv_fs_t *req) {
    if (req->result < 0) {
        fprintf(stderr, "Write error: %s\n", uv_strerror((int)req->result));
    }
    else {
        uv_fs_read(loop, &read_req, open_req.result, &iob, 1, -1, on_read);
    }
}

//callback function 2 when the timer runs out; reading what is in the main files
void on_open(uv_fs_t *req) {
    assert(req == &open_req);
    if (req->result >= 0) {
        iob = uv_buf_init(buffer, sizeof(buffer));
        uv_fs_read(loop, &read_req, req->result, &iob, 1, -1, on_read);
    }
    else {
        fprintf(stderr, "error opening file: %s\n", uv_strerror((int)req->result));
    }
}

//callback function 1 when the timer runs out; opening the main file
void on_timer(uv_timer_t *handle) {
    system("clear");
    //uv_mutex_lock(&mutex);
    uv_fs_open(loop, &open_req, MAIN_FILE, O_RDONLY, 0, on_open);
}

//call back function 3 for pooling a file; closing the file when there is nothing left to write
void on_write_poll(uv_fs_t *req) {
    if (req->result < 0) {
        fprintf(stderr, "Write error: %s\n", uv_strerror((int)req->result));
    }
    else {
        uv_fs_close(loop, &close_req_poll, open_req_poll.result, NULL);
    }
}

//callback function 2 for polling a file; preparation of the buffer which will be written to the log file
void on_open_poll(uv_fs_t *req) {
    assert(req == &open_req_poll);
    if (req->result >= 0) {
        time_t clk = time(NULL);
        iob2 = uv_buf_init(buffer2, sizeof(buffer2));
        char pom[100];
        sprintf(pom, "File changed: %s\n", ctime(&clk));
        iob2.base = pom;
        iob2.len = strlen(pom)+1;
        uv_fs_write(loop, &write_req_poll, req->result, &iob2, 1, -1, on_write_poll);
    }
    else {
        fprintf(stderr, "error opening file: %s\n", uv_strerror((int)req->result));
    }
}

//callback function 1 for polling a file; when a change is detected this will be called to open the log file
void on_polling(uv_fs_event_t *handle, const char *filename, int events, int status) {
    uv_fs_open(loop, &open_req_poll, LOG_FILE, O_RDWR, 0, on_open_poll); 
}

void free_write_req(uv_write_t *req) {
    write_req_t *wr = (write_req_t*) req;
    free(wr->buf.base);
    free(wr);
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    buf->base = (char*) malloc(suggested_size);
    buf->len = suggested_size;
}

//calback function 2 for TCP handling; writing to the main file
void tcp_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread > 0) {
        write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
        req->buf = uv_buf_init(buf->base, nread);
        //uv_mutex_lock(&mutex);
        uv_fs_open(loop, &open_req_tcp, MAIN_FILE, O_RDWR, 0, NULL);
        uv_fs_write(loop, &open_req_tcp, open_req_tcp.result, &req->buf, 1, -1, NULL);
        uv_fs_close(loop, &open_req_tcp, open_req_tcp.result, NULL);     
        //uv_mutex_unlock(&mutex);
        return;
    }
    if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t*) client, NULL);
    }

    free(buf->base);
}

//callback function 1 for TCP handling; reading from the stream
void on_new_connection(uv_stream_t *server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        // error!
        return;
    }
    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    if (uv_accept(server, (uv_stream_t*) client) == 0) {
        uv_read_start((uv_stream_t*) client, alloc_buffer, tcp_read);
    }
    else {
        uv_close((uv_handle_t*) client, NULL);
    }
}


int main() {
    //main loop for libuv
    loop = uv_default_loop();

    if(uv_mutex_init(&mutex) < 0){
        fprintf(stderr, "error creating mutex\n");
    }

    //UDP handle
    uv_udp_t recv_socket;
    struct sockaddr_in recv_addr;
    uv_udp_init(loop, &recv_socket);
    uv_ip4_addr(UDP_IP, UDP_PORT, &recv_addr);
    uv_udp_bind(&recv_socket, (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&recv_socket, alloc_buffer, on_read_udp);

    //timer handle
    uv_timer_init(loop, &show_user);
    uv_timer_start(&show_user, on_timer, 0, TIMER);

    //polling handle
    uv_fs_event_t *fs_event_req = malloc(sizeof(uv_fs_event_t));
    uv_fs_event_init(loop, fs_event_req);
    uv_fs_event_start(fs_event_req, on_polling, MAIN_FILE, UV_FS_EVENT_RECURSIVE);

    //TCP handle
    uv_tcp_t server;
    uv_tcp_init(loop, &server);
    uv_ip4_addr(TCP_IP, TCP_PORT, &addr);
    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);
    int report_error = uv_listen((uv_stream_t*) &server, DEFAULT_BACKLOG, on_new_connection);
    if (report_error) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(report_error));
        return 1;
    }

    return uv_run(loop, UV_RUN_DEFAULT);

    uv_fs_req_cleanup(&open_req);
    uv_fs_req_cleanup(&read_req);
    uv_fs_req_cleanup(&write_req);
    uv_fs_req_cleanup(&open_req_poll);
    uv_fs_req_cleanup(&write_req_poll);
    uv_mutex_destroy(&mutex);
    return 0;
}