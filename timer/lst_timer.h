#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

#include <vector>
#include <unordered_map>

class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    // util_timer *timer;
};


class util_timer
{
public:
    util_timer() {}
    
public:
    int sockfd;
    time_t expire;
    
    void (* cb_func)(client_data *);
    client_data *user_data;
    bool operator<(const util_timer& t) {    // 重载比较运算符
        return expire < t.expire;
    }
    bool operator>(const util_timer& t) {    // 重载比较运算符
        return expire > t.expire;
    }
};

class sort_timer_lst
{
public:
    sort_timer_lst() {min_heap.reserve(1024);}
    ~sort_timer_lst() {clear();}

    void add_timer(util_timer timer);
    void adjust_timer(util_timer timer);
    void del_timer(util_timer timer);
    
    void clear();
    util_timer top();
    void pop();
    util_timer* get_timer(int sockfd);

    void tick();

    size_t size();
private:
    void swap_node(size_t i, size_t j);
    bool sift_down(size_t i, size_t n);
    void sift_up(size_t i);
    
    vector<util_timer> min_heap;
    unordered_map<int, size_t> ref_table;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
