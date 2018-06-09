#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#include "../common/user.h"
#include <set>
using namespace std;
extern unordered_map<string, struct user_info> user_table;

#define MAXLINE 4096
#define SERV_PORT 8888

void broadcast();
void *connection_handler(void *);

set<int> sock_set;

uint8_t rule(uint8_t p1, uint8_t p2)
{
	if(p1 == p2)
		return 0x03;
	else if((p1==0x01 && p2==0x02) || (p1==0x02 && p2==0x03) || (p1==0x03 && p2==0x01))
		return 0x02;
	else
		return 0x01;
}

int main()
{
	int socket_desc, client_sock, c;
	struct sockaddr_in server, client;

	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if(socket_desc == -1)
	{
		puts("Could not create socket");
		exit(-1);
	}
	puts("Socket created");

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(SERV_PORT);

	bind(socket_desc, (struct sockaddr *)&server, sizeof(server));
	puts("Bind done");

	listen(socket_desc, 20);
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);
	pthread_t thread_id;

	while((client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c)) != -1)
	{
		puts("Connection accepted");
		if(pthread_create(&thread_id, NULL, connection_handler, (void*) &client_sock) > 0)
		{
			perror("Could not create thread");
			exit(-1);
		}
		puts("Handler assigned");
	}
	perror("Accpet failed");
	close(socket_desc);
	return -1;
}

void* connection_handler(void* socket_desc)
{
	int sock = *(int*)socket_desc;
	int read_size;
	char *message;
	char client_message[MAXLINE];


	while((read_size = recv(sock, client_message, MAXLINE, 0)) > 0)
	{
		TcpData* data = (TcpData*)client_message;
		switch(data->category) {
			case 0x01:
				/* 0x01 stands for login
				first check whether there is an online user with same name
				if not, add the user into user_table or change user state to 1, else return bad request*/
				if(user_table.find(data->user_name)==user_table.end())
				{
					sock_set.insert(sock); // insert the socket into set
					UserInfo userinfo;
					userinfo.state = 1;
					userinfo.current_life = 10;
					userinfo.sock = sock;
					string str = data->user_name;
					user_table[str] = userinfo;
					data->info = 0x01; //success
					printf("%s\n", "Success login");
					printf("Total user number: %lu\n", user_table.size());
					write(sock, data, sizeof(TcpData));
					broadcast();
				}
				else if(user_table[data->user_name].state==0)
				{
					sock_set.insert(sock); // insert the socket into set
					user_table[data->user_name].state = 1;
					user_table[data->user_name].current_life = 10;
					data->info = 0x01; //success
					printf("%s\n", "Success login");
					printf("Total user number: %lu\n", user_table.size());
					write(sock, data, sizeof(TcpData));
					broadcast();
				}
				else
				{
					data->info = 0x02; //fail to login
					printf("%s\n", "Fail login");
					write(sock, data, sizeof(TcpData));
				}
				break;
			case 0x02: { //challenge
				int pk_sock = 0;
				user_table[data->user_name].state = 2;
				unordered_map<string, UserInfo>::iterator it = user_table.begin();
				while(it != user_table.end())
				{
					if(strcmp(&it->first[0], data->pk_name)==0)
					{
						if(it->second.state == 1)
						{
							it->second.state = 2; // waiting PK
							pk_sock = it->second.sock;
							break;
						}
					}
					it++;
				}
				if(it == user_table.end()) //No available pk user
				{
					user_table[data->user_name].state = 1;
					data->category = 0x03;
					data->info = 0x03;
					write(sock, data, sizeof(TcpData));
				}
				else
				{
					write(pk_sock, data, sizeof(TcpData));
				}
				break;
			}
			case 0x03: { //response to challenge
				int res_sock = 0;
				res_sock = user_table[data->user_name].sock;
				write(res_sock, data, sizeof(TcpData));
				if(data->info == 0x01)
				{
					user_table[data->user_name].state = 3;
					user_table[data->pk_name].state = 3;
					broadcast();
				}
				else if(data->info == 0x02)
				{
					user_table[data->user_name].state = 1;
					user_table[data->pk_name].state = 1;
				}
				break;
			}
			case 0x04: { 
				user_table[data->user_name].state = 4;
				user_table[data->user_name].signal = data->info;
				if(user_table[data->pk_name].state == 4) //Ready to judge
				{
					user_table[data->user_name].state = 3;
					user_table[data->pk_name].state = 3;
					uint8_t result = rule(data->info, user_table[data->pk_name].signal);
					int pk_sock = user_table[data->pk_name].sock;
					if(result==0x03) //tie
					{
						data->info = 0x03;
						data->remain_life = user_table[data->user_name].current_life;
						write(sock, data, sizeof(TcpData));
						data->remain_life = user_table[data->pk_name].current_life;
						write(pk_sock, data, sizeof(TcpData));
					}
					else if(result==0x02) //lose
					{
						data->info = 0x02;
						user_table[data->user_name].current_life -= 5;
						data->remain_life = user_table[data->user_name].current_life;
						if(data->remain_life<=0)
						{
							data->category = 0x05;
							user_table[data->user_name].state = 1;
							user_table[data->pk_name].state = 1;
							user_table[data->user_name].current_life = 10;
							user_table[data->pk_name].current_life = 10;
						}
						write(sock, data, sizeof(TcpData));
						data->remain_life = user_table[data->pk_name].current_life;
						data->info = 0x01;
						write(pk_sock, data, sizeof(TcpData));
					}
					else if(result==0x01) //win
					{
						data->info = 0x02;
						user_table[data->pk_name].current_life -= 5;
						data->remain_life = user_table[data->pk_name].current_life;
						if(data->remain_life<=0)
						{	
							data->category = 0x05;
							user_table[data->user_name].state = 1;
							user_table[data->pk_name].state = 1;
							user_table[data->user_name].current_life = 10;
							user_table[data->pk_name].current_life = 10;
						}
						write(pk_sock, data, sizeof(TcpData));
						data->remain_life = user_table[data->user_name].current_life;
						data->info = 0x01;
						write(sock, data, sizeof(TcpData));
					}
				}
				break;
			}
			case 0x10:
				broadcast();
				break;
			default:
				;
		}
		memset(client_message, 0, MAXLINE);
	}
	
	
	unordered_map<string, UserInfo>::iterator it = user_table.begin();
	while(it != user_table.end())
	{
		if(it->second.sock == sock)
		{
			if(it->second.state != 0)
			{
				it->second.state = 0; // offline
				printf("User %s offline\n", &it->first[0]);
				break;
			}
		}
		it++;
	}
	puts("Client disconnected");
	fflush(stdout);

	if(read_size == -1)
	{
		perror("Receive failed");
	}
	broadcast();
	sock_set.erase(sock); // Remove the socket from set
	close(sock);
	return 0;

}

// Broadcast all user info to all online user
void broadcast()
{
	int user_num = user_table.size();
	TcpBroadcast data;
	data.category = 0x10;
	data.user_num = user_num;
	int index = 0;
	unordered_map<string, UserInfo>::iterator it = user_table.begin();
	while(it != user_table.end())
	{
		data.user[index].state = it->second.state;
		strcpy(data.user[index].name, &it->first[0]);
		index++;
		it++;
	}
	unsigned int data_length = (5+54*index);
	set<int>::iterator it2 = sock_set.begin();
	while(it2 != sock_set.end())
	{
		write(*it2, &data, data_length);
		it2++;
	}

}

