#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>


#include "../common/user.h"
#include <iostream>
using namespace std;
extern "C" {
	#include "stcp_client.h"
}
#define MAXLINE 4096
#define SERV_PORT 8888

int state = -1;
char myName[50];
char pkName[50];

void printWelcome()
{
	system("clear");
	printf("Welcome to Battle Game online.\n");
	printf("(#) exit\t(h) help\n");
	printf("Please enter your user name:");
}

void printHelp()
{
	system("clear");
	printf("TODO: print help message\n");
	printf("(r) back\t(#) exit\n");
}

void* monitor_handler(void* socket_desc)
{
	char recvline[MAXLINE];
	int sock = *(int*)socket_desc;
	int read_size;
	while((read_size = recv(sock, recvline, MAXLINE, 0)) > 0)
	{

		TcpData* data = (TcpData*)recvline;
		switch(state){
			case 0:
				switch(data->category) {
					case 0x01:
						// 0x01 stands for login
						if(data->info == 0x01)
						{
							state = 3;
							printf("Successful login as %s\n", data->user_name);
							printf("%s\n", "(#) exit");
							strcpy(myName, data->user_name);
						}
						else if(data->info == 0x02)
						{
							printf("%s\n", "Fail login. There exists user with same name");
						}
						else
						{
							printf("%s\n", "Unknown login response");
						}
						break;

					default:
						;
				}
				break;
			case 3:
				switch(data->category) {
					case 0x10:
					{
						TcpBroadcast* broadcast_data = (TcpBroadcast*)recvline;
						system("clear");
						printf("(#) exit\n");
						int user_num = broadcast_data->user_num;
						printf("Total user:\t\t%d\n", user_num);
						int online_num = 0;
						for(int i=0; i<user_num; i++)
							if(broadcast_data->user[i].state != 0) //online
								online_num++;
						printf("Online user:\t\t%d\n", online_num);
						printf("%s\n", "###########################################");
						for(int i=0; i<user_num; i++)
						{
							printf("%s\t\t", broadcast_data->user[i].name);
							if(broadcast_data->user[i].state==0)
								printf("Offline\n");
							else if(broadcast_data->user[i].state==1)
								printf("Online\n");
							else if(broadcast_data->user[i].state==2)
								printf("Waiting for PK\n");
							else if(broadcast_data->user[i].state==3 || broadcast_data->user[i].state==4)
								printf("In PK\n");
							else
								printf("Unknown\n");
						}
						printf("Please enter user's name you want to request PK:\n");


						// 0x10 stands for updating user info
						break;
					}
					case 0x02: // challenge request
						state = 5;
						strcpy(pkName, data->user_name);
						printf("User %s is asking for PK. Enter y to accept or n to decline\n", data->user_name);
						break;
					default:
						;
				}
				break;
			case 4:
				switch(data->category) {
					case 0x03: //challenge response
						if(data->info == 0x03)
						{
							state = 3;
							printf("User %s is not available\n", data->pk_name);
						}
						else if(data->info == 0x02)
						{
							state = 3;
							printf("User %s declined your PK\n", data->pk_name);
						}
						else if(data->info == 0x01)
						{
							state = 6;
							system("clear");
							printf("User %s accepted your PK\n", data->pk_name);
							printf("Please enter 1, 2 or 3 to PK. 2 win 1;  3 win 2;  1 win 3\n");
						}
						break;
				}
				break;
			case 7:
				switch(data->category) {
					case 0x04: 
						if(data->info==0x03)
							printf("Result is Tie this time\n");
						else if(data->info==0x01)
							printf("You Win this time\n");
						else if(data->info==0x02)
							printf("You Lose this time\n");
						printf("Your remaining life is %d\n", data->remain_life);
						printf("Please enter 1, 2 or 3 to PK. 2 win 1;  3 win 2;  1 win 3\n");
						state = 6;
						break;
					case 0x05:
						if(data->info==0x01)
						{
							printf("Congratulations! You Win the PK\n");
							state = 3;
							std::cin.get();
						}
						else if(data->info==0x02)
						{
							printf("You Lose the PK. Good Luck next time\n");
							state = 3;
							std::cin.get();
						}
						data->category = 0x10; //ask for broadcast
						send(sock, data, sizeof(TcpData), 0);

						break;
				}
				break;
			default:
				;
		}
		memset(recvline, 0, MAXLINE);
	}
	return 0;

}


