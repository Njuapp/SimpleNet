//文件名: son/son.c
//
//描述: 这个文件实现SON进程 
//SON进程首先连接到所有邻居, 然后启动listen_to_neighbor线程, 每个该线程持续接收来自一个邻居的进入报文, 并将该报文转发给SIP进程. 
//然后SON进程等待来自SIP进程的连接. 在与SIP进程建立连接之后, SON进程持续接收来自SIP进程的sendpkt_arg_t结构, 并将接收到的报文发送到重叠网络中. 
//
//创建日期: 2018年

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "son.h"
#include "../common/common.h"
#include "../topology/topology.h"
#include "neighbortable.h"

//你应该在这个时间段内启动所有重叠网络节点上的SON进程
#define SON_START_DELAY 10

/**************************************************************/
//声明全局变量
/**************************************************************/

//将邻居表声明为一个全局变量 
nbr_entry_t* nt; 
//将与SIP进程之间的TCP连接声明为一个全局变量
int sip_conn;
int nbrNum;
int myid;
/**************************************************************/
//实现重叠网络函数
/**************************************************************/

// 这个线程打开TCP端口CONNECTION_PORT, 等待节点ID比自己大的所有邻居的进入连接,
// 在所有进入连接都建立后, 这个线程终止. 
void* waitNbrs(void* arg) {
	int greater = 0;
	for (int i = 0; i < nbrNum; i ++){
		if(nt[i].nodeID > myid)
			greater++;
	}
	struct sockaddr_in listenaddr;
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	listenaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	listenaddr.sin_family = AF_INET;
	listenaddr.sin_port = htons(CONNECTION_PORT);
	bind(listenfd, (struct sockaddr *)&listenaddr, sizeof(listenaddr));
	listen(listenfd, nbrNum);
	struct sockaddr_in nbraddr;
	socklen_t socklen = sizeof(nbraddr);
	while(greater> 0 ){
		int sockfd = accept(listenfd, (struct sockaddr*) &nbraddr, & socklen);
		int nbr_id = topology_getNodeIDfromip(&nbraddr.sin_addr);
		printf("Accept connection from %d\n", nbr_id);
		for (int i = 0; i < nbrNum; i++)
		{
			if(nt[i].nodeID == nbr_id){
				assert(nt[i].conn == -1);
				nt[i].conn = sockfd;
				printf("Established connection from ID: %d\n", nbr_id);
				greater--;
				break;
			}
		}
	}
	pthread_exit(NULL);
}

// 这个函数连接到节点ID比自己小的所有邻居.
// 在所有外出连接都建立后, 返回1, 否则返回-1.
int connectNbrs() {
	for (int i = 0; i < nbrNum; i ++){
		if(myid > nt[i].nodeID){
			struct sockaddr_in nbraddr;
			nbraddr.sin_addr.s_addr = nt[i].nodeIP;
			nbraddr.sin_family = AF_INET;
			nbraddr.sin_port = htons(CONNECTION_PORT);
			int nbrsock = socket(AF_INET, SOCK_STREAM, 0);
			if(!connect(nbrsock, (struct sockaddr*)&nbraddr, sizeof(nbraddr))){
				nt[i].conn = nbrsock;
				printf("Established connected to ID: %d", nt[i].nodeID);
			}
			else{
				fprintf(stderr, "FAILED when connecting to ID: %d", nt[i].nodeID);
				return -1;
			}
		}
	}
	return 1;
}

