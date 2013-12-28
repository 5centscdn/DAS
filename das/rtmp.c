#include <pthread.h>
#include <poll.h>

#include "common.h"
#include "../lib/librtmp/rtmp_sys.h"
#include "../lib/librtmp/log.h"

static int DAS_DEBUG_ON = 0;

#define SET(dst, value) __sync_lock_test_and_set(&(dst), (value))

#include <pthread.h>
#define TFRET()	return 0
#define THANDLE pthread_t
typedef void *(thrfunc)(void *arg);

pthread_t
ThreadCreate(thrfunc *routine, void *args)
{
  pthread_t id = 0;
  int ret;

  ret =
    pthread_create(&id, NULL, routine, args);
  if (ret != 0)
    logmsg("%s, pthread_create failed with %d\n", __FUNCTION__, ret);
  pthread_detach(id);

  return id;
}
const char header_err[] = "HTTP/1.1 404 Not Found\r\nCache-Control: no-store\r\nConnection: close\r\nPragma: no-cache\r\nServer: " PACKAGE_NAME " " PACKAGE_VERSION "\r\n\r\n";
#define HEADER_ERR_SIZE (sizeof(header_err) - 1)

typedef struct
{
  AVal hostname;
  int rtmpport;
  int protocol;
  int bLiveStream;		// is it a live stream? then we can't seek/resume

  long int timeout;		// timeout connection after 120 seconds
  uint32_t bufferTime;

  char *rtmpurl;
  AVal playpath;
  AVal swfUrl;
  AVal tcUrl;
  AVal pageUrl;
  AVal app;
  AVal auth;
  AVal swfHash;
  AVal flashVer;
  AVal token;
  AVal subscribepath;
  AVal usherToken; //Justin.tv auth token
  AVal sockshost;
  AMFObject extras;
  int edepth;
  uint32_t swfSize;
  int swfAge;
  int swfVfy;

  uint32_t dStartOffset;
  uint32_t dStopOffset;

#ifdef CRYPTO
  unsigned char hash[RTMP_SWF_HASHLEN];
#endif
} RTMP_REQUEST;
#define STR2AVAL(av,str)	av.av_val = str; av.av_len = strlen(av.av_val)

static RTMP_REQUEST defaultRTMPRequest;

typedef struct block_s {
    char *data;
    size_t len;
    size_t size;
    long long seqn;
} block_t;

/* Total number of buffered packets */
#define BUFFER_PACKET_COUNT 1024
/* Number of packets behind current one when only audio is sent */
#define BUFFER_LAG_TOO_LARGE 900
/* Skip this many frames before start streaming, gives client possibility to launch player, etc */
#define BUFFER_PACKET_SKIP  60

typedef struct buffer_s {
    block_t blocks[BUFFER_PACKET_COUNT];
} buffer_t;

typedef struct stream_backend_s {
    char *url;                  /* Stream URL */
    RTMP *rtmp;
    RTMP_REQUEST req;
    pthread_cond_t cond;
    int have_cond;

    int clients_per_thread;     /* Thread client limit */
    int consumer_count;
    int stream_alive;

    char *metadata;             /* Metadata buffer */
    size_t metahead_len;        /* Current size of metadata header */
    size_t metadata_len;        /* Current size of metadata */
    size_t metadata_size;       /* Current size of metadata buffer */

    long long n_read;           /* Number of bytes read from stream */
    long long usec_reading;
    long long usec_idle;

    long long seqn;
    long long last_key_frame_seqn;
    buffer_t buffer;
} stream_backend_t;

typedef struct stream_s {
    stream_backend_t *backend;
    pthread_mutex_t mutex;
    int have_mutex;

    int *clients;
    size_t clients_count;
    size_t clients_size;
    long long n_written;        /* Number of bytes sent to clients */

    long long usec_idle;
    long long usec_writing;
} stream_t;

enum req_type_t { REQ_STREAM, REQ_PLAYLIST, REQ_PLAYLIST_X, REQ_FILE };

#define BUFFERING_ON 1
#define BUFFERING_OFF 0
typedef struct client_s {
    stream_t *stream;           /* Pointer to backend stream. If this is NULL, socket is not connected */
    char *uagent;
    char *url_stream;
    char *url_file;
    struct sockaddr_in sockaddr;
    long long n_written;        /* Number of bytes sent to client */
    int n_read;                 /* Number of request bytes read for Apache-logging */
    int buffering_state;
    int sending;
    int header_size;
    enum req_type_t req_type;


    size_t offset;
    long long buffer_seqn;

    block_t buf;
} client_t;

typedef struct context_s {
    /* Max number of open files so we can pre-allocate buffers */
    int max_open_files;
    int max_clients_per_thread;

    client_t *clients;
    size_t  clients_count;

    stream_t *streams;
    size_t  streams_count;
    pthread_mutex_t mutex;

    stream_backend_t *backends;
    size_t backends_count;
    pthread_mutex_t backend_mutex;
} context_t;

static context_t ctxt;
#define SOCKADDR(i) ctxt.clients[i].sockaddr

// /var/log/das/access.log
// /var/log/das/aaaa/clients.%date.log
// /var/log/das/aaaa/access.%date.log

