#include "common.h"

#include "../lib/json-c/json.h"

url_t *cfg_urls;
int cfg_nurls;
static const char *cfg_listen;


void listen_loop(int socket_id);

static void serve() {
    int backlog = 8;
    int socket_id;
    struct sockaddr_in addr;

    char *dup = strdup(cfg_listen);
    char *sep = strchr(dup, ':');
    char *interface = NULL, *port = dup;

    if (sep != NULL) {
        interface = dup;
        *sep = '\0';
        port = sep + 1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;

    if (interface == NULL) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        addr.sin_addr.s_addr = inet_addr(interface);
    }
    addr.sin_port = htons(atoi(port));            
    free(dup);

    socket_id = socket(AF_INET, SOCK_STREAM, 0);
    socket_setup(socket_id);

    if (bind(socket_id, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        logmsg("Failed to start listening on %s (%s)", cfg_listen, strerror(errno));
    } else if (listen(socket_id, backlog) == -1) {
        logmsg("listen(%d) failed: %s", socket_id, strerror(errno));
    } else {
        logmsg("Listening on %s", cfg_listen);
        if (rtmp_init() != -1) {
            listen_loop(socket_id);
        }
        logmsg("Finish listening on %s", cfg_listen);
    }

    socket_cleanup(socket_id);
}

static char *read_whole_file(const char *fname) {
    /* procfs files don't have size, so read chunks until EOF is reached */
    char *data = NULL;
    FILE *f;

    f = fopen(fname, "r");
    if (f != NULL) {
        int n, len = 0;
        int size = 1024;

        data = malloc(size);
        for (;;) {
            int max = size - len;

            n = fread(data + len, 1, max, f);
            if (n > 0) {
                len += n;
            }

            if (n != max) {
                break;
            }
            size *= 2;
            data = realloc(data, size);
        }
        data[len] = '\0';
        fclose(f);
    }
    return data;
}

static int read_config(const char *config_file) {
    int result = 0;
    struct json_tokener *tok;
    json_object *new_obj;

    char *data = read_whole_file(config_file);
    if (data == NULL) {
        fprintf(stderr, "Error reading configuration file %s\n", config_file);
        return -1;
    }

    tok = json_tokener_new();
    tok->err = json_tokener_success;
    new_obj = json_tokener_parse_ex(tok, data, strlen(data));

    if (tok->err != json_tokener_success) {
        int nline = 0, npos = 0;
        int char_offset = tok->char_offset;
        char *line = data, *p = data;
        while (*p != '\0') {
            if (*p == '\n') {
                *p = '\0';
                if (char_offset >= 0) {
                    line = p + 1;
                    nline++;
                    npos = 0;
                }
            }
            if (char_offset >= 0) {
                npos++;
            }
            char_offset--;
            p++;
        }
 fprintf(stderr, "Error: %s\nConfiguration file %s line %d position %d:\n%s\n",
                json_tokener_errors[tok->err], config_file, nline, npos, line);
        result = -1;
    } else {
        json_object *obj;
        obj = json_object_object_get(new_obj, "listen");
        if (json_object_is_type(obj, json_type_string)) {
            cfg_listen = json_object_get_string(obj);
        }
        obj = json_object_object_get(new_obj, "urls");
        if (json_object_is_type(obj, json_type_array)) {
            int i;
            cfg_nurls = json_object_array_length(obj);
            cfg_urls = calloc(sizeof(url_t), cfg_nurls);

            for (i = 0; i < cfg_nurls; i++) {
                json_object *url = json_object_array_get_idx(obj, i);
                if (json_object_is_type(url, json_type_object)) {
                    json_object *value;

                    value = json_object_object_get(url, "url");
                    if (json_object_is_type(value, json_type_string)) {
                        cfg_urls[i].url = json_object_get_string(value);
                        if (regcomp(&cfg_urls[i].re_url, cfg_urls[i].url, REG_EXTENDED) != 0) {
                        }
                    }

#define GET_STRING_VALUE(name) do { \
            value = json_object_object_get(url, #name); \
            if (value == NULL) { \
                value = json_object_object_get(new_obj, #name); \
            } \
            if (json_object_is_type(value, json_type_string)) { \
                cfg_urls[i].name = json_object_get_string(value); \
            } \
        } while (0)
#define GET_NUMBER_VALUE(name) do { \
            value = json_object_object_get(url, #name); \
            if (value == NULL) { \
                value = json_object_object_get(new_obj, #name); \
            } \
            if (json_object_is_type(value, json_type_int)) { \
                cfg_urls[i].name = json_object_get_int(value); \
            } \
        } while (0)

                    GET_STRING_VALUE(pageUrl);
                    GET_STRING_VALUE(swfUrl);
                    GET_STRING_VALUE(app);
                    GET_STRING_VALUE(playpath);
                    GET_STRING_VALUE(rtmp);
                    GET_STRING_VALUE(flashVer);
                    GET_NUMBER_VALUE(clients_per_thread);
                }
            }
        }
    }

    json_tokener_free(tok);
    free(data);
    return result;
}

static void usage(char *argv0) {
 fprintf(stdout, PACKAGE_NAME " version " PACKAGE_VERSION "\n");
    fprintf(stdout, "Usage: %s [-V] -f <configuration file>\n", argv0);
}

int main(int argc, char *argv[]) {
    pid_t childpid, daemonpid;
    int status;

    const char *cfgfile;
    const char *pidfile;
    extern char *optarg;
    FILE *pidf;
    int x;

    if (argc < 2) {
        usage(argv[0]);
        exit(-1);
    }

    while ((x = getopt(argc, argv, "Vf:p:")) != EOF) {
        switch (x) {
        case 'f':
            cfgfile = optarg;
            break;
        case 'p':
            pidfile = optarg;
            break;
        case 'V':
fprintf(stdout, PACKAGE_NAME " version " PACKAGE_VERSION "\n");
            exit(0);
            break;

        default:
            fprintf(stderr, "%s: unknown argument \'%c\'\n", argv[0], (char)x);
            usage(argv[0]);
            exit(-1);
            break;
        }
    }

    if (read_config(cfgfile) == -1) {
        exit(-1);
    }

    (void)unlink(pidfile);
    pidf = fopen(pidfile, "w");
    if (pidfile == NULL) {
        fprintf(stderr, "Failed to open pidfile %s (%s)\n", pidfile, strerror(errno));
        exit(-1);
    }

    daemonpid = fork();
    if (daemonpid == -1) {
        perror("fork() failed");
        abort();
    } else if (daemonpid != 0) {
        fprintf(pidf, "%d", (int)daemonpid);
        fclose(pidf);
        exit(0);
    }

    fclose(pidf);
    setsid();
 
    while (1) {
        childpid = fork();
        if (childpid == -1) {
            perror("fork() failed");
            abort();
        } else if (childpid == 0) {
            serve();
            break;
        } else {
            wait(&status);
            fprintf(stderr, "Child process %d died, restarting\n", childpid);
            sleep(1);
        }
    }
    return 0;
}
