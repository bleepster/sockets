/* necessary for getsubopt() on linux                                    */
/* man page for getsupobt(3) suggests using _XOPEN_SOURCE 500            */
/* but doing this breaks event.h code, googling throught mailing         */
/* led me to:                                                            */
/* http://www.linuxsa.org.au/pipermail/linuxsa/2005-February/077172.html */
/* which suggests using _GNU_SOURCE - apparently it works :)             */
#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <errno.h>

#if defined DEBUG_SYSLOG
  #include <syslog.h>
  #define DPRINT_DEBUG LOG_DEBUG
  #define DPRINT_ERROR LOG_ERR
  #define DPRINT(priority, text, args...) syslog(priority, text, ##args)
#elif defined DEBUG_CONSOLE
  #define DPRINT_DEBUG stdout
  #define DPRINT_ERROR stderr
  #define DPRINT(file, text, args...) fprintf(file, text"\n", ##args)
#else
  #define DPRINT_DEBUG
  #define DPRINT_ERROR
  #define DPRINT(x,y,z...)
#endif

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/time.h>
#include <time.h>
#include <event.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>

#define DEF_DELAY 1000000
#define DEF_INSTANCES 1


enum {
    SERVER = 0,
    CLIENT
};


char *const token[] = {
  [SERVER] = "server",
  [CLIENT] = "client",
  NULL
};


typedef struct _con_data
{
  struct sockaddr_storage bindAddr;
  struct sockaddr_storage servAddr;
  socklen_t bindAddrSize;
  int buf_len;
  int delay;
  int s;
  int ipver;
  pthread_t tid;
  int quit;
  pthread_mutex_t lock;
} con_data;


typedef struct _event_timeout
{
  struct event e;
  struct timeval tv;
} event_timeout;


int get_ip_subopt(char **server, char **client, char *arg)
{
    int unknown = 0;
    char *value = NULL;

    while(*arg != '\0' && !unknown)
    {
        switch(getsubopt(&arg, token, &value))
        {
            case SERVER:
                *server = value;
                break;

            case CLIENT:
                *client = value;
                break;

            default:
                unknown = 1;
                break;
        }
    }

    return (unknown);
}


int is_quit(con_data *cd_p)
{
    int ret;

    pthread_mutex_lock(&cd_p->lock);
    ret = cd_p->quit;
    pthread_mutex_unlock(&cd_p->lock);    

    return (ret);
}


int set_quit(con_data *cd_p)
{
    pthread_mutex_lock(&cd_p->lock);
    cd_p->quit =  1;
    pthread_mutex_unlock(&cd_p->lock);    
    
    return (0);
}


void print_usage(char *cmd)
{
    DPRINT(DPRINT_ERROR,"usage: %s [parameters]\n", cmd);
    DPRINT(DPRINT_ERROR,"required parameters:\n");
    DPRINT(DPRINT_ERROR,"\t[%s%s]\n", 
        "-4 server=<IPv4 address>,client=<IPv4 address>",
        " | -6 server=<IPv6 address>,client=<IPv6 address>");
    DPRINT(DPRINT_ERROR,"\t[-p <port number>]\n");
    DPRINT(DPRINT_ERROR,"optional parameters:\n");
    DPRINT(DPRINT_ERROR,"\t[-b <size of data>]\n");
    DPRINT(DPRINT_ERROR,"\t[-d <delay time>]\n");
    DPRINT(DPRINT_ERROR,"\t[-e <echo received data to client>]\n");
    DPRINT(DPRINT_ERROR,"\t[-t <duration (in seconds)>]\n");
}


void *cb_run_client(void *arg)
{
    char *buffer = NULL;
    con_data *cd_p = (con_data *)arg;

    do {
        cd_p->s = socket(cd_p->ipver, SOCK_DGRAM, 0);
        if(cd_p->s < 0) {
            break;
        }

        if(bind(cd_p->s, (struct sockaddr*)&cd_p->bindAddr, 
               cd_p->bindAddrSize) < 0) {
            break;
        }

        buffer = (char *) malloc(cd_p->buf_len);
        
        while(!is_quit(cd_p)) {
            sendto(cd_p->s, buffer, cd_p->buf_len, 0, 
                (struct sockaddr *)&cd_p->servAddr, cd_p->bindAddrSize);
            usleep(cd_p->delay);
        }

        close(cd_p->s);
        free(buffer);
 
        pthread_mutex_destroy(&cd_p->lock);
    } while(0);

    return (NULL);
}


void cb_keyboard_int(int fd, short event, void *arg)
{
    int recv_sz = 0;
    char read_buff[80];
    struct event_base *b = (struct event_base *)arg;
    
    recv_sz = read(fd, (void *)read_buff, sizeof(read_buff));
    if(recv_sz > 0) {
        if(recv_sz == 1 && read_buff[0] == '\n') {
            event_base_loopbreak(b);
        }
    }
}


void cb_timeout(int fd, short event, void *arg)
{
    struct event_base *b = (struct event_base *)arg;
    event_base_loopbreak(b);
}