int main()
{
	stcp_client_init(0);
	int sockfd;
	struct sockaddr_in servaddr;
	char sendline[MAXLINE];

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(8888);

	connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
	state = 0;
	pthread_t thread_id;
	if(pthread_create(&thread_id, NULL, monitor_handler, (void*) &sockfd) > 0)
	{
		perror("Could not create thread");
		exit(-1);
	}

	printWelcome();
	while(fgets(sendline, MAXLINE, stdin) != NULL)
	{
		int i = strlen(sendline);
		if(sendline[i-1]=='\n')
			sendline[i-1] = '\0';
		if(strlen(sendline) == 0)
			continue;
		switch(state) {
			case 0:
				if(strcmp(sendline, "#")==0)
				{
					close(sockfd);
					exit(0);
				}
				else if(strcmp(sendline, "h")==0)
				{
					printHelp();
					state = 2;
				}
				else
				{
					//send login request
					TcpData data;
					data.category = 0x01;
					strcpy(data.user_name, sendline);
					send(sockfd, (char*)&data, sizeof(TcpData), 0);
				}
				break;
			case 2:
				if(strcmp(sendline, "#")==0)
				{
					close(sockfd);
					exit(0);
				}
				else if(strcmp(sendline, "r")==0)
				{
					printWelcome();
					state = 0;
				}
				break;
			case 3:
				if(strcmp(sendline, "#")==0)
				{
					close(sockfd);
					exit(0);
				}
				else
				{
					strcpy(pkName, sendline);
					TcpData data;
					data.category = 0x02; //challenge request
					strcpy(data.user_name, myName);
					strcpy(data.pk_name, sendline);
					state = 4; // Waiting for PK response
					send(sockfd, (char*)&data, sizeof(TcpData), 0);
					printf("Waiting for your opponent to respond...\n");
				}

				break;
			case 5:
				if(strcmp(sendline, "y")==0 || strcmp(sendline, "n")==0)
				{
					TcpData data;
					data.category = 0x03;
					strcpy(data.user_name, pkName);
					strcpy(data.pk_name, myName);
					if(strcmp(sendline, "y")==0)
					{
						state = 6;
						data.info = 0x01;
						system("clear");
						printf("Please enter 1, 2 or 3 to PK. 1 win 2;  2 win 3;  3 win 1\n");
					}
					else
					{
						state = 3;
						data.info = 0x02;
					}
					send(sockfd, (char*)&data, sizeof(TcpData), 0);

				}
				else
					printf("%s\n", "Please enter y to accept or n to decline");
				break;
			case 6:
				if(strcmp(sendline, "1")==0 || strcmp(sendline, "2")==0 || strcmp(sendline, "3")==0)
				{
					state = 7;
					TcpData data;
					data.category = 0x04;
					strcpy(data.user_name, myName);
					strcpy(data.pk_name, pkName);
					if(strcmp(sendline, "1")==0)
						data.info = 0x01;
					else if(strcmp(sendline, "2")==0)
						data.info = 0x02;
					else
						data.info = 0x03;
					send(sockfd, (char*)&data, sizeof(TcpData), 0);
					printf("Waiting for judge...\n");

				}
				else
					printf("Please enter 1, 2 or 3 to PK. 2 win 1;  3 win 2;  1 win 3\n");
				break;

			default:
				;
		}
		/*
		TcpData data;
		data.category = 0x01;
		strcpy(data.user_name, sendline);
		send(sockfd, (char*)&data, sizeof(TcpData), 0);
		*/
		memset(sendline, 0, MAXLINE);

	}
	exit(0);
}
