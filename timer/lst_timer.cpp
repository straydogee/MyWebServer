#include "lst_timer.h"
#include "../http/http_conn.h"

void sort_timer_lst::add_timer(util_timer timer)
{
    size_t n = min_heap.size();
    min_heap.push_back(timer); 
    ref_table[timer.sockfd] = n;
    sift_up(n);
    // return &min_heap[ref_table[timer.sockfd]];
}

void sort_timer_lst::adjust_timer(util_timer timer)
{   
    int sockfd = timer.sockfd;
    assert(!min_heap.empty() && ref_table.count(sockfd));

    if (!sift_down(ref_table[sockfd], min_heap.size())) {
        sift_up(ref_table[sockfd]);
    }
}

void sort_timer_lst::del_timer(util_timer timer)
{
    int sockfd = timer.sockfd;
    if (min_heap.empty() && ref_table.count(sockfd) == 0) return;

    // assert(!min_heap.empty() && ref_table.count(sockfd));    

    size_t index = ref_table[sockfd];
    // 将要删除的结点换到队尾，然后调整堆
    size_t n = min_heap.size() - 1;
    // 如果就在队尾，就不用移动了
    if(index < min_heap.size() - 1) {
        swap_node(index, min_heap.size() - 1);
        if(!sift_down(index, n)) {
            sift_up(index);
        }
    }
    ref_table.erase(min_heap.back().sockfd);
    min_heap.pop_back();
}


void sort_timer_lst::clear()
{
    min_heap.clear();
    ref_table.clear();
}

util_timer sort_timer_lst::top()
{
    return min_heap.front();
}

void sort_timer_lst::pop()
{
    assert(!min_heap.empty());    
    del_timer(min_heap[0]);
}

void sort_timer_lst::swap_node(size_t i, size_t j) {
    assert(i >= 0 && i < min_heap.size());
    assert(j >= 0 && j < min_heap.size());
    swap(min_heap[i], min_heap[j]);
    ref_table[min_heap[i].sockfd] = i;    // 节点交换后，哈希表更新
    ref_table[min_heap[j].sockfd] = j;    
}

bool sort_timer_lst::sift_down(size_t i, size_t n) {
    assert(i >= 0 && i < min_heap.size());
    assert(n >= 0 && n <= min_heap.size());    // n:共几个结点
    auto index = i;
    auto child = 2 * index + 1; // 左孩子
    while(child < n) {
        // 左右孩子取最小的那个
        if(child + 1 < n && min_heap[child + 1] < min_heap[child]) {
            child++;
        }
        if(min_heap[child] < min_heap[index]) {
            swap_node(index, child);
            index = child;
            child = 2 * child + 1;
        } else {
            break;  // 需要跳出循环
        }
        
    }
    return index > i;  
}

void sort_timer_lst::sift_up(size_t i) {
    assert(i >= 0 && i < min_heap.size());
    size_t parent = (i - 1) / 2;
    while(parent >= 0) {
        if(min_heap[parent] > min_heap[i]) {
            swap_node(i, parent);
            i = parent;
            parent = (i - 1) / 2;
        } else {
            break;
        }
    } 
}

util_timer* sort_timer_lst::get_timer(int sockfd)
{
    if (ref_table.count(sockfd)) {
        return &min_heap[ref_table[sockfd]];
    }
    else {
        return nullptr;
    }
     
}

size_t sort_timer_lst::size() {
    return min_heap.size();
}

void sort_timer_lst::tick()
{
    if (!min_heap.size())
    {
        return;
    }
    
    time_t cur = time(NULL);

    while (!min_heap.empty())
    {
        util_timer tmp = top();
        if (cur < tmp.expire)
        {
            break;
        }
        tmp.cb_func(tmp.user_data);
        pop();
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();

    time_t cur = time(NULL);
    if (m_timer_lst.size()) {
        alarm(m_timer_lst.top().expire - cur);
    }
 
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
