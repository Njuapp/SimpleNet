
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

int nbrs;
//这个函数动态创建邻居代价表并使用邻居节点ID和直接链路代价初始化该表.
//邻居的节点ID和直接链路代价提取自文件topology.dat.
nbr_cost_entry_t* nbrcosttable_create()
{
  nbrs = topology_getNbrNum();
  nbr_cost_entry_t *nct = (nbr_cost_entry_t *)malloc(sizeof(nbr_cost_entry_t) * nbrs);
  FILE *fp = fopen("../topology/topology.dat", "r");
  char host1[256];
  char host2[256];
  char host[256];
  gethostname(host, 256);
  int cost;
  int cnt = 0;
  while (!feof(fp))
  {
    fscanf(fp, "%s", host1);
    if (feof(fp))
      break;
    fscanf(fp, "%s", host2);
    fscanf(fp, "%d", &cost);
    if(!strcmp(host, host1)){
      int nodeID = topology_getNodeIDfromname(host2);
      nct[cnt].nodeID = nodeID;
      nct[cnt].cost = cost;
      cnt++;
    }
    if(!strcmp(host, host2)){
      int nodeID = topology_getNodeIDfromname(host1);
      nct[cnt].nodeID = nodeID;
      nct[cnt].cost = cost;
      cnt++;
    }
  }
  fclose(fp);
  return nct;
}

//这个函数删除邻居代价表.
//它释放所有用于邻居代价表的动态分配内存.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
  free(nct);
  return;
}

//这个函数用于获取邻居的直接链路代价.
//如果邻居节点在表中发现,就返回直接链路代价.否则返回INFINITE_COST.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
  for (int i = 0; i < nbrs; i ++){
    if(nct[i].nodeID == nodeID){
      return nct[i].cost;
    }
  }
  return INFINITE_COST;
}

//这个函数打印邻居代价表的内容.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
  printf("----------\n");
  for (int i = 0; i < nbrs; i++)
  {
    printf("| %d | %d |\n", nct[i].nodeID, nct[i].cost);
  }
  printf("----------\n");
}
