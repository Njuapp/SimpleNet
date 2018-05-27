//文件名: son/neighbortable.c
//
//描述: 这个文件实现用于邻居表的API
//
//创建日期: 2018年

#include "neighbortable.h"
#include "../topology/topology.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

//这个函数首先动态创建一个邻居表. 然后解析文件topology/topology.dat, 填充所有条目中的nodeID和nodeIP字段, 将conn字段初始化为-1.
//返回创建的邻居表.
nbr_entry_t* nt_create()
{
  int myid = topology_getMyNodeID();
  FILE *fp = fopen("../topology/topology.dat","r");
  assert(fp);
  char node1[256];
  char node2[256];
  int cost;
  int nbrs = topology_getNbrNum();
  nbr_entry_t *nbr_table = (nbr_entry_t *)malloc(sizeof(nbr_entry_t) * nbrs);
  int cnt = 0;
  while (!feof(fp))
  {
    fscanf(fp, "%s", node1);
    if(feof(fp))
      break;
    fscanf(fp, "%s", node2);
    fscanf(fp, "%d", &cost);
    int id1 = topology_getNodeIDfromname(node1);
    int id2 = topology_getNodeIDfromname(node2);
    if (id1 == myid)
    {
      nbr_table[cnt].conn = -1;
      nbr_table[cnt].nodeID = id2;
      struct hostent *hostinfo = gethostbyname(node2);
      char* addr = hostinfo->h_addr_list[0];
      nbr_table[cnt].nodeIP = *((in_addr_t *)addr);
      cnt++;
    }
    else if(id2 == myid){
      nbr_table[cnt].conn = -1;
      nbr_table[cnt].nodeID = id1;
      struct hostent *hostinfo = gethostbyname(node1);
      char *addr = hostinfo->h_addr_list[0];
      nbr_table[cnt].nodeIP = *((in_addr_t *)addr);
      cnt++;
    }
  }
  assert(cnt == nbrs);
  return nbr_table;
}

//这个函数删除一个邻居表. 它关闭所有连接, 释放所有动态分配的内存.
void nt_destroy(nbr_entry_t* nt)
{
  int nbrs = topology_getNbrNum();
  for (int i = 0; i < nbrs; i ++){
    while(close(nt[i].conn)!=0){}
  }
}

//这个函数为邻居表中指定的邻居节点条目分配一个TCP连接. 如果分配成功, 返回1, 否则返回-1.
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
  int nbrs = topology_getNbrNum();
  for (int i = 0; i < nbrs; i++){
    if(nt[i].nodeID == nodeID){
      if(nt[i].conn == -1){
        nt[i].conn = conn;
      }
      else{
        return -1;
      }
      break;
    }
  }
  return -1;
}
