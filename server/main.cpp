#pragma comment(lib, "Ws2_32.lib")
#include<iostream>
#include<WinSock2.h>
#include<string>
#include<string.h>
#include<fstream>
#include<time.h>
using namespace std;
SOCKET server, server_2;
SOCKADDR_IN serverAddr, clientAddr;
#define TIMEOUT 500
#define SENDSUCCESS true
#define SENDFAIL false
#define CONNECTSUCCESS true
#define CONNECTFAIL false
#define SEQ1 '1'
#define ACK1 '#'
#define SEQ2 '3'
#define ACK2 SEQ1 + 1
#define SEQ3 '5'
#define ACK3 SEQ2 + 1
#define WAVE1 '7'
#define ACKW1 '#'
#define WAVE2 '9'
#define ACKW2 WAVE1 + 1
#define LENGTH 16377				// [1,16378]任意值
#define CheckSum 7
#define LAST '$'
#define NOTLAST '@'
#define ACKMsg '%'
#define NAK '^'
#define Scroll 16					// 滑动窗口大小
char message[200000000];
static char pindex[2];		// 记录滑动窗的最开始的位置
int pcknum = 0;
string filename;
int pkgsure = 0;
// 等待连接
void WaitConnection()
{
	while (1)
	{
		char recv[2];
		int clientlen = sizeof(clientAddr);
		while (recvfrom(server, recv, 2, 0, (sockaddr *)&clientAddr, &clientlen) == SOCKET_ERROR);
		if (recv[0] != SEQ1)
			continue;
		cout << "Server received first handshake." << endl;
		bool flag = CONNECTSUCCESS;
		while (1)
		{
			memset(recv, 0, 2);
			char send[2];
			send[0] = SEQ2;
			send[1] = ACK2;
			sendto(server, send, 2, 0, (sockaddr*)&clientAddr, sizeof(clientAddr));
			cout << "Server sent second handshake." << endl;
			while (recvfrom(server, recv, 2, 0, (sockaddr *)&clientAddr, &clientlen) == SOCKET_ERROR);
			if (recv[0] == SEQ1)
				continue;
			if (recv[0] != SEQ3 || recv[1] != ACK3)
			{
				cout << "Connection failed.\nPlease restart your client." << endl;
				flag = CONNECTFAIL;
			}
			break;
		}
		if (!flag)
			continue;
		break;
	}
}
// 开启服务器
int StartServer()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		cout << "WSAStartup Error:" << WSAGetLastError() << endl;
		return -1;
	}
	server = socket(AF_INET, SOCK_DGRAM, 0);

	if (server == SOCKET_ERROR)
	{
		cout << "Socket Error:" << WSAGetLastError() << endl;
		return -1;
	}
	int Port = 1439;
	serverAddr.sin_addr.s_addr = htons(INADDR_ANY);
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(Port);

	if (bind(server, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		cout << "Bind Error:" << WSAGetLastError() << endl;
		return -1;
	}
	cout << "Start Server Success!" << endl;
	return 1;
}
// 等待挥手断开
void WaitDisconnection(SOCKET server)
{
	while (1)
	{
		char recv[2];
		int clientlen = sizeof(clientAddr);
		while (recvfrom(server, recv, 2, 0, (sockaddr *)&clientAddr, &clientlen) == SOCKET_ERROR);
		if (recv[0] != WAVE1)
			continue;
		cout << "Server received first hand." << endl;
		char send[2];
		send[0] = WAVE2;
		send[1] = ACKW2;
		sendto(server, send, 2, 0, (sockaddr *)&clientAddr, sizeof(clientAddr));
		break;
	}
	cout << "Client Disconnected." << endl;
}
// checksum算法 进行包的检测
unsigned char PkgCheck(char *arr, int len)
{
	if (len == 0)
		return ~(0);
	unsigned char ret = arr[0];
	for (int i = 1; i < len; i++)
	{
		unsigned int tmp = ret + (unsigned char)arr[i];
		tmp = tmp / (1 << 8) + tmp % (1 << 8);
		tmp = tmp / (1 << 8) + tmp % (1 << 8);
		ret = tmp;
	}
	return ~ret;
}
// 接收消息
void Recvmessage(SOCKET server)
{
	int clientlen = sizeof(clientAddr);
	char length[2];
	while (recvfrom(server, length, 2, 0, (sockaddr*)&clientAddr, &clientlen) == SOCKET_ERROR);
	int pcklen = length[0] * 128 + length[1];
	pindex[0] = 0;
	pindex[1] = -1;
	int len = 0;
	int lent;
	while (1)
	{
		char recv[LENGTH + CheckSum];
		memset(recv, '\0', LENGTH + CheckSum);
		while (1)
		{
			int clientlen = sizeof(clientAddr);
			int begin_time = clock();
			bool flag = SENDSUCCESS;
			while (recvfrom(server, recv, LENGTH + CheckSum, 0, (sockaddr*)&clientAddr, &clientlen) == SOCKET_ERROR)
			{
				int over_time = clock();
				if (over_time - begin_time > TIMEOUT)
				{
					flag = SENDFAIL;
					break;
				}
				cout << WSAGetLastError() << endl;
			}
			lent = recv[4] * 128 + recv[5];
			char send[3];
			memset(send, '\0', 3);
			// 没有超时，ACK码正确，差错检查正确
			if (flag && recv[6] == ACKMsg && PkgCheck(recv, lent + CheckSum) == 0)
			{
				// 在滑动窗范围里
				if (recv[2] * 128 + recv[3] <= pindex[0] * 128 + pindex[1] <= recv[2] * 128 + recv[3] + Scroll)
				{
					// 累计确认
					pkgsure++;
					if (pkgsure == 5)
					{
						cout << "Sure." << recv[2] * 128 + recv[3] << endl;
						pkgsure = 0;
						send[0] = ACKMsg;
						pindex[0] = recv[2];
						pindex[1] = recv[3];
						send[1] = recv[2];
						send[2] = recv[3];
						sendto(server, send, 3, 0, (sockaddr*)&clientAddr, sizeof(clientAddr));
					}
					// cout << recv[2] * 128 + recv[3] << endl;
					pcknum++;
					break;
				}
				else
				{
					cout << "Package out of Scroll." << endl;
				}
			}
			// 什么东西错了。。发送NAK，重发
			else
			{
				cout << "Something Wrong." << endl;
				send[0] = NAK;
				send[1] = recv[2];
				send[2] = recv[3];
				sendto(server, send, 3, 0, (sockaddr*)&clientAddr, sizeof(clientAddr));
				continue;
			}
		}
		// 收到的包可能顺序不对（因为存在差错检测之后的重传）
		// 根据它的index码确定它的位置
		int loc = LENGTH * (recv[2] * 128 + recv[3]);
		for (int i = CheckSum; i < lent + CheckSum; i++)
		{
			message[loc + i - CheckSum] = recv[i];
			len++;
		}
		// 是不是最后一个，是就结束了
		if (recv[1] == LAST)
			break;
	}
	// 累计确认总的包数
	if (pcklen == pcknum)
	{
		cout << "No packages disappeared!" << endl;
	}
	ofstream fout(filename.c_str(), ofstream::binary);
	for (int i = 0; i < len; i++)
		fout << message[i];
	fout.close();
}
// 接收文件名
void RecvName()
{
	char name[100];
	int clientlen = sizeof(clientAddr);
	while (recvfrom(server, name, 100, 0, (sockaddr*)&clientAddr, &clientlen) == SOCKET_ERROR);
	for (int i = 0; name[i] != '$'; i++)
	{
		filename += name[i];
	}
}
int main()
{
	StartServer();
	cout << "Waiting..." << endl;
	WaitConnection();
	cout << "Connect to Client!" << endl;
	RecvName();
	Recvmessage(server);
	cout << "ReceiveMessage Over!" << endl;
	WaitDisconnection(server);
	closesocket(server);
	WSACleanup();
	return 0;
}