static void logaccess(int socket_id) {
    char path[4 * 1024];
    time_t now;
    struct tm tm;
    char ts[64];
    char file_ts[64];
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    const char *app = ctxt.clients[socket_id].stream->backend->req.app.av_val;

    now = time(NULL);
    localtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "%d/%b/%Y:%H:%M:%S %z", &tm);
    strftime(file_ts, sizeof(file_ts), "%Y-%m-%d", &tm);
    getpeername(socket_id, (struct sockaddr *)&peer, &peer_len);

    FILE *fout = fopen("/var/log/das/access.log", "a");
    if (fout == NULL && errno == ENOENT) {
        (void)system("mkdir -p /var/log/das");
        fout = fopen("/var/log/das/access.log", "a");
    }
    if (fout != NULL) {
        fprintf(fout, "%s - - [%s] \"GET %s HTTP/1.1\" 200 %d \"-\" \"%s\"\n", inet_ntoa(peer.sin_addr), ts,
                ctxt.clients[socket_id].stream->backend->url, ctxt.clients[socket_id].n_read, ctxt.clients[socket_id].uagent);
        fclose(fout);
    }
    snprintf(path, sizeof(path), "/var/log/das/%s/access.%s.log", app, file_ts);
    fout = fopen(path, "a");
    if (fout == NULL && errno == ENOENT) {
        char cmd[4 * 1024];
        snprintf(cmd, sizeof(cmd), "mkdir -p /var/log/das/%s", app);
        (void)system(cmd);
        fout = fopen(path, "a");
    }
    if (fout != NULL) {
        fprintf(fout, "%s - - [%s] \"GET %s HTTP/1.1\" 200 %d \"-\" \"%s\"\n", inet_ntoa(peer.sin_addr), ts,
                ctxt.clients[socket_id].stream->backend->url, ctxt.clients[socket_id].n_read, ctxt.clients[socket_id].uagent);
        fclose(fout);
    }
}

static void logclient(int socket_id, const char *what) {
    char path[4 * 1024];
    time_t now;
    struct tm tm;
    char ts[64];
    char file_ts[64];
    //const char *app = ctxt.clients[socket_id].stream->req.app.av_val;

    now = time(NULL);
    localtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "%d/%b/%Y:%H:%M:%S %z", &tm);
    strftime(file_ts, sizeof(file_ts), "%Y-%m-%d", &tm);

    //snprintf(path, sizeof(path), "/var/log/das/%s/clients.%s.log", app, file_ts);
    snprintf(path, sizeof(path), "/var/log/das/clients.%s.log", file_ts);
    FILE *fout = fopen(path, "a");
    if (fout == NULL && errno == ENOENT) {
        char cmd[4 * 1024];
        //snprintf(cmd, sizeof(cmd), "mkdir -p /var/log/das/%s", app);
        snprintf(cmd, sizeof(cmd), "mkdir -p /var/log/das");
        (void)system(cmd);
        fout = fopen(path, "a");
    }
    if (fout != NULL) {
        fprintf(fout, "%s - Client %s from: %s\n", ts, what, inet_ntoa(SOCKADDR(socket_id).sin_addr));
        fclose(fout);
    }
}

#define SOCKET_CLEANUP(socket_id) do { \
    logclient(socket_id, "disconnected"); \
    socket_cleanup(socket_id); \
} while (0)

/*
* Removes client from stream's client list
*/
static void remove_stream_client(int client_fd) {
    int i;
    stream_t *stream = ctxt.clients[client_fd].stream;

    pthread_mutex_lock(&ctxt.mutex);

    for (i = 0; i < stream->clients_count; i++) {
        if (stream->clients[i] == client_fd) {
            free(ctxt.clients[client_fd].uagent);
            free(ctxt.clients[client_fd].url_stream);
            stream->clients[i] = stream->clients[stream->clients_count - 1];
            stream->clients_count--;
            break;
        }
    }
    pthread_mutex_unlock(&ctxt.mutex);

    SOCKET_CLEANUP(client_fd);
}

/*
* Tries to remove stream. It succeeds only if all the clients have been removed
* because listener thread may add new clients in parallel
*/
static int remove_stream(stream_t *stream) {
    int success = 0;
    pthread_mutex_lock(&ctxt.mutex);
    if (stream->clients_count == 0) {

        pthread_mutex_lock(&ctxt.backend_mutex);
        stream->backend->consumer_count--;
        pthread_mutex_unlock(&ctxt.backend_mutex);

        free(stream->clients);
        stream->backend = NULL;
        if (stream == &ctxt.streams[ctxt.streams_count - 1]) {
            ctxt.streams_count--;
        }
        success = 1;
    }
    pthread_mutex_unlock(&ctxt.mutex);
    return success;
}

static int remove_backend(stream_backend_t *stream) {
    int success = 0;
    pthread_mutex_lock(&ctxt.backend_mutex);
    if (stream->consumer_count == 0) {
        RTMP_Close(stream->rtmp);
        free(stream->url);
        free(stream->metadata);
        free(stream->rtmp);
        free(stream->req.tcUrl.av_val);
        free(stream->req.app.av_val);
        free(stream->req.playpath.av_val);

        stream->url = NULL;
        if (stream == &ctxt.backends[ctxt.backends_count - 1]) {
            ctxt.backends_count--;
        }
        success = 1;
    }
    pthread_mutex_unlock(&ctxt.backend_mutex);
    return success;
}

static int is_key_frame(const char *data, int data_len) {
    const unsigned char *d = data;
    if (data_len >= 16) {
        if (d[0] == RTMP_PACKET_TYPE_VIDEO && (d[11] >> 4) == 0x01) {
            return 1;
        }
    }
    return 0;
}

static void m3u8(const char *url) {
#if 0
http://us-dal-mp01.5centscdn.com/zeetamilvxtv/685ce080365181e6f09841ed7a6417af.sdp/playlist.m3u8
#EXTM3U
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=1042732
http://us-dal-mp01.5centscdn.com/zeetamilvxtv/685ce080365181e6f09841ed7a6417af.sdp/playlist.m3u8?wowzasessionid=387355163

http://us-dal-mp01.5centscdn.com/zeetamilvxtv/685ce080365181e6f09841ed7a6417af.sdp/playlist.m3u8?wowzasessionid=387355163
#EXTM3U
#EXT-X-ALLOW-CACHE:NO
#EXT-X-TARGETDURATION:13
#EXT-X-MEDIA-SEQUENCE:17895
#EXTINF:10,
media_17895.ts?wowzasessionid=387355163
#EXTINF:9,
media_17896.ts?wowzasessionid=387355163
#EXTINF:9,
media_17897.ts?wowzasessionid=387355163
#endif
}

