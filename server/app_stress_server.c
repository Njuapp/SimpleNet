//�ļ���: server/app_stress_server.c

//����: ����ѹ�����԰汾�ķ������������. �������������ӵ�����SIP����. Ȼ��������stcp_server_init()��ʼ��STCP������.
//��ͨ������stcp_server_sock()��stcp_server_accept()�����׽��ֲ��ȴ����Կͻ��˵�����. ��Ȼ������ļ�����. 
//����֮��, ������һ��������, �����ļ����ݲ��������浽receivedtext.txt�ļ���.
//���, ������ͨ������stcp_server_close()�ر��׽���, ���Ͽ��뱾��SIP���̵�����.

//��������: 2018��

//����: ��

//���: STCP������״̬

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "../common/constants.h"
#include "stcp_server.h"

//����һ������, ʹ�ÿͻ��˶˿ں�87�ͷ������˿ں�88. 
#define CLIENTPORT1 87
#define SERVERPORT1 88
//�ڽ��յ��ļ����ݱ������, �������ȴ�15��, Ȼ��ر�����.
#define WAITTIME 15

//����������ӵ�����SIP���̵Ķ˿�SIP_PORT. ���TCP����ʧ��, ����-1. ���ӳɹ�, ����TCP�׽���������, STCP��ʹ�ø����������Ͷ�.
int connectToSIP() {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(SIP_PORT);
	if(!connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)))
		return sockfd;
	else
		return -1;
	
}

//��������Ͽ�������SIP���̵�TCP����. 
void disconnectToSIP(int sip_conn) {

	while(close(sip_conn)!=0){}
	
}

int main() {
	//���ڶ����ʵ����������
	srand(time(NULL));

	//���ӵ�SIP���̲����TCP�׽���������
	gsip_conn = connectToSIP();
	if(gsip_conn<0) {
		printf("can not connect to the local SIP process\n");
	}

	//��ʼ��STCP������
	stcp_server_init(gsip_conn);

	//�ڶ˿�SERVERPORT1�ϴ���STCP�������׽��� 
	int sockfd= stcp_server_sock(SERVERPORT1);
	if(sockfd<0) {
		printf("can't create stcp server\n");
		exit(1);
	}
	//��������������STCP�ͻ��˵����� 
	stcp_server_accept(sockfd);

	//���Ƚ����ļ�����, Ȼ������ļ�����
	int fileLen;
	stcp_server_recv(sockfd,&fileLen,sizeof(int));
	char* buf = (char*) malloc(fileLen);
	stcp_server_recv(sockfd,buf,fileLen);

	//�����յ����ļ����ݱ��浽�ļ�receivedtext.txt��
	FILE* f;
	f = fopen("receivedtext.txt","a");
	fwrite(buf,fileLen,1,f);
	fclose(f);
	free(buf);

	//�ȴ�һ���
	sleep(WAITTIME);

	//�ر�STCP������ 
	if(stcp_server_close(sockfd)<0) {
		printf("can't destroy stcp server\n");
		exit(1);
	}				

	//�Ͽ���SIP����֮�������
	disconnectToSIP(gsip_conn);
}
