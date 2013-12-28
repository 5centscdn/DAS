#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <regex.h>

typedef struct url_s {
    const char *url;
    const char *pageUrl;
    const char *swfUrl;
    const char *app;
    const char *playpath;
    const char *rtmp;
    const char *flashVer;
    int clients_per_thread;
    regex_t re_url;
} url_t;

extern url_t *cfg_urls;
extern int cfg_nurls;

void logmsg(const char *fmt, ...)  __attribute__ ((format (printf, 1, 2)));
void socket_setup(int socket_id);
void socket_cleanup(int socket_id);

void rtmp_handle_connection(int socket_id);
int rtmp_init();