//每个listen_to_neighbor线程持续接收来自一个邻居的报文. 它将接收到的报文转发给SIP进程.
//所有的listen_to_neighbor线程都是在到邻居的TCP连接全部建立之后启动的. 
void* listen_to_neighbor(void* arg) {
	int *idxp = (int *)arg;
	int idx = *idxp;
	sip_pkt_t pkt;
	int n = 0;
	while ((n = recvpkt(&pkt, nt[idx].conn)) > 0)
	{
		log("RECV A PACKET FROM %d <--\n", pkt.header.src_nodeID);
		if(sip_conn != -1)
			forwardpktToSIP(&pkt, sip_conn);
	}
	log("Unable to listen to %d, return code is %d\n", nt[idx].nodeID, n);
	if (n <= 0)
	{
		nt[idx].conn = -1;
		log("ENDED listen from %d\n", nt[idx].nodeID);
		pthread_exit(NULL);
	}
	return 0;
}

//这个函数打开TCP端口SON_PORT, 等待来自本地SIP进程的进入连接.
//在本地SIP进程连接之后, 这个函数持续接收来自SIP进程的sendpkt_arg_t结构, 并将报文发送到重叠网络中的下一跳.
//如果下一跳的节点ID为BROADCAST_NODEID, 报文应发送到所有邻居节点.
void waitSIP() {
	struct sockaddr_in listenaddr, sipaddr;
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	listenaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	listenaddr.sin_family = AF_INET;
	listenaddr.sin_port = htons(SON_PORT);
	bind(listenfd, (struct sockaddr *)&listenaddr, sizeof(listenaddr));
	listen(listenfd, 1);
	while(1){
	socklen_t socklen = sizeof(sipaddr);
	log("==waiting for connection from sip==\n");
	sip_conn = accept(listenfd, (struct sockaddr *)&sipaddr, &socklen);
	log("==ESTABLISHED connection from sip==\n");
	sip_pkt_t pkt;
	int nextnode;
	while (getpktToSend(&pkt, &nextnode, sip_conn) > 0){
		if(nextnode == BROADCAST_NODEID){
			log("SEND A BROADCAST PACKET TO-->\n");
			for (int i = 0; i < topology_getNbrNum(); i++){
				if(nt[i].conn != -1){
					log("%d ", nt[i].nodeID);
					sendpkt(&pkt, nt[i].conn);
				}
			}
			printf("\n");
		}
	}
	sip_conn = -1;
	log("Oops! DISCONNECTED FROM SIP !\n");
	}
	close(listenfd);
}

//这个函数停止重叠网络, 当接收到信号SIGINT时, 该函数被调用.
//它关闭所有的连接, 释放所有动态分配的内存.
void son_stop() {
	panic("SON_STOP");
	while (close(sip_conn) != 0)
	{
	}
	for (int i = 0; i < nbrNum; i ++){
		close(nt[i].conn);
	}
}

int main() {
	//启动重叠网络初始化工作
	myid = topology_getMyNodeID();
	printf("Overlay network: Node %d initializing...\n", myid);

	//创建一个邻居表
	nt = nt_create();
	//将sip_conn初始化为-1, 即还未与SIP进程连接
	sip_conn = -1;
	
	//注册一个信号句柄, 用于终止进程
	signal(SIGINT, son_stop);

	//打印所有邻居
	nbrNum = topology_getNbrNum();
	int i;
	for(i=0;i<nbrNum;i++) {
		printf("Overlay network: neighbor %d:%d\n",i+1,nt[i].nodeID);
	}

	//启动waitNbrs线程, 等待节点ID比自己大的所有邻居的进入连接
	pthread_t waitNbrs_thread;
	pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

	//等待其他节点启动
	sleep(SON_START_DELAY);
	
	//连接到节点ID比自己小的所有邻居
	connectNbrs();

	//等待waitNbrs线程返回
	pthread_join(waitNbrs_thread,NULL);	

	//此时, 所有与邻居之间的连接都建立好了
	//创建线程监听所有邻居
	for(i=0;i<nbrNum;i++) {
		int* idx = (int*)malloc(sizeof(int));
		*idx = i;
		pthread_t nbr_listen_thread;
		pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
	}
	printf("Overlay network: node initialized...\n");
	printf("Overlay network: waiting for connection from SIP process...\n");

	//等待来自SIP进程的连接
	waitSIP();
}
