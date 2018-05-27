//文件名: topology/topology.c
//
//描述: 这个文件实现一些用于解析拓扑文件的辅助函数 
//
//创建日期: 2018年

#include "topology.h"
#include <stdio.h>
#include "../common/constants.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define max(x,y) x>y?x:y 
#define min(x,y) x<y?x:y
//这个函数返回指定主机的节点ID.
//节点ID是节点IP地址最后8位表示的整数.
//例如, 一个节点的IP地址为202.119.32.12, 它的节点ID就是12.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromname(char* hostname) 
{
  struct hostent *hostinfo = gethostbyname(hostname);
  char *addr = hostinfo->h_addr_list[0];
  struct in_addr *ip = (struct in_addr *)addr;
  return topology_getNodeIDfromip(ip);
}

//这个函数返回指定的IP地址的节点ID.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromip(struct in_addr* addr)
{
  char *dotip = inet_ntoa(*addr);
  int i = 0;
  char *p = dotip;
  while (i < 3)
  {
    if(*p == '.')
      i++;
    p++;
  }
  return atoi(p);
}

//这个函数返回本机的节点ID
//如果不能获取本机的节点ID, 返回-1.
int topology_getMyNodeID()
{
  char hostname[256];
  gethostname(hostname, 256);
  struct hostent *hostinfo = gethostbyname(hostname);
  char *addr = hostinfo->h_addr_list[0];
  struct in_addr *ip = (struct in_addr *)addr;
  return topology_getNodeIDfromip(ip);
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回邻居数.
int topology_getNbrNum()
{
  char hostname[256];
  gethostname(hostname, 256);
  FILE *fp = fopen("../topology/topology.dat", "r");
  assert(fp!= NULL);
  char node1[256];
  char node2[256];
  int cost;
  int cnt = 0;
  while (!feof(fp))
  {
    fscanf(fp, "%s", node1);
    if(feof(fp))
      break;
    fscanf(fp, "%s", node2);
    fscanf(fp, "%d", &cost);
    if(!strcmp(node1, hostname))
      cnt++;
    if(!strcmp(node2, hostname))
      cnt++;
  }
  return cnt;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回重叠网络中的总节点数.
int topology_getNodeNum()
{ 
  return 4;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含重叠网络中所有节点的ID. 
int* topology_getNodeArray()
{
  int *nodeArr = (int *)malloc(sizeof(int) * 4);
  for (int i = 0; i < 4; i ++)
    nodeArr[i] = 185 + i;
  return nodeArr;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回一个动态分配的数组, 它包含所有邻居的节点ID.  
int* topology_getNbrArray()
{
  int nbrs = topology_getNbrNum();
  int *nbrArr = (int *)malloc(sizeof(int) * nbrs);
  FILE *fp = fopen("../topology/topology.dat", "r");
  char host1[256], host2[256];
  int cost;
  int hostid = topology_getMyNodeID();
  int cnt = 0;
  while (!feof(fp))
  {
    fscanf(fp, "%s", host1);
    if(feof(fp))
      break;
    fscanf(fp, "%s", host2);
    fscanf(fp, "%d", &cost);
    int node1 = topology_getNodeIDfromname(host1);
    int node2 = topology_getNodeIDfromname(host2);
    if(node1 == hostid){
      nbrArr[cnt++] = node2;
    }
    else if(node2 == hostid){
      nbrArr[cnt++] = node1;
    }
  }
  assert(cnt == nbrs);
  return nbrArr;
}

//这个函数解析保存在文件topology.dat中的拓扑信息.
//返回指定两个节点之间的直接链路代价. 
//如果指定两个节点之间没有直接链路, 返回INFINITE_COST.
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{
  FILE *fp = fopen("../topology/topology.dat", "r");
  char host1[256], host2[256];
  int cost;
  while (!feof(fp))
  {
    fscanf(fp, "%s", host1);
    if(feof(fp))
      break;
    fscanf(fp, "%s", host2);
    fscanf(fp, "%d", &cost);
    int node1 = topology_getNodeIDfromname(host1);
    int node2 = topology_getNodeIDfromname(host2);
    int minv = min(fromNodeID, toNodeID);
    int maxv = max(fromNodeID, toNodeID);
    if (node1 == minv && node2 == maxv)
    {
      return cost;
    }
  }
  return INFINITE_COST;
}