#define DUMP_FLV(data, len) do { \
    unsigned char *d = (data); \
    int l = (len); \
    /* Stream is always 000000, not interesting */ \
    TRACE("type [%02x] length [%02x%02x%02x] timestamp [%02x%02x%02x%02x] body [%02x...] prev length [%02x%02x%02x]", \
            d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[11], d[l - 3], d[l - 2], d[l - 1]); \
} while (0)

#define TRACE(msg, args...) do { \
    if (DAS_DEBUG_ON) { \
        logmsg(msg, ##args); \
    } \
} while (0)
 
static int client_write(client_t *client, int socket, const char *data, int data_len) {
    int n_written;
    n_written = write(socket, data, data_len);
    TRACE("write(%d, %p, %d) = %d", socket, data, data_len, n_written);
    if (n_written == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        } else {
            logmsg("Removing socket %d/%s:%d because of failure to write %d bytes (%s)",
                    socket, inet_ntoa(SOCKADDR(socket).sin_addr), SOCKADDR(socket).sin_port,
                    data_len, strerror(errno));
            remove_stream_client(socket);
            return -1;
        }
    } else {
        client->stream->n_written += n_written;
        client->n_written += n_written;
        client->offset += n_written;
        return n_written;
    }
}

long long usec() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (long long)now.tv_sec * (long long)1000000 + (long long)now.tv_usec;
}

#define SEQNBUF(n) ((n) % BUFFER_PACKET_COUNT)

static void *backend_thread(void *arg) {
    stream_backend_t *stream = arg;

    int n_read;
    int i;

    /* Reads stream metadata */
    for (i = 0; i < 50; i++) {
        n_read = RTMP_Read(stream->rtmp, stream->metadata + stream->metadata_len, stream->metadata_size - stream->metadata_len);
        if (n_read <= 0) {
            logmsg("RTMP_Read FAILED, Got Play.Complete or Play.Stop from server. Assuming stream is complete");
            break;
        }

        if (i == 0) {
            stream->metahead_len = n_read;
        }

        DUMP_FLV(stream->metadata + stream->metadata_len, n_read);
        char type = *(stream->metadata + stream->metadata_len);

        /* Read the whole timestamp == 0 */
        if (type == RTMP_PACKET_TYPE_AUDIO || type == RTMP_PACKET_TYPE_VIDEO) {
            if (memcmp(stream->metadata + stream->metadata_len + 4, "\0\0\0\0", 4) != 0) {
                logmsg("Got non-zero timestamp, enough metadata");
                break;
            }
        }
        logmsg("Metadata size %d", n_read);

        stream->n_read += n_read;
        stream->metadata_len += n_read;
    }

    while (1) {
        char buf[1024 * 1024];
        time_t consumers_ts = time(NULL);
        int stop_backend = 0;

        while (stream->stream_alive && !stop_backend) {
            int i;
            int n_written;
            int write_retry;
            int key_frame;
            int buf_id;
            long long ts_start, ts_read;
            time_t now;

            ts_start = usec();
            n_read = RTMP_Read(stream->rtmp, buf, sizeof(buf));
            ts_read = usec();

            stream->usec_reading += ts_read - ts_start;

            /* Either end of stream (0) or error (-1) occured */
            TRACE("read() = %d", n_read);
            if (n_read <= 0) {
                stream->stream_alive = 0;
                logmsg("RTMP_Read FAILED, Got Play.Complete or Play.Stop from server. Assuming stream is complete");
                break;
            }
            DUMP_FLV(buf, n_read);
            stream->n_read += n_read;

            /* Stop backend if no connected consumers for 1min */
            now = time(NULL);
            if (stream->consumer_count != 0) {
                consumers_ts = now;
            } else if (now - consumers_ts > 60) {
                stop_backend = 1;
            }

            /*
             * Store frame in buffer
             */
            long long new_seqn = stream->seqn + 1;
            key_frame = is_key_frame(buf, n_read);
            TRACE("%lld/Packet Type [%d], key_frame?=%d", new_seqn, buf[0], key_frame);

            buf_id = SEQNBUF(new_seqn);
            if (stream->buffer.blocks[buf_id].size < n_read) {
                stream->buffer.blocks[buf_id].size = n_read + 1024;
                stream->buffer.blocks[buf_id].data = realloc(stream->buffer.blocks[buf_id].data,
                        stream->buffer.blocks[buf_id].size);
            }

            memcpy(stream->buffer.blocks[buf_id].data, buf, n_read);
            stream->buffer.blocks[buf_id].len = n_read;
            stream->buffer.blocks[buf_id].seqn = new_seqn;

            SET(stream->seqn, new_seqn);
            if (key_frame) {
                SET(stream->last_key_frame_seqn, new_seqn);
            }
            pthread_cond_broadcast(&stream->cond);
        }

        /* We get here if reading from stream failed or no more clients left */

        if (!stream->stream_alive) {
            logmsg("[%s] is unavailable", stream->url);
        }

        logmsg("[%s] No more stream connections to backend, stopping thread", stream->url);
        if (remove_backend(stream)) {
            break;
        }
        /* Stream removal failed - there should be new clients, handle them */
    }

    return NULL;
}


