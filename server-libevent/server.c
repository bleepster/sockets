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
#include <event.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define MAX_CONNECTIONS 10


typedef struct _connections {
  struct event e;
  struct event_base *b;
} connections;


typedef struct _run_data
{
    int stype;
    int s;
    socklen_t saddr_sz;
    struct event_base *main_event;
    int conn_count;
    struct sockaddr_storage saddr_s;
    struct sockaddr_storage peer_s;
} run_data;


void recv_data(int fd, short event, void *arg)
{
    int recv_sz = 0;
    char recv_buff[256];
    connections *con = (connections *)arg;
    
    memset(recv_buff, '\0', sizeof(recv_buff));
    recv_sz = recv(fd, (void *)recv_buff, sizeof(recv_buff), 0); 
    if(recv_sz > 0) {
        DPRINT(DPRINT_DEBUG, "[%s] received\n'''\n%s\n'''\n",
            __FUNCTION__, recv_buff);
    }
    else {
        DPRINT(DPRINT_DEBUG, "[%s] closing socket [%d]", __FUNCTION__, fd);
        close(fd);
        event_del(&con->e);
        free(con);
    }
}


void cons_read(int fd, short event, void *arg)
{
    int recv_sz = 0;
    char read_buff[256];
    run_data *rd = (run_data *)arg;
    
    recv_sz = read(fd, (void *)read_buff, sizeof(read_buff));
    if(recv_sz > 0) {
        if(recv_sz == 1 && read_buff[0] == '\n') {
            event_base_loopbreak(rd->main_event);
        }
    }
}


void accept_conn(int fd, short event, void *arg)
{
    int new_conn;
    socklen_t sz;
    connections *con = NULL;
    run_data *rd = (run_data *)arg;
    
    sz = rd->saddr_sz;
    new_conn = accept(fd, (struct sockaddr *)&rd->peer_s, &sz);
    if(new_conn > 0 && rd->conn_count < MAX_CONNECTIONS) {
        con = (connections *) calloc(1, sizeof(connections));
        if(con == NULL) {
            DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        }
        else {
            DPRINT(DPRINT_DEBUG, "[%s] accepting connection, socket [%d]", 
                __FUNCTION__, new_conn);
            con->b = rd->main_event; 
            event_set(&con->e, new_conn, (EV_READ | EV_PERSIST), recv_data,
                (void *)con);
            event_base_set(rd->main_event, &con->e);
            event_add(&con->e, NULL);
            ++rd->conn_count;
      }
    }
}


int loop_tcp(run_data *rd)
{
    struct event *a_event = NULL;
    struct event *console_event = NULL;

    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    a_event = (struct event *) calloc(1, sizeof(struct event));
    if(a_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        return (1);
    }

    console_event = (struct event *) calloc(1, sizeof(struct event));
    if(console_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        return (1);
    }

    rd->main_event = event_base_new();
    if(rd->main_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] libevent error", __FUNCTION__);
        return (1);
    }

    DPRINT(DPRINT_DEBUG, "[%s] libevent using [%s]", __FUNCTION__,
        event_base_get_method(rd->main_event));

    if(listen(rd->s, 5) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] listen() failed", __FUNCTION__);
        return (1);
    }

    event_set(a_event, rd->s, (EV_READ | EV_PERSIST), accept_conn, 
        (void *)rd);
    event_base_set(rd->main_event, a_event);
    event_add(a_event, NULL);

    event_set(console_event, STDIN_FILENO, (EV_READ | EV_PERSIST), cons_read, 
        (void *)rd);
    event_base_set(rd->main_event, console_event);
    event_add(console_event, NULL);

    event_base_dispatch(rd->main_event);
    
    event_del(a_event);
    free(a_event);

    event_base_free(rd->main_event);

    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return (0);
}


