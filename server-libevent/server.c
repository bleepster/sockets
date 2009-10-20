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

struct _event_group;

typedef struct _event_data_wrap {
  int fd;
  short eflags;
  void *params;
  struct timeval *tv;
  struct event event;
  struct _event_group *group;
  void (*callback)(int, short, void *arg);
  struct sockaddr_storage peer_s;
} event_data_wrap; 


typedef struct _event_group {
    struct event_base *b;
    int max;
    int cur;
    event_data_wrap *events[];
} event_group;


typedef struct _run_data
{
    int stype;
    int s;
    event_group *e_group;
    struct sockaddr_storage saddr_s;
} run_data;


int setup_event(event_data_wrap *e_wrap)
{
    /* TODO: add error checking in case something goes wrong... */
    event_set(&e_wrap->event, e_wrap->fd, e_wrap->eflags, e_wrap->callback, 
        e_wrap->params);
    event_base_set(e_wrap->group->b, &e_wrap->event);
    event_add(&e_wrap->event, e_wrap->tv);

    return (0);
}


int add_to_group(event_data_wrap *e_wrap)
{
    if(e_wrap->group != NULL && e_wrap->group->cur < e_wrap->group->max) {
        e_wrap->group->events[e_wrap->group->cur] = e_wrap;
        ++e_wrap->group->cur;
    }
    else {
        return (-1);
    }

    return (0);
}


int destroy_event(event_data_wrap *e_wrap)
{
    event_del(&e_wrap->event);
    --e_wrap->group->cur;
    free(e_wrap);

    return (0);
}


int setup_event_group(event_group **grp, int max)
{
   size_t size;
   
   size = sizeof(event_group) + (max * sizeof(event_data_wrap *));
   *grp = (event_group *) calloc(1, size);
   if((*grp) != NULL) {
       (*grp)->cur = 0;
       (*grp)->max = max;

       (*grp)->b = event_base_new();
       if((*grp)->b == NULL) {
          DPRINT(DPRINT_ERROR, "[%s] libevent error", __FUNCTION__);
          return (-1);
       }
   }
   else {
       DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
       return (-1);
   }

   return (0);
}


int destroy_event_group(event_group **grp)
{
    int i, max;
    
    max = (*grp)->cur;
    for(i = 0; i < max; ++i) {
        destroy_event((*grp)->events[i]);
    }

    event_base_free((*grp)->b);
    free(*grp);

    return (0);
}


void recv_data_tcp(int fd, short event, void *arg)
{
    int recv_sz = 0;
    char recv_buff[256];
    event_data_wrap *e_wrap = (event_data_wrap *)arg;
    
    memset(recv_buff, '\0', sizeof(recv_buff));
    recv_sz = recv(fd, (void *)recv_buff, sizeof(recv_buff), 0); 
    if(recv_sz > 0) {
        DPRINT(DPRINT_DEBUG, "[%s] received\n'''\n%s\n'''\n",
            __FUNCTION__, recv_buff);
    }
    else {
        DPRINT(DPRINT_DEBUG, "[%s] closing socket [%d]", __FUNCTION__, fd);
        close(fd);
        destroy_event(e_wrap);
    }
}


void recv_data_udp(int fd, short event, void *arg)
{
    int recv_sz = 0;
    socklen_t sz;
    char recv_buff[256];
    event_data_wrap *e_wrap = (event_data_wrap *)arg;
    
    memset(recv_buff, '\0', sizeof(recv_buff));
    sz = sizeof(struct sockaddr);
    recv_sz = recvfrom(fd, (void *)recv_buff, sizeof(recv_buff), 0,
        (struct sockaddr *)&e_wrap->peer_s, &sz); 
    if(recv_sz > 0) {
        DPRINT(DPRINT_DEBUG, "[%s] received\n'''\n%s\n'''\n",
            __FUNCTION__, recv_buff);
    }
}


void cons_read(int fd, short event, void *arg)
{
    int recv_sz = 0;
    char read_buff[256];
    struct event_base *b = (struct event_base *)arg;
    
    recv_sz = read(fd, (void *)read_buff, sizeof(read_buff));
    if(recv_sz > 0) {
        if(recv_sz == 1 && read_buff[0] == '\n') {
            event_base_loopbreak(b);
        }
    }
}