int main(int argc, char **argv)
{
    char *sip = NULL;
    char *cip = NULL;
    int i, opt;
    int ipver = 0;
    int port = 0;
    int buf_len = 0;
    int delay = DEF_DELAY;
    time_t tm_out = 0;
    int instances = DEF_INSTANCES;

    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
    struct event_base *ebase_halt = NULL;
    struct event e_ki;

    event_timeout e_timeout;
    con_data cd;
    con_data *cd_p = NULL;

    /* TODO: add option to scpecify UDP or TCP */
    while((opt = getopt(argc, argv, "4:6:p:b:d:t:i:h")) != -1)
    {
        switch(opt) {
            case '4':
                ipver = AF_INET;
                if(get_ip_subopt(&sip, &cip, optarg) != 0) {
                    print_usage(argv[0]);
                    return (1);
                }
                break;

            case '6':
                ipver = AF_INET6;
                if(get_ip_subopt(&sip, &cip, optarg) != 0) {
                    print_usage(argv[0]);
                    return (1);
                }
                break;

            case 'p':
                port = (int) strtol(optarg, (char **)NULL, 10);
                break;

            case 'b':
                buf_len = (int) strtol(optarg, (char **)NULL, 10);
                break; 

            case 'd':
                delay = (int) strtol(optarg, (char **)NULL, 10);
                break;

            case 't':
                tm_out = (time_t) strtol(optarg, (char **)NULL, 10);
                break;

            case 'i':
                instances = (int) strtol(optarg, (char **)NULL, 10);
                break;

            case 'h':
                print_usage(argv[0]);
                return (0);
                break;
     
            default:
                print_usage(argv[0]);
                return (1);
                break;
        }
    }

    if(cip == NULL || sip == NULL || port == 0 || buf_len == 0) {
        DPRINT(DPRINT_ERROR,"parameters are not valid\n");
        print_usage(argv[0]);
        return (1);
    } 

    memset(&cd, 0, sizeof(con_data));
    cd.buf_len = buf_len;
    cd.delay = delay;
    cd.ipver = ipver;

    if(cd.ipver == AF_INET) {
        memset(&sin, 0, sizeof(struct sockaddr_in));

        sin.sin_family = AF_INET;
        if(inet_pton(AF_INET, sip, (void *)&sin.sin_addr) < 0) 
        {
            DPRINT(DPRINT_ERROR, "[%s] failed to convert [%s] address", 
                __FUNCTION__, sip);
            return (1);
        }
        sin.sin_port = htons(port);
        memcpy(&cd.servAddr, &sin, sizeof(struct sockaddr_storage));

        if(inet_pton(AF_INET, cip, (void *)&sin.sin_addr) < 0) {
            DPRINT(DPRINT_ERROR, "[%s] failed to convert [%s] address", 
                __FUNCTION__, cip);
            return (1);
        }

        sin.sin_port = htons(0);
        memcpy(&cd.bindAddr, &sin, sizeof(struct sockaddr_storage));

        cd.bindAddrSize = sizeof(struct sockaddr_in);
    }
    else if (cd.ipver == AF_INET6) {
        memset(&sin6, 0, sizeof(struct sockaddr_in6));

        sin6.sin6_family = AF_INET6;
        if(inet_pton(AF_INET6, sip, (void *)&sin6.sin6_addr) < 0) {
            DPRINT(DPRINT_ERROR, "[%s] failed to convert [%s] address", 
                __FUNCTION__, sip);
            return (1);
        }
        sin6.sin6_port = htons(port);
        memcpy(&cd.servAddr, &sin6, sizeof(struct sockaddr_storage));

        if(inet_pton(AF_INET6, cip, (void *)&sin6.sin6_addr) < 0) {
            DPRINT(DPRINT_ERROR, "[%s] failed to convert [%s] address", 
                __FUNCTION__, cip);
            return (1);
        }
        sin6.sin6_port = htons(0);
        memcpy(&cd.bindAddr, &sin6, sizeof(struct sockaddr_storage));

        cd.bindAddrSize = sizeof(struct sockaddr_in6);
    }

    ebase_halt = event_base_new();
    if(ebase_halt == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] unable to initialize event base", 
            __FUNCTION__);
        return (1);
    }

    /* initialize keyboard interupt event handler */
    event_set(&e_ki, STDIN_FILENO, (EV_READ | EV_PERSIST), 
        cb_keyboard_int, ebase_halt);
    event_base_set(ebase_halt, &e_ki);
    event_add(&e_ki, NULL);

    /* initialize keyboard timeout event handler */
    e_timeout.tv.tv_usec = 0;
    e_timeout.tv.tv_sec = tm_out;
    event_set(&e_timeout.e, -1, 0, cb_timeout, ebase_halt);
    event_base_set(ebase_halt, &e_timeout.e);
    event_add(&e_timeout.e, NULL);

    cd_p = (con_data *) calloc(instances, sizeof(con_data));
    for(i = 0; i < instances; ++i) {
        memcpy(&cd_p[i], &cd, sizeof(con_data));
        pthread_mutex_init(&cd_p[i].lock, NULL);
        pthread_create(&cd_p[i].tid, NULL, cb_run_client, &cd_p[i]);
    }

    /* this returns either on a timeout event or a keyboard event */
    event_base_dispatch(ebase_halt);

    for(i = 0; i < instances; ++i) {
        set_quit(&cd_p[i]);
    }

    free(cd_p);

    return (0);
}