/*

Handling client connection:
send HTTP response headers
send first metadata frame 
wait a bit/skip frames for client to start up player...
wait for key frame
send remaining metadata frames (initial video, audio)
send key frame
continue sending data...

*/
static void *
stream_thread(void *arg)
{
    stream_t *stream = arg;
    stream_backend_t *backend = stream->backend;

    int i;

    /* For HTTP Date header */
    char date[32];
    time_t now;
    struct tm tm;

    while (1) {
        while (backend->stream_alive && stream->clients_count > 0) {
            int i;
            int n_written;
            int write_retry;
            long long ts_start, ts_read;

            ts_start = usec();
            pthread_mutex_lock(&stream->mutex);
            pthread_cond_wait(&backend->cond, &stream->mutex);
            pthread_mutex_unlock(&stream->mutex);
            ts_read = usec();

            stream->usec_idle += ts_read - ts_start;
            long long last_key_frame_seqn = backend->last_key_frame_seqn;
            long long seqn = backend->seqn;

            now = time(NULL);
            localtime_r(&now, &tm);
            strftime(date, sizeof(date), "%a, %d %b %y %T %z", &tm);
            int fragment = 4;

            for (i = 0; i < stream->clients_count; i++) {
                int socket = stream->clients[i];
                client_t *client = &ctxt.clients[socket];

                if (client->n_written == 0) {
                    if (client->req_type == REQ_STREAM) {
                        /* Sending HTTP headers */
                        client->header_size = snprintf(client->buf.data, client->buf.size,
                                    "HTTP/1.1 200 OK\r\n"
                                    "Server: " PACKAGE_NAME " " PACKAGE_VERSION "\r\n"
                                    "Date: %s\r\n"
                                    "Connection: close\r\n"
                                    "Cache-Control: no-store\r\n"
                                    "Pragma: no-cache\r\n"
                                    "Accept-Ranges: bytes\r\n"
                                    "Content-Length: 1073741824\r\n"
                                    "Content-Type: video/mp4\r\n"
                                    "\r\n", date);
                    } else if (client->req_type == REQ_PLAYLIST) {
                        char content[1024];
                        int content_length;

                        content_length = snprintf(content, sizeof(content),
                                "#EXTM3U\n"
                                "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=1042732\n"
                                "%s/playlist.m3u8?ts=%d", backend->url, (int)now);

                        client->header_size = snprintf(client->buf.data, client->buf.size,
                                    "HTTP/1.1 200 OK\r\n"
                                    "Server: " PACKAGE_NAME " " PACKAGE_VERSION "\r\n"
                                    "Date: %s\r\n"
                                    "Connection: close\r\n"
                                    "Cache-Control: no-store\r\n"
                                    "Pragma: no-cache\r\n"
                                    "Content-Length: %d\r\n"
                                    "Content-Type: text/plain\r\n"
                                    "\r\n%s", date, content_length, content);
                    } else if (client->req_type == REQ_PLAYLIST_X) {
                        char content[1024];
                        int content_length;

                        content_length = snprintf(content, sizeof(content),
                            "#EXTM3U\n"
                            "#EXT-X-ALLOW-CACHE:NO\n"
                            "#EXT-X-TARGETDURATION:13\n"
                            "#EXT-X-MEDIA-SEQUENCE:%d\n"
                            "#EXTINF:10,\n"
                            "%s/fragment_%d?ts=%d\n"
                            "#EXTINF:9,\n"
                            "%s/fragment_%d?ts=%d\n"
                            "#EXTINF:9,\n"
                            "%s/fragment_%d?ts=%d\n",
                            fragment - 2,
                            backend->url, fragment - 2, (int)now,
                            backend->url, fragment - 1, (int)now,
                            backend->url, fragment - 0, (int)now
                            );
                    }
                } if (client->n_written < client->header_size) { logmsg("Sending headers to socket %d/%s:%d",
                        socket, inet_ntoa(SOCKADDR(socket).sin_addr), SOCKADDR(socket).sin_port);
                    logaccess(socket);
                    if (client_write(client, socket,
                            client->buf.data + client->n_written,
                            client->header_size - client->n_written) == -1) {
                        i--;
                        continue;
                    }
                }

                if (client->req_type == REQ_STREAM) {
                    /* Sending metadata headers so client can start up player */
                    if (client->n_written < backend->metahead_len + client->header_size) {
                        logmsg("Sending metadata to socket %d/%s:%d",
                            socket, inet_ntoa(SOCKADDR(socket).sin_addr), SOCKADDR(socket).sin_port);
                        if (client_write(client, socket,
                                backend->metadata + (client->n_written - client->header_size),
                                backend->metahead_len -  (client->n_written - client->header_size)) == -1) { 
                            i--;
                            continue;
                        }
                        client->sending = 0;
                        client->buffer_seqn = seqn + BUFFER_PACKET_SKIP;
                    }

                    /* Ok, some time has passed. Client should be ready to receive data */
                    if (seqn >= client->buffer_seqn) {
                        /* First time here */
                        if (!client->sending) {
                            /* Wait for first key frame */
                            if (last_key_frame_seqn < client->buffer_seqn) {
                                continue;
                            }
                            client->sending = 1;
                            client->offset = 0;
                        }

                        if (client->n_written < backend->metadata_len + client->header_size) {
                            logmsg("Sending data to socket %d/%s:%d",
                                socket, inet_ntoa(SOCKADDR(socket).sin_addr), SOCKADDR(socket).sin_port);
                            if (client_write(client, socket,
                                    backend->metadata + (client->n_written - client->header_size),
                                    backend->metadata_len -  (client->n_written - client->header_size)) == -1) { 
                                i--;
                                continue;
                            }
                            client->offset = 0;
                        }

                        if (client->buffer_seqn + BUFFER_PACKET_COUNT - 10 < seqn) {
                            logmsg("Removing socket %d/%s:%d because pending data exceeds buffer",
                                socket, inet_ntoa(SOCKADDR(socket).sin_addr), SOCKADDR(socket).sin_port);
                            remove_stream_client(socket);
                            i--;
                            continue;
                        }

                        int err = 0;
                        int code;
                        int skip = (seqn - client->buffer_seqn > BUFFER_LAG_TOO_LARGE);

                        /*
                         * Try to send all pending packets to client
                         */
                        while (client->buffer_seqn <= seqn) {
                            if (client->offset > 0  /* Partial packet written */
                                    || !skip        /* Normal mode */
                                                    /* Skip till last key frame */
                                    || (skip && backend->buffer.blocks[SEQNBUF(client->buffer_seqn)].seqn >= last_key_frame_seqn)) {

                                TRACE("Sending data/%lld to socket %d/%s:%d", client->buffer_seqn,
                                    socket, inet_ntoa(SOCKADDR(socket).sin_addr), SOCKADDR(socket).sin_port);
                                code = client_write(client, socket,
                                            backend->buffer.blocks[SEQNBUF(client->buffer_seqn)].data + client->offset,
                                            backend->buffer.blocks[SEQNBUF(client->buffer_seqn)].len - client->offset);

                                if (code == -1) {
                                    err = 1;
                                    break;
                                }

                                /* Not fully written */
                                if (client->offset != backend->buffer.blocks[SEQNBUF(client->buffer_seqn)].len) {
                                    if (ctxt.clients[socket].buffering_state == BUFFERING_OFF) {
                                        logmsg("Could not send data to socket %d/%s:%d, start buffering [seq#%lld]@[seq#%lld]",
                                            socket, inet_ntoa(SOCKADDR(socket).sin_addr), SOCKADDR(socket).sin_port,
                                            client->buffer_seqn, seqn);
                                    }
                                    ctxt.clients[socket].buffering_state = BUFFERING_ON;
                                    break;
                                } else {
                                    if (ctxt.clients[socket].buffering_state == BUFFERING_ON) {
                                        logmsg("Sending buffered data to socket %d/%s:%d, [seq#%lld]...[seq#%lld]",
                                            socket, inet_ntoa(SOCKADDR(socket).sin_addr), SOCKADDR(socket).sin_port,
                                            client->buffer_seqn, seqn);
                                    }
                                    ctxt.clients[socket].buffering_state = BUFFERING_OFF;
                                }
                                skip = 0;
                            }

                            client->buffer_seqn = backend->buffer.blocks[SEQNBUF(client->buffer_seqn)].seqn;
                            client->offset = 0;
                        }

                        if (err) {
                            i--;
                            continue;
                        }
                    }
                }
            }

            stream->usec_writing += usec() - ts_read;
        }

        /* Remove all clients (in case of failed stream) */
        while (!backend->stream_alive && stream->clients_count > 0) {
            int socket = stream->clients[0];
            client_t *client = &ctxt.clients[socket];
            if (client->n_written == 0) {
                write(socket, header_err, HEADER_ERR_SIZE);
            }
            remove_stream_client(socket);
        }

        logmsg("[%s] No more client connections, stopping thread", backend->url);
        if (remove_stream(stream)) {
            break;
        }
        /* Stream removal failed - there should be new clients, handle them */
    }

    return NULL;
}

