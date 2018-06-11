#ifndef USER_H_
#define USER_H_

#pragma pack(1)

#define MAX_USER 10

#include <pthread.h>
#include <unordered_map>
#include <string>
typedef unsigned char uint8_t;
//state is for user state, current_life is life during pk, initilized to 10
struct user_info
{
	int state; // 0 stands for offline, 1 stands for online, 2 stands for waiting PK, 3 stands for in PK(waiting user), 4 stands for in PK(waiting judge)
	int current_life;
	int sock;
	uint8_t signal;
};

//TCP data structure. category is for query type, user_name is for query user, pk_name 
//is for user_name ask for pk, info is for additional information.
struct tcp_data
{
	uint8_t category; 
	char user_name[50];
	char pk_name[50];
	uint8_t info;
	int remain_life;
};

struct user_data
{
	char name[50];
	int state;
};

struct tcp_broadcast
{
	uint8_t category;
	int user_num;
	struct user_data user[MAX_USER];
};

typedef struct user_info UserInfo;
typedef struct tcp_data TcpData;
typedef struct tcp_broadcast TcpBroadcast;

typedef union stcp_data{
	TcpBroadcast castdata;
	TcpData tcpdata;
} StcpData;

#endif
