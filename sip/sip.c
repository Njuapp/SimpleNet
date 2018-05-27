//文件名: sip/sip.c
//
//描述: 这个文件实现SIP进程  
//
//创建日期: 2018年

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "sip.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//SIP层等待这段时间让SIP路由协议建立路由路径. 
#define SIP_WAITTIME 60

/**************************************************************/
//声明全局变量
/**************************************************************/
int son_conn; 			//到重叠网络的连接
int stcp_conn;			//到STCP的连接
nbr_cost_entry_t* nct;			//邻居代价表
dv_t* dv;				//距离矢量表
pthread_mutex_t* dv_mutex;		//距离矢量表互斥量
routingtable_t* routingtable;		//路由表
pthread_mutex_t* routingtable_mutex;	//路由表互斥量

/**************************************************************/
//实现SIP的函数
/**************************************************************/

//SIP进程使用这个函数连接到本地SON进程的端口SON_PORT.
//成功时返回连接描述符, 否则返回-1.
int connectToSON() {
	struct sockaddr_in sonaddr;
	sonaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	sonaddr.sin_family = AF_INET;
	sonaddr.sin_port = htons(SON_PORT);
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(!connect(sockfd, (struct sockaddr*)&sonaddr, sizeof(sonaddr)))
		return sockfd;
	else
		return -1;
}

//这个线程每隔ROUTEUPDATE_INTERVAL时间发送路由更新报文.路由更新报文包含这个节点
//的距离矢量.广播是通过设置SIP报文头中的dest_nodeID为BROADCAST_NODEID,并通过son_sendpkt()发送报文来完成的.
void* routeupdate_daemon(void* arg) {
	sip_pkt_t pkt;
	pkt.header.type = ROUTE_UPDATE;
	pkt.header.length = sizeof(seg_t);
	pkt.header.dest_nodeID = BROADCAST_NODEID;
	pkt.header.src_nodeID = topology_getMyNodeID();
	//TODO:construct routing packet

	while (son_conn != -1)
	{
		printf("=SEND A BROADCAST OUT=\n");
		if(son_conn != -1)
			son_sendpkt(BROADCAST_NODEID, &pkt, son_conn);
		sleep(ROUTEUPDATE_INTERVAL);
	}
	pthread_exit(NULL);
}

//这个线程处理来自SON进程的进入报文. 它通过调用son_recvpkt()接收来自SON进程的报文.
//如果报文是SIP报文,并且目的节点就是本节点,就转发报文给STCP进程. 如果目的节点不是本节点,
//就根据路由表转发报文给下一跳.如果报文是路由更新报文,就更新距离矢量表和路由表.
void* pkthandler(void* arg) {
	sip_pkt_t* pkt = (sip_pkt_t*)malloc(sizeof(sip_pkt_t));
	while(son_recvpkt(pkt,son_conn)>0) {
		if(pkt->header.type == ROUTE_UPDATE){
			//TODO:update the routing table
			printf("Routing: received ROUTE_UPDATE from neighbor %d\n",pkt->header.src_nodeID);
		}
		else if(pkt->header.type == SIP){
			if(pkt->header.dest_nodeID == topology_getMyNodeID()){
				seg_t *recvseg = (seg_t *)(&pkt->data);
				if(stcp_conn!=-1){
					forwardsegToSTCP(stcp_conn, pkt->header.src_nodeID, recvseg);
					printf("Forward a data from SRC(%d) to STCP\n", pkt->header.src_nodeID);
				}
			}
			else{
				int nextID = routingtable_getnextnode(routingtable, pkt->header.dest_nodeID);
				if(son_conn!=-1){
					son_sendpkt(nextID, pkt, son_conn);
					printf("Routing: [%d->%d->%d]\n", pkt->header.src_nodeID, topology_getMyNodeID(), nextID);
				}
			}
		}
		else{
			fprintf(stderr, "Recevied something wrong with type of packet\n");
		}
	}
	close(son_conn);
	son_conn = -1;
	pthread_exit(NULL);
}

//这个函数终止SIP进程, 当SIP进程收到信号SIGINT时会调用这个函数. 
//它关闭所有连接, 释放所有动态分配的内存.
void sip_stop() {
	fprintf(stderr, "IN SIP_STOP!\n");
	while (close(son_conn) != 0)
	{
	}
}

//这个函数打开端口SIP_PORT并等待来自本地STCP进程的TCP连接.
//在连接建立后, 这个函数从STCP进程处持续接收包含段及其目的节点ID的sendseg_arg_t. 
//接收的段被封装进数据报(一个段在一个数据报中), 然后使用son_sendpkt发送该报文到下一跳. 下一跳节点ID提取自路由表.
//当本地STCP进程断开连接时, 这个函数等待下一个STCP进程的连接.
void waitSTCP() {
	struct sockaddr_in listenaddr;
	listenaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	listenaddr.sin_family = AF_INET;
	listenaddr.sin_port = SIP_PORT;
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	bind(listenfd, (struct sockaddr *)&listenaddr, sizeof(listenaddr));
	
	while (1)
	{
		listen(listenfd, 1);
		struct sockaddr_in stcpaddr;
		socklen_t socklen = sizeof(stcpaddr);
		int stcp_conn = accept(listenfd, (struct sockaddr *)&stcpaddr, &socklen);
		int destID;
		seg_t seg;
		while (getsegToSend(stcp_conn, &destID, &seg) > 0){
			sip_pkt_t *pkt = (sip_pkt_t *)malloc(sizeof(sip_pkt_t));
			pkt->header.src_nodeID = topology_getMyNodeID();
			pkt->header.dest_nodeID = destID;
			pkt->header.type = SIP;
			pkt->header.length = sizeof(seg_t);
			memcpy(&pkt->data, &seg, sizeof(seg_t));
			int nextID = routingtable_getnextnode(routingtable, destID);
			son_sendpkt(nextID, pkt, son_conn);
		}
	}
  return;
}

int main(int argc, char *argv[]) {
	printf("SIP layer is starting, pls wait...\n");

	//初始化neighbor cost table
	nct = nbrcosttable_create();
	nbrcosttable_print(nct);
	return 0;
	//初始化distance vector table
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	dvtable_print(dv);

	//初始化routing table
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	routingtable_print(routingtable);

	son_conn = -1;
	stcp_conn = -1;

	//注册用于终止进程的信号句柄
	signal(SIGINT, sip_stop);

	//连接到本地SON进程 
	son_conn = connectToSON();
	if(son_conn<0) {
		printf("can't connect to SON process\n");
		exit(1);		
	}
	
	//启动线程处理来自SON进程的进入报文 
	pthread_t pkt_handler_thread; 
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//启动路由更新线程 
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

	printf("SIP layer is started...\n");
	printf("waiting for routes to be established\n");
	sleep(SIP_WAITTIME);
	routingtable_print(routingtable);

	//等待来自STCP进程的连接
	printf("waiting for connection from STCP process\n");
	waitSTCP(); 

}


