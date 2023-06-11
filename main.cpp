#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<error.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include"locker.h"
#include"threadpool.h"
#include<signal.h>
#include"http_conn.h"
#include<errno.h>

#define MAX_FD 65535  //最多支持多大的并发
#define MAX_EVENT_NUMBER 10000  //监听最多的事件 

//添加信号捕捉
void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa, NULL);   

}

//添加文件描述符
extern void addfd(int epollfd, int fd, bool one_shot);

//从epoll 中删除文件描述符
extern void removefd(int epollfd, int fd);

//修改 描述符

extern void modfd(int epollfd, int fd, int ev);


int main(int argc, char* argv[])
{
    if(argc<=1)
    {
        //至少传入端口号
        printf("按照如下格式运行： %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    int port = atoi(argv[1]);

    //对信号处理
    addsig(SIGPIPE,SIG_IGN);

    //创建线程池 初始化线程
    threadpool<http_conn> *pool = NULL;

    try{
        pool = new threadpool<http_conn>;

    }catch(...){
        exit(-1);

    }


    //创建数组 保存所有客户端信息
    http_conn *users = new http_conn[MAX_FD];

    int listenfd = socket(PF_INET,SOCK_STREAM,0);

    //设置端口 复用
    int reuse = 1;
    int num = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if(num == -1)
    {
        perror("setsockopt");
        exit(-1);
    }
    //绑定
    struct sockaddr_in  addrress;
    addrress.sin_family = AF_INET;
    addrress.sin_addr.s_addr = INADDR_ANY;
    addrress.sin_port = htons(port);
    bind(listenfd,(struct  sockaddr*)&addrress, sizeof(addrress));


    //监听
    listen(listenfd,5);

    //创建epoll 对象， 事件数组

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    //监听的文件描述符 添加到epoll
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd = epollfd;

    while(true){
        epoll_wait(epollfd,events, MAX_EVENT_NUMBER, -1);
        if((num<0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }
    }

    //循环遍历事件数组
    for(int i = 0;i<num;i++){
        int sockfd = events[i].data.fd;
        if(sockfd == listenfd){
            //有客户端连接
            struct sockaddr_in client_address;
            socklen_t client_addrlen = sizeof(client_address);
            int connfd = accept(listenfd,(struct sockaddr*)&client_address, &client_addrlen);

            if(http_conn::m_user_count >= MAX_FD){
                //连接数满

                close(connfd);
                continue;
            }

            //新的客户数据初始化 放入数组
            users[connfd].init(connfd,client_address);

        }else if(events[i].events &(EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
            // 对方异常连接 断开
            users[sockfd].close_conn();
        }else if(events[i].events & EPOLLIN){
            if(users[sockfd].read()){
                //一次性读完所有数据
                pool->append(users+sockfd);
            }else{
                users[sockfd].close_conn();
            }

        }else if(events[i].events & EPOLLOUT){
            if(!users[sockfd].write()){
                //一次性写完所有数据
                users[sockfd].close_conn();
            }
        }
        
    }


    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;






    return 0;
}