void accept_conn(int fd, short event, void *arg)
{
    int new_conn;
    socklen_t sz;
    event_data_wrap *recv_event = NULL;
    run_data *rd = (run_data *)arg;
    struct sockaddr_storage peer;
    
    sz = sizeof(struct sockaddr);
    memset(&peer, 0, sizeof(struct sockaddr_storage));
    new_conn = accept(fd, (struct sockaddr *)&peer, &sz);
    if(new_conn > 0) {
        recv_event = (event_data_wrap *) calloc(1, sizeof(event_data_wrap));
        if(recv_event == NULL) {
            DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        }
        else {
            recv_event->fd = new_conn;
            recv_event->eflags = (EV_READ | EV_PERSIST);
            recv_event->group = rd->e_group;
            recv_event->callback = recv_data_tcp;
            recv_event->tv = NULL;
            recv_event->params = recv_event;
            memcpy(&recv_event->peer_s, &peer, 
                sizeof(struct sockaddr_storage));

            if(setup_event(recv_event) < 0 || add_to_group(recv_event) < 0) {
                    DPRINT(DPRINT_ERROR, "[%s] unable to setup event", 
                    __FUNCTION__);

                close(new_conn);
                free(recv_event);
            }
            else {
                DPRINT(DPRINT_DEBUG, "[%s] connection accepted socket [%d]", 
                    __FUNCTION__, new_conn);
            }
         }
    }
}


int loop_tcp(run_data *rd)
{
    event_data_wrap *accept_event = NULL;
    event_data_wrap *console_event = NULL;

    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    accept_event = (event_data_wrap *) calloc(1, sizeof(event_data_wrap));
    if(accept_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        return (1);
    }
  
    accept_event->fd = rd->s;
    accept_event->eflags = (EV_READ | EV_PERSIST);
    accept_event->group = rd->e_group;
    accept_event->callback = accept_conn;
    accept_event->tv = NULL;
    accept_event->params = rd;

    if(setup_event(accept_event) < 0 || add_to_group(accept_event) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] unable to setup event", __FUNCTION__);
        return (1);
    }

    console_event = (event_data_wrap *) calloc(1, sizeof(event_data_wrap));
    if(console_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        return (1);
    }

    console_event->fd = STDIN_FILENO;
    console_event->eflags = (EV_READ | EV_PERSIST);
    console_event->group = rd->e_group;
    console_event->callback = cons_read;
    console_event->tv = NULL;
    console_event->params = rd->e_group->b;

    if(setup_event(console_event) < 0 || add_to_group(console_event) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] unable to setup event", __FUNCTION__);
        return (1);
    }

    if(listen(rd->s, 5) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] listen() failed", __FUNCTION__);
        return (1);
    }

    event_base_dispatch(rd->e_group->b);

    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return (0);
}


int loop_udp(run_data *rd)
{
    event_data_wrap *read_event = NULL;
    event_data_wrap *console_event = NULL;

    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    read_event = (event_data_wrap *) calloc(1, sizeof(event_data_wrap));
    if(read_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        return (1);
    }

    read_event->fd = rd->s;
    read_event->eflags = (EV_READ | EV_PERSIST);
    read_event->group = rd->e_group;
    read_event->callback = recv_data_udp;
    read_event->tv = NULL;
    read_event->params = read_event;

    if(setup_event(read_event) < 0 || add_to_group(read_event) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] unable to setup event", __FUNCTION__);
        return (1);
    }

    console_event = (event_data_wrap *) calloc(1, sizeof(event_data_wrap));
    if(console_event == NULL) {
        DPRINT(DPRINT_ERROR, "[%s] malloc() failed", __FUNCTION__);
        return (1);
    }

    console_event->fd = STDIN_FILENO;
    console_event->eflags = (EV_READ | EV_PERSIST);
    console_event->group = rd->e_group;
    console_event->callback = cons_read;
    console_event->tv = NULL;
    console_event->params = rd->e_group->b;

    if(setup_event(console_event) < 0 || add_to_group(console_event) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] unable to setup event", __FUNCTION__);
        return (1);
    }

    event_base_dispatch(rd->e_group->b);

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
  
    if(bind(rd->s, (struct sockaddr *)&rd->saddr_s, 
        sizeof(struct sockaddr)) < 0) {
            DPRINT(DPRINT_ERROR, "[%s] bind() failed", __FUNCTION__);
            close(rd->s);
            return (1);
    }

    if(setup_event_group(&rd->e_group, MAX_CONNECTIONS) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] unable to setup event groups", 
            __FUNCTION__);
        close(rd->s);
        return (1);
    }

    DPRINT(DPRINT_DEBUG, "[%s] libevent using [%s]", __FUNCTION__,
        event_base_get_method(rd->e_group->b));

    if(rd->stype == SOCK_STREAM) {
        loop_tcp(rd);
    }
    else if(rd->stype == SOCK_DGRAM) {
        loop_udp(rd);
    }

    destroy_event_group(&rd->e_group);

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
    rd.stype = stype;
    memcpy(&rd.saddr_s, &si, sizeof(struct sockaddr_storage));

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
    rd.stype = stype;
    memcpy(&rd.saddr_s, &si, sizeof(struct sockaddr_storage));

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