int loop_udp(run_data *rd)
{
    struct event *a_event = NULL;
    struct event *console_event = NULL;

    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    a_event = (struct event *) calloc(1, sizeof(struct event));
    if(a_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        return (1);
    }

    console_event = (struct event *) calloc(1, sizeof(struct event));
    if(console_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        return (1);
    }

    rd->main_event = event_base_new();
    if(rd->main_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] libevent error", __FUNCTION__);
        return (1);
    }

    DPRINT(DPRINT_DEBUG, "[%s] libevent using [%s]", __FUNCTION__,
        event_base_get_method(rd->main_event));

    event_set(a_event, rd->s, (EV_READ | EV_PERSIST), recv_data, 
        (void *)rd);
    event_base_set(rd->main_event, a_event);
    event_add(a_event, NULL);

    event_set(console_event, STDIN_FILENO, (EV_READ | EV_PERSIST), cons_read, 
        (void *)rd);
    event_base_set(rd->main_event, console_event);
    event_add(console_event, NULL);

    event_base_dispatch(rd->main_event);
    
    event_del(a_event);
    free(a_event);

    event_base_free(rd->main_event);

    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return (0);
}


int run(run_data *rd)
{
    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    rd->s = socket(rd->saddr_s.ss_family, rd->stype, 0);
    if(rd->s < 0) {
        DPRINT(DPRINT_ERROR, "[%s] socket() failed", __FUNCTION__);
        return (1);
    }
  
    if(bind(rd->s, (struct sockaddr *)&rd->saddr_s, rd->saddr_sz) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] bind() failed", __FUNCTION__);
        close(rd->s);
        return (1);
    }

    if(rd->stype == SOCK_STREAM) {
        loop_tcp(rd);
    }
    else if(rd->stype == SOCK_DGRAM) {
        loop_udp(rd);
    }

    close(rd->s);
    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return (0);
}


int run4(char *ip, int port, int stype)
{
    socklen_t sz;
    struct sockaddr_in si;
    run_data rd;

    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    memset(&si, 0, sizeof(struct sockaddr_in));
    sz = sizeof(struct sockaddr_in);
    si.sin_family = AF_INET;
    si.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, (void *)&si.sin_addr) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] inet_pton() failed", __FUNCTION__);
        return (1);
    }

    memset(&rd, 0, sizeof(run_data));
    rd.saddr_sz = sz;
    rd.stype = stype;
    memcpy(&rd.saddr_s, &si, rd.saddr_sz);

    run(&rd);
    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return (0);
}


int run6(char *ip, int port, int stype)
{
    socklen_t sz;
    struct sockaddr_in6 si;
    run_data rd;

    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    memset(&si, 0, sizeof(struct sockaddr_in6));
    sz = sizeof(struct sockaddr_in6);
    si.sin6_family = AF_INET6;
    si.sin6_port = htons(port);
    if(inet_pton(AF_INET6, ip, (void *)&si.sin6_addr) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] inet_pton() failed", __FUNCTION__);
        return (1);
    }
  
    memset(&rd, 0, sizeof(run_data));
    rd.saddr_sz = sz;
    rd.stype = stype;
    memcpy(&rd.saddr_s, &si, rd.saddr_sz);

    run(&rd);
    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return (0);
}


int main(int argc, char *argv[])
{
    int port = 0;
    int ipver = 0;
    int stype = 0;
    int len = 0;
    char *ip = NULL;
    int opt;
  
    while((opt = getopt(argc, argv, "4:6:p:t:")) != -1) {
        switch(opt) {
          case '4':
              ip = argv[optind - 1];
              ipver = 4;
              break;

          case '6':
              ip = argv[optind - 1];
              ipver = 6;
              break;

          case 'p':
              port = (int) strtol(optarg, (char **)NULL, 10);
              break;

          case 't':
              len = strlen(argv[optind - 1]);

              if(!strncmp(argv[optind - 1], "tcp", len))
                  stype = SOCK_STREAM;
              else if(!strncmp(argv[optind - 1], "udp", len))
                  stype = SOCK_DGRAM;

              break;

          default:
              break;
        }
    }

    if(ip == NULL || port == 0 || stype == 0) {
        fprintf(stderr, 
            "usage: ./server [-4|-6] ip [-p] port [-t] (tcp|udp)\n");
        return (1);
    }

    switch(ipver) {
      case 4:
          run4(ip, port, stype);
          break;

      case 6:
          run6(ip, port, stype);
          break;

      default:
          break;
    }

    return (0);
}