/*
* Regex-based search/replace
*/
static int replace(regmatch_t *pmatch, char *buf, int size, const char *str) {
    char *pos;
    int sub, so, n;
    for (pos = buf; *pos; pos++) {
        if (*pos == '\\' && *(pos + 1) > '0' && *(pos + 1) <= '9') {
            so = pmatch [*(pos + 1) - '0'].rm_so;
            n = pmatch [*(pos + 1) - '0'].rm_eo - so;
            if (so < 0 || strlen(buf) + n - 1 > size) {
                return -1;
            }
            memmove(pos + n, pos + 2, strlen(pos) - 1);
            memcpy(pos, str + so, n);
            pos = pos + n - 2;
        }
    }
    return 0;
}

/*
* Fills RTMP parameters for creating a stream
*/
static int fill_rtmp_req(RTMP_REQUEST *req, int *clients_per_thread, const char *url) {
    int i;

    for (i = 0; i < cfg_nurls; i++) {
        regmatch_t pmatch[10];
        if (regexec(&cfg_urls[i].re_url, url, 10, pmatch, 0) == 0) {
            char app[4 * 1024];
            char playpath[4 * 1024];

            strcpy(app, cfg_urls[i].app);
            replace(pmatch, app, sizeof(app), url);

            strcpy(playpath, cfg_urls[i].playpath);
            replace(pmatch, playpath, sizeof(playpath), url);

            AVal parsedHost, parsedPlaypath, parsedApp;
            unsigned int parsedPort = 0;
            int parsedProtocol = RTMP_PROTOCOL_UNDEFINED;

            memcpy(req, &defaultRTMPRequest, sizeof(RTMP_REQUEST));
            *clients_per_thread = cfg_urls[i].clients_per_thread;
            STR2AVAL(req->pageUrl, (char *)cfg_urls[i].pageUrl);
            STR2AVAL(req->swfUrl, (char *)cfg_urls[i].swfUrl);
            STR2AVAL(req->flashVer, (char *)cfg_urls[i].flashVer);
            STR2AVAL(req->app, strdup(app));
            STR2AVAL(req->playpath, strdup(playpath));

            req->rtmpurl = (char *)cfg_urls[i].rtmp;
            if (!RTMP_ParseURL(req->rtmpurl, &parsedProtocol, &parsedHost, &parsedPort, &parsedPlaypath, &parsedApp)) {
                logmsg("Couldn't parse the specified url (%s)!", cfg_urls[i].rtmp);
            } else {
                if (!req->hostname.av_len) {
                    req->hostname = parsedHost;
                }
                if (req->rtmpport == -1) {
                    req->rtmpport = parsedPort;
                }
                if (req->playpath.av_len == 0 && parsedPlaypath.av_len) {
                    req->playpath = parsedPlaypath;
                }
                if (req->protocol == RTMP_PROTOCOL_UNDEFINED) {
                    req->protocol = parsedProtocol;
                }
                if (req->app.av_len == 0 && parsedApp.av_len) {
                    req->app = parsedApp;
                }
            }

            if (req->rtmpport == 0) {
                if (req->protocol & RTMP_FEATURE_SSL)
                    req->rtmpport = 443;
                else if (req->protocol & RTMP_FEATURE_HTTP)
                    req->rtmpport = 80;
                else
                    req->rtmpport = 1935;
            }
            if (req->tcUrl.av_len == 0) {
                char str[4 * 1024];
                req->tcUrl.av_len = snprintf(str, sizeof(str), "%s://%.*s:%d/%.*s",
                                            RTMPProtocolStringsLower[req->protocol],
                                            req->hostname.av_len,
                                            req->hostname.av_val, req->rtmpport,
                                            req->app.av_len, req->app.av_val);
                req->tcUrl.av_val = strdup(str);
            }
 

            logmsg("matched url [%s]", cfg_urls[i].url);
            logmsg("tcUrl [%s]", req->tcUrl.av_val);
            logmsg("app [%s] -> [%s]", cfg_urls[i].app, app);
            logmsg("playpath [%s] -> [%s]", cfg_urls[i].playpath, playpath);
            return 0;
        }
    }

    logmsg("No target stream found for URL [%s]", url);
    return -1;
}

