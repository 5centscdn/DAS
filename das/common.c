#include "common.h"
#include <time.h>

void logmsg(const char *fmt, ...) {
    char header[1024];
    char ts[1024];
    time_t now;
    struct tm tm;
    FILE *fout = fopen("/var/log/das/das.log", "a");
    if (fout == NULL && errno == ENOENT) {
        (void)system("mkdir -p /var/log/das");
        fout = fopen("/var/log/das/das.log", "a");
    }
    if (fout == NULL) {
        fout = fopen("das.log", "a");
    }

    now = time(NULL);
    localtime_r(&now, &tm);

    strftime(ts, sizeof(ts), "%Y/%m/%d %H:%M:%S", &tm);
	snprintf(header, sizeof(header), "%s %s\n", ts, fmt);


    va_list args;
    va_start(args, fmt);
    vfprintf(fout, header, args);
    va_end(args);

    fclose(fout);
}

void socket_setup(int socket_id) {
    struct linger l;
    int yes = 1;

    if (socket_id == -1) {
        return;
    }

#define ERR(fd, x) \
    do { \
        if (x == -1) { \
            logmsg("%s failed for %d: %s", #x, fd, strerror(errno)); \
        } \
    } while (0)

    ERR(socket_id, setsockopt(socket_id, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)));
    ERR(socket_id, setsockopt(socket_id, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)));
    ERR(socket_id, setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));

    l.l_onoff = 0;
    l.l_linger = 0;
    ERR(socket_id, setsockopt(socket_id, SOL_SOCKET, SO_LINGER, &l, sizeof(l)));
    ERR(socket_id, fcntl(socket_id, F_SETFL, O_NONBLOCK));
#undef ERR
}

void socket_cleanup(int socket_id) {
    (void)shutdown(socket_id, SHUT_RDWR);
    (void)close(socket_id); 
}
