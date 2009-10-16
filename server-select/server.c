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

#include <sys/select.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define MAX_SLEEP_VAL 1000000
#define WAIT_RETRY 5


typedef struct _run_data
{
    int stype;
    socklen_t saddr_sz;
    struct sockaddr_storage saddr_s;
    struct sockaddr_storage peer_s;
}run_data;



/* this function is linux specific */
unsigned int get_sleep_val(void)
{
    struct timeval tv;
    struct drand48_data buff;
    long int res = 0;

    gettimeofday(&tv, NULL);
    srand48_r(tv.tv_usec, &buff);
    lrand48_r(&buff, &res);
  
    /* drop everything above one million, is this right? */
    return ((res >> 8) & (MAX_SLEEP_VAL)); 
}


int wait_for_incoming(int highest, fd_set *read_set)
{
    int ret, retry;
    struct timeval tv = {0, 0};
  
    DPRINT(DPRINT_DEBUG, "[%s] waiting for incoming connections....", 
        __FUNCTION__);

    for(retry = 0; retry < WAIT_RETRY; ++retry) {
        ret = 0;
        if((ret = select((highest+1), read_set, NULL, NULL, &tv)) != 0) {
          break;
        }

        usleep(get_sleep_val());
    }

    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return ret; 
}


int loop_tcp_recv(int s, int *max_fd, fd_set *rset_p)
{
    int con, recv_sz;
    char recv_buff[256];

    for(con = s + 1; con <= (* max_fd); ++con) {
        if(FD_ISSET(con, rset_p)) {
            memset(&recv_buff, 0, sizeof(recv_buff));
            recv_sz = recv(con, (void *)recv_buff, sizeof(recv_buff), 0);
            if(recv_sz <= 0) {
                close(con);
                FD_CLR(con, rset_p);

                if((*max_fd) == con) {
                    while(!FD_ISSET((*max_fd), rset_p)) {
                        --(*max_fd);
                    }
                }
            }
            else {
                 DPRINT(DPRINT_DEBUG, "[%s] received\n'''\n%s\n'''\n",
                     __FUNCTION__, recv_buff);
            }
        }
    }
    
    return (0);
}


int loop_tcp(int s, run_data *rd)
{
    int max_fd, ready, new_con;

    fd_set readset, readset_t;
    socklen_t sz;


    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    if(listen(s, 5) < 0) {
      DPRINT(DPRINT_ERROR, "[%s] listen() failed", __FUNCTION__);
      return (1);
    }

    FD_ZERO(&readset);
    FD_SET(s, &readset);

    max_fd = s;

    while(1) {
        memcpy(&readset_t, &readset, sizeof(fd_set));

        ready = wait_for_incoming(max_fd, &readset_t);
        if (ready <= 0) {
            continue;
        }

        if(FD_ISSET(s, &readset_t)) {
            sz = rd->saddr_sz;
            new_con = accept(s, (struct sockaddr *)&rd->peer_s, &sz);
            if(new_con < 0) {
                DPRINT(DPRINT_ERROR, "[%s] accept() failed", __FUNCTION__);
            }
            else {
                DPRINT(DPRINT_DEBUG, "[%s] new connection...", __FUNCTION__);

                if(new_con > max_fd) {
                    max_fd = new_con;
                }

                FD_SET(new_con, &readset);
            }
        }

        loop_tcp_recv(s, &max_fd, &readset_t);
    }

    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return (0);
}


int loop_udp(int s, run_data *rd)
{
    int max_fd, ready, recv_sz;
    char recv_buff[255];

    fd_set readset, readset_t;
    socklen_t sz;

    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    sz = rd->saddr_sz;

    FD_ZERO(&readset);
    FD_SET(s, &readset);
    max_fd = s;

    while(1) {
        memcpy(&readset_t, &readset, sizeof(fd_set));

        ready = wait_for_incoming(max_fd, &readset_t);
        if(ready <= 0) {
            continue;
        }

        if((recv_sz = recvfrom(s, (void *)recv_buff, sizeof(recv_buff), 0,
            (struct sockaddr *)&rd->peer_s, &sz)) > 0) {
            DPRINT(DPRINT_DEBUG, "[%s] received\n'''\n%s\n'''\n",
                __FUNCTION__, recv_buff);
        }
        else {
            DPRINT(DPRINT_ERROR, "[%s] recvfrom() failed", __FUNCTION__);
        }
    }

    DPRINT(DPRINT_DEBUG, "[%s] exiting...", __FUNCTION__);

    return (0);
}


int run(run_data *rd)
{
    int s;

    DPRINT(DPRINT_DEBUG, "[%s] starting...", __FUNCTION__);

    s = socket(rd->saddr_s.ss_family, rd->stype, 0);
    if(s < 0) {
        DPRINT(DPRINT_ERROR, "[%s] socket() failed", __FUNCTION__);
        return (1);
    }
  
    if(bind(s, (struct sockaddr *)&rd->saddr_s, rd->saddr_sz) < 0) {
        DPRINT(DPRINT_ERROR, "[%s] bind() failed", __FUNCTION__);
        close(s);
        return (1);
    }

    if(rd->stype == SOCK_STREAM) {
        loop_tcp(s, rd);
    }
    else if(rd->stype == SOCK_DGRAM) {
        loop_udp(s, rd);
    }

    close(s);
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