static int add_stream_client(stream_backend_t *backend, int client_fd, const char *uagent) {
    int i;
    int start_thread = 0;
    stream_t *stream = NULL;

    pthread_mutex_lock(&ctxt.mutex);

    for (i = 0; i < ctxt.streams_count; i++) {
        if (backend == ctxt.streams[i].backend
                && ctxt.streams[i].clients_count < ctxt.streams[i].backend->clients_per_thread) {
            stream = &ctxt.streams[i];
            break;
        }
    }
    if (stream == NULL) {
        for (i = 0; i < ctxt.streams_count; i++) {
            if (ctxt.streams[i].backend == NULL) {
                stream = &ctxt.streams[i];
                break;
            }
        }
        if (stream == NULL) {
            stream = &ctxt.streams[ctxt.streams_count++];
        }

        logmsg("Starting new client thread for URL [%s]", backend->url);

        stream->backend = backend;
        if (!stream->have_mutex) {
            pthread_mutex_init(&stream->mutex, NULL);
            stream->have_mutex = 1;
        }

        stream->clients_count = 0;
        stream->clients_size = ctxt.max_clients_per_thread;
        stream->clients = malloc(stream->clients_size * sizeof(int));

        stream->usec_idle = 0;
        stream->usec_writing = 0;

        start_thread = 1;

        pthread_mutex_lock(&ctxt.backend_mutex);
        backend->consumer_count++;
        pthread_mutex_unlock(&ctxt.backend_mutex);
    }

    ctxt.clients[client_fd].stream = stream;
    if (uagent == NULL) {
        ctxt.clients[client_fd].uagent = NULL;
    } else {
        ctxt.clients[client_fd].uagent = strdup(uagent);
    }
    ctxt.clients[client_fd].buffering_state = BUFFERING_OFF;
    ctxt.clients[client_fd].n_written = 0;
    ctxt.clients[client_fd].header_size = 0;
    stream->clients[stream->clients_count] = client_fd;

    /* Ensure socket is initialized before we anounce it's available to stream thread */
    __sync_synchronize();
    stream->clients_count++;

    pthread_mutex_unlock(&ctxt.mutex);

    /* No need to keep mutex while starting thread */
    if (start_thread) {
        ThreadCreate(stream_thread, stream);
    }
    return 0;
}

static stream_backend_t *add_backend(char *url) {
    int i;
    int start_thread = 0;
    stream_backend_t *stream = NULL;

#if 0
http://us-dal-mp01.5centscdn.com/zeetamilvxtv/685ce080365181e6f09841ed7a6417af.sdp/playlist.m3u8
#EXTM3U
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=1042732
http://us-dal-mp01.5centscdn.com/zeetamilvxtv/685ce080365181e6f09841ed7a6417af.sdp/playlist.m3u8?wowzasessionid=387355163
#endif
    pthread_mutex_lock(&ctxt.backend_mutex);

    for (i = 0; i < ctxt.backends_count; i++) {
        char *stream_url = ctxt.backends[i].url;
        if (stream_url != NULL && strcmp(url, stream_url) == 0) {
            stream = &ctxt.backends[i];
            break;
        }
    }
    if (stream == NULL) {
        for (i = 0; i < ctxt.backends_count; i++) {
            if (ctxt.backends[i].url == NULL) {
                stream = &ctxt.backends[i];
                break;
            }
        }
        if (stream == NULL) {
            stream = &ctxt.backends[ctxt.backends_count++];
        }

        if (fill_rtmp_req(&stream->req, &stream->clients_per_thread, url) == -1) {
            pthread_mutex_unlock(&ctxt.mutex);
            return NULL;
        }

        logmsg("Starting new backend thread for URL [%s]", url);

        stream->url = strdup(url);
        if (!stream->have_cond) {
            pthread_cond_init(&stream->cond, NULL);
            stream->have_cond = 1;
        }

        stream->metadata_len = 0;
        stream->metadata_size = 1024 * 1024;
        stream->metadata = malloc(stream->metadata_size * sizeof(char));

        stream->rtmp = calloc(sizeof(RTMP), 1);

        stream->usec_reading = 0;
        stream->usec_idle = 0;
        stream->stream_alive = 1;
        stream->consumer_count = 0;
        stream->seqn = 0;
        stream->last_key_frame_seqn = -1;

        RTMP_Init(stream->rtmp);
        RTMP_SetBufferMS(stream->rtmp, stream->req.bufferTime);
        RTMP_SetupStream(stream->rtmp, stream->req.protocol, &stream->req.hostname,
                         stream->req.rtmpport, &stream->req.sockshost, &stream->req.playpath,
                         &stream->req.tcUrl, &stream->req.swfUrl, &stream->req.pageUrl, &stream->req.app,
                         &stream->req.auth, &stream->req.swfHash, stream->req.swfSize, &stream->req.flashVer,
                         &stream->req.subscribepath, &stream->req.usherToken, 0, stream->req.dStopOffset,
                         stream->req.bLiveStream, stream->req.timeout);
        if (stream->req.auth.av_len) {
            stream->rtmp->Link.lFlags |= RTMP_LF_AUTH;
        }
        stream->rtmp->Link.extras = stream->req.extras;
        stream->rtmp->Link.token = stream->req.token;
        stream->rtmp->m_read.timestamp = 0;

        RTMP_Connect(stream->rtmp, NULL);

        start_thread = 1;
    }

    /* No need to keep mutex while starting thread */
    if (start_thread) {
        ThreadCreate(backend_thread, stream);
    }

    pthread_mutex_unlock(&ctxt.backend_mutex);
    return stream;
}

