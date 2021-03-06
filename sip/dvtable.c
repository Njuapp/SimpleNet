
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//这个函数动态创建距离矢量表.
//距离矢量表包含n+1个条目, 其中n是这个节点的邻居数,剩下1个是这个节点本身.
//距离矢量表中的每个条目是一个dv_t结构,它包含一个源节点ID和一个有N个dv_entry_t结构的数组, 其中N是重叠网络中节点总数.
//每个dv_entry_t包含一个目的节点地址和从该源节点到该目的节点的链路代价.
//距离矢量表也在这个函数中初始化.从这个节点到其邻居的链路代价使用提取自topology.dat文件中的直接链路代价初始化.
//其他链路代价被初始化为INFINITE_COST.
//该函数返回动态创建的距离矢量表.
int nbrs;
dv_t *dvtable_create()
{
  nbrs = topology_getNbrNum();
  dv_t *dv = (dv_t *)malloc(sizeof(dv_t) * (nbrs+1));
  int *nbrArr = topology_getNbrArray();
  int *nodeArr = topology_getNodeArray();
  for (int i = 0; i < nbrs; i++)
  {
    dv[i].nodeID = nbrArr[i];
    for (int j = 0; j < topology_getNodeNum(); j ++){
        dv[i].dvEntry[j].nodeID = nodeArr[j];
        dv[i].dvEntry[j].cost = INFINITE_COST;
    }
  }
  dv[nbrs].nodeID = topology_getMyNodeID();
  for (int j = 0; j < topology_getNodeNum(); j ++){
    dv[nbrs].dvEntry[j].nodeID = nodeArr[j];
    if(dv[nbrs].nodeID == nodeArr[j])
      dv[nbrs].dvEntry[j].cost = 0;
    else
      dv[nbrs].dvEntry[j].cost = topology_getCost(dv[nbrs].nodeID, nodeArr[j]);
  }
  free(nbrArr);
  free(nodeArr);
  return dv;
}

//这个函数删除距离矢量表.
//它释放所有为距离矢量表动态分配的内存.
void dvtable_destroy(dv_t* dvtable)
{
  free(dvtable);
}

//这个函数设置距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,并且链路代价也被成功设置了,就返回1,否则返回-1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
  return 0;
}

//这个函数返回距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,就返回链路代价,否则返回INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
  return 0;
}

//这个函数打印距离矢量表的内容.
void dvtable_print(dv_t* dvtable)
{
  printf("--------------------------\n");
  printf("| DV |");
  for (int j = 0; j < topology_getNodeNum(); j++)
    printf("%4d|", dvtable[0].dvEntry[j].nodeID);
  printf("\n");
  for (int i = 0; i <= nbrs; i++)
  {
    printf("|%4d|", dvtable[i].nodeID);
    for (int j = 0; j < topology_getNodeNum(); j++)
    {
      printf("%4d|", dvtable[i].dvEntry[j].cost);
    }
    printf("\n");
  }
  printf("--------------------------\n");
}