static void preinit_client(int client_fd) {
    client_t *c = &ctxt.clients[client_fd];
    if (c->buf.data == NULL) {
        c->buf.size = 4 * 1024;
        c->buf.data = malloc(c->buf.size);
    }
    c->buf.data[0] = '\0';
    c->buf.len = 0;
}

static void handle_http_request(int socket_id) {
    char *url = NULL;
    char *uagent = NULL;
    client_t *client = &ctxt.clients[socket_id];

    client->n_read = client->buf.len;

    /* Parse HTTP GET request */
    if (strncmp(client->buf.data, "GET ", 4) == 0 && client->buf.len > 5) {
        char *p;
        char *file;
        url = client->buf.data + 4;
        p = url;

        while (*p != '\0') {
            if (*p == ' ') {
                *p++ = '\0';
                break;
            }
            p++;
        }
        client->url_stream = strdup(url);

        client->url_file = strrchr(client->url_stream, '/');
        if (client->url_file == NULL) {
            client->url_file = client->url_stream + strlen(client->url_stream);
            client->req_type = REQ_STREAM;
        } else {
            if (strcmp(client->url_file, "/playlist.m3u8") == 0) {
                client->req_type = REQ_PLAYLIST;
                *client->url_file++ = '\0';
            } else if (strncmp(client->url_file, "/playlist.m3u8?ts=", sizeof("/playlist.m3u8?ts=") - 1) == 0) {
                client->req_type = REQ_PLAYLIST_X;
                *client->url_file++ = '\0';
            } else if (strncmp(client->url_file, "/fragment_", sizeof("/fragment_") - 1) == 0) {
                client->req_type = REQ_FILE;
                *client->url_file++ = '\0';
            } else {
                client->req_type = REQ_STREAM;
                client->url_file = client->url_stream + strlen(client->url_stream);
            }
        }

        /* Retrieve user agent for Apache-logging */
        uagent = strstr(p, "User-Agent: ");
        if (uagent != NULL) {
            uagent += sizeof("User-Agent: ") - 1;
            p = uagent;
            while (*p != '\0') {
                if (*p == '\r' || *p == '\n') {
                    *p++ = '\0';
                    break;
                }
                p++;
            }
        }
    } else {
        (void)write(socket_id, header_err, HEADER_ERR_SIZE);
        SOCKET_CLEANUP(socket_id);
        return;
    }

    logmsg("Accepted request for [%s] from client [%s:%d]", url,
            inet_ntoa(SOCKADDR(socket_id).sin_addr), SOCKADDR(socket_id).sin_port);
    stream_backend_t *backend = add_backend(client->url_stream);
    if (backend == NULL || add_stream_client(backend, socket_id, uagent) == -1) {
        (void)write(socket_id, header_err, HEADER_ERR_SIZE);
        SOCKET_CLEANUP(socket_id);
    }
}

static void *
statusThread(void *arg)
{
    arg = arg;
    while (1) {
        int i;
        //"#Version: 1.0\n#Start-Date: %s %s\n#Software: DaS (%s)\n#Date: %s\n#Fields: date time streamname concurrent bandwidth\n",
        for (i = 0; i < ctxt.streams_count; i++) {
            stream_t *stream = &ctxt.streams[i];
            if (stream->backend != NULL) {
                /* "writing" data to client is the real work, "reading" is mostly idle waiting for stream data */
                double writing = stream->usec_writing;
                double total = stream->usec_idle + stream->usec_writing;

                logmsg("[%s] %d clients | %lld bytes read from stream | %lld bytes sent to clients | %f load",
                        stream->backend->url, stream->clients_count, stream->backend->n_read, stream->n_written, writing/total);
            }
        }
        sleep(60);
    }
}

static int min(int a, int b) {
    return a < b ? a : b;
}

/*
* Increases soft limit of open files up to hard limit
* and return it.
*/
static int get_max_open_files() {
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1) {
        fprintf(stderr, "Failed to get open file limit (%s)",
                strerror(errno));
        abort();
    }

    lim.rlim_cur = lim.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &lim) == -1) {
        fprintf(stderr,
                "Failed to raise open file soft limit up to hard limit (%s), ignored",
                strerror(errno));
    }
    return min(lim.rlim_cur, 65535);
}

static void context_init() {
    pthread_mutex_init(&ctxt.mutex, NULL);
    pthread_mutex_init(&ctxt.backend_mutex, NULL);
    ctxt.max_open_files = get_max_open_files();
    ctxt.max_clients_per_thread = 1000;

    ctxt.clients_count = 0;
    ctxt.clients = calloc(ctxt.max_open_files * sizeof(client_t), 1);

    ctxt.streams_count = 0;
    ctxt.streams = calloc(ctxt.max_open_files * sizeof(stream_t), 1);

    ctxt.backends_count = 0;
    ctxt.backends = calloc(ctxt.max_open_files * sizeof(stream_backend_t), 1);
}

static void RTMP_logmsg(int level, const char *fmt, va_list args) {
    if (level <= RTMP_debuglevel) {
        char msg[4 * 1024];
        vsnprintf(msg, sizeof(msg), fmt, args);
        logmsg("%s", msg);
    }
}

int rtmp_init() {
    context_init();

    if (getenv("DASDEBUG") != 0) {
        const char *value = getenv("DASDEBUG");
        if (strcmp(value, "1") == 0 || strcasecmp(value, "Y") == 0) {
            DAS_DEBUG_ON = 1;
        }
    }

    memset(&defaultRTMPRequest, 0, sizeof(RTMP_REQUEST));

    defaultRTMPRequest.rtmpport = -1;
    defaultRTMPRequest.protocol = RTMP_PROTOCOL_UNDEFINED;
    defaultRTMPRequest.bLiveStream = TRUE;	// is it a live stream? then we can't seek/resume

    defaultRTMPRequest.timeout = 120;	// timeout connection after 120 seconds
    defaultRTMPRequest.bufferTime = 20 * 1000;

    defaultRTMPRequest.swfAge = 30;

    //RTMP_debuglevel = RTMP_LOGDEBUG;
    RTMP_debuglevel = RTMP_LOGINFO;
    RTMP_LogSetCallback(RTMP_logmsg);
    ThreadCreate(statusThread, NULL);
    signal(SIGPIPE, SIG_IGN);
    return 0;
}

void listen_loop(int server_socket_id) {
    int n_pollfds = 1;
    struct pollfd pollfds[1000];
    pollfds[0].fd = server_socket_id;
    pollfds[0].events = POLLIN;
    pollfds[0].revents = 0;

    while (1) {
        int i;
        int ready = poll(pollfds, n_pollfds, 1000);
        if (ready == -1) {
            logmsg("poll() failed with %s", strerror(errno));
            break;
        }

        /* Parent is dead */
        if (getppid() <= 1) {
            logmsg("Parent process has died, exit");
            break;
        }

        /* Have incomming connection */
        if (pollfds[0].revents & POLLIN) {
            /* Multi-accept mechanism */
            while (1) {
                struct sockaddr_in addr;
                socklen_t len = sizeof(addr);
                int childfd = accept(pollfds[0].fd, (struct sockaddr *) &addr, &len);
                if (childfd == -1) {
                    if (errno == EAGAIN && errno == EWOULDBLOCK) {
                        // No more pending connections
                    } else {
                        logmsg("accept() failed with %s", strerror(errno));
                    }
                    break;
                } else {
                    if (n_pollfds + 1 >= sizeof(pollfds)/sizeof(pollfds[0])) {
                        logmsg("accept() too many active connections (%d), drop new connection", n_pollfds);
                        close(childfd);
                    } else {
                        client_t *client = &ctxt.clients[childfd];
                        preinit_client(childfd);
                        socket_setup(childfd);

                        socklen_t peer_len = sizeof(client->sockaddr);
                        if (getpeername(childfd, (struct sockaddr *)&client->sockaddr, &peer_len) == -1) {
                        }

                        logmsg("Got new connection on socket %d from client [%s:%d]", childfd,
                                inet_ntoa(SOCKADDR(childfd).sin_addr), SOCKADDR(childfd).sin_port);

                        logclient(childfd, "connected");

                        pollfds[n_pollfds].fd = childfd;
                        pollfds[n_pollfds].events = POLLIN;
                        pollfds[n_pollfds].revents = 0;
                        n_pollfds++;
                    }
                }
            }
        } else if (pollfds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            logmsg("poll() returned unexpected event %08x", pollfds[0].revents);
            break;
        }

        for (i = n_pollfds - 1; i > 0; i--) {
            int childfd = pollfds[i].fd;
            client_t *client = &ctxt.clients[childfd];

            if (pollfds[i].revents & POLLIN) {

                int n;
                n = read(childfd, client->buf.data + client->buf.len, client->buf.size - client->buf.len - 1);
                if (n == -1) {
                    logmsg("Read failed on socket %d (%s) client [%s:%d]", childfd, strerror(errno),
                        inet_ntoa(SOCKADDR(childfd).sin_addr), SOCKADDR(childfd).sin_port);

                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // continue
                    } else {
                        SOCKET_CLEANUP(childfd);
                        pollfds[i] = pollfds[--n_pollfds];
                    }
                } else if (n == 0) {
                    logmsg("Disconnect on socket %d while reading HTTP headers (%d bytes, %s) client [%s:%d]",
                            childfd, client->buf.len, client->buf.data,
                            inet_ntoa(SOCKADDR(childfd).sin_addr), SOCKADDR(childfd).sin_port);
                    SOCKET_CLEANUP(childfd);
                    pollfds[i] = pollfds[--n_pollfds];
                } else {
                    client->buf.len += n;
                    client->buf.data[client->buf.len] = '\0';
                }

                if (strchr(client->buf.data, '\n') != NULL) {
                    pollfds[i] = pollfds[--n_pollfds];
                    handle_http_request(childfd);
                }
            } else if (pollfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                logmsg("Disconnect on socket %d while reading HTTP headers (%d bytes, %s) client [%s:%d]",
                        childfd, client->buf.len, client->buf.data,
                        inet_ntoa(SOCKADDR(childfd).sin_addr), SOCKADDR(childfd).sin_port);
                SOCKET_CLEANUP(childfd);
                pollfds[i] = pollfds[--n_pollfds];
            }
        }
    }
}



