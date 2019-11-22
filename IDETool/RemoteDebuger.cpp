// RemoteDebuger.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <iostream>
#include <io.h>
#include <WS2tcpip.h>
#include <Winsock2.h>
#include <fstream>
#include "zip.h"
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GCFileTools.h"
#include "GCConfBase.h"
#include <vector>
#include <map>

#pragma comment(lib,"ws2_32.lib")
using namespace std;

char g_remote_ip[32];
int g_remote_port;
string strDeviceID;
string strMainDir;
vector<string> vecSubDir;
vector<string> vecAllFiles;
vector<string> vecAllFilesName;
int g_socket_id;

char g_local_ip[32] = {0};
int g_local_port = 0;


void set_console_pos(int x, int y)
{
	COORD coord;
	coord.X = x;
	coord.Y = y;
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}
void get_console_pos(int* x, int* y)
{
	CONSOLE_SCREEN_BUFFER_INFO b;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &b);
	*x = b.dwCursorPosition.X;
	*y = b.dwCursorPosition.Y;
}
void set_console_color(unsigned short color)
{
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}

char cSendData[10240];
char cTemp[10240];

void HandleReceiveMsg(int iMsgID,char* pMsg,int iLen);
void SendData(unsigned char cMsgID, char* pMsg, int iLen);

static void ErrorExit(const char* str=nullptr)
{
	if (str)
	{
		set_console_color(FOREGROUND_RED);
		cout << str << endl;
	}
	set_console_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

#if _DEBUG
	char ch = getchar();
#else
	Sleep(100);
#endif
	exit(0);
}

char cLog[256] = { 0 };
static void WhiteLog(const char* str) 
{
	set_console_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
	cout << str << endl;
}
static void GreenLog(const char* str)
{
	set_console_color(FOREGROUND_GREEN);
	cout << str << endl;
}
static void RedLog(const char* str)
{
	set_console_color(FOREGROUND_RED);
	cout << str << endl;
}
static void YelowLog(const char* str)
{
	set_console_color(FOREGROUND_RED| FOREGROUND_GREEN);
	cout << str << endl;
}

int main()
{
	WSAData lpWSAData;
	WSAStartup(0x0202, &lpWSAData);
	WhiteLog("远程资源同步Begin...");

	GetConfValue(g_remote_ip, "remote_ip", "run.conf", "MAIN");
	GetConfValue(&g_remote_port, "remote_port", "run.conf", "MAIN");

	GetConfValue(g_local_ip, "local_ip", "run.conf", "MAIN");
	GetConfValue(&g_local_port, "local_port", "run.conf", "MAIN");

	char cc[256];
	GetConfValue(cc, "sync_main_dir", "run.conf", "MAIN");
	strMainDir = UrlProcess(cc);
	if (strMainDir.empty())
	{		
		ErrorExit("配置 main_dir 不能空!");
	}
	if (strMainDir.back() != '/')
		strMainDir.push_back('/');

	for (int i = 1; i < 50; ++i)
	{
		char cSection[32];
		sprintf(cSection,"sub_dir_%d",i);
		GetConfValue(cc, cSection, "run.conf", "MAIN");
		string str = UrlProcess(cc);
		if (!str.empty())
		{
			vecSubDir.push_back(str);
			GetDirAllFiles(strMainDir + str, vecAllFiles, vecAllFilesName);
		}
	}
	if (vecSubDir.empty())
	{
		ErrorExit("配置 sub_dir 不能空!");
	}

	//初始化服务器端地址族变量
	SOCKADDR_IN srvAddr;
	srvAddr.sin_addr.S_un.S_addr = inet_addr(g_remote_ip);
	srvAddr.sin_family = AF_INET;
	srvAddr.sin_port = htons(g_remote_port);
	g_socket_id = socket(AF_INET, SOCK_STREAM, 0);
	if (g_socket_id <= 0)
	{
		sprintf(cLog,"连接远程设备 %s:%d socket 失败!", g_remote_ip, g_remote_port);
		ErrorExit(cLog);
	}
	if (connect(g_socket_id, (SOCKADDR*)&srvAddr, sizeof(SOCKADDR)) == -1)
	{
		sprintf(cLog, "连接远程设备 %s:%d connect 失败!", g_remote_ip, g_remote_port);
		ErrorExit(cLog);
	}

	sprintf(cLog, "连接远程设备 %s:%d connect 成功", g_remote_ip, g_remote_port);
	YelowLog(cLog);
	
	char cReceiveBuff[10240];
	int iReceiveLen = 0;

	while (true)
	{
		int rt = recv(g_socket_id, cReceiveBuff+iReceiveLen, sizeof(cReceiveBuff), 0);
		if (rt <= 0)
		{
			if (rt == 0)
			{
				RedLog("连接中断,程序退出");
				YelowLog("~~~good luck~~~\n");
				ErrorExit();
			}
			else
			{
				sprintf(cLog,"连接 recv 错误! %d 程序退出", rt);
				ErrorExit(cLog);
			}
		}
		iReceiveLen += rt;

		while (iReceiveLen >= 4)
		{
			unsigned char tag = cReceiveBuff[0];
			unsigned char msgtype = cReceiveBuff[1];
			int msglen = ((unsigned char)cReceiveBuff[2] << 8) | (unsigned char)cReceiveBuff[3];
			if (iReceiveLen < msglen + 4)
				break;

			HandleReceiveMsg(msgtype,cReceiveBuff+4, msglen);

			iReceiveLen -= (msglen + 4);
			if (iReceiveLen > 0)
			{
				memcpy(cTemp, cReceiveBuff + 4+ msglen, iReceiveLen);
				memcpy(cReceiveBuff, cTemp, iReceiveLen);
			}
		}
	}
	
	YelowLog("远程资源同步结束...\n ~~~good luck~~~");
    return 0;
}

void SendData(unsigned char cMsgID, char* pMsg, int iLen)
{
	cSendData[0] = 0xD;
	cSendData[1] = cMsgID;
	cSendData[2] = iLen >> 8;
	cSendData[3] = iLen;
	if(iLen>0)
		memcpy(cSendData + 4, pMsg, iLen);

	int iSendSize = 0;
	while (iSendSize < iLen + 4)
	{
		int ll = send(g_socket_id, cSendData + iSendSize, iLen + 4 - iSendSize, 0);
		if (ll <= 0)
		{
			sprintf(cLog,"发送数据出现错误 err=%d MsgID=%d Len=%d",ll,cMsgID,iLen);
			ErrorExit(cLog);
		}
		iSendSize += ll;
	}
}

void HandleReceiveMsg(int iMsgID, char* pMsg, int iLen)
{
	if (iMsgID == 0x01)
	{
		strDeviceID = string(pMsg,pMsg+iLen);
		
		map<string, long long> mapFiles;
		string datfile = strDeviceID+".dat";
		if (_access(datfile.c_str(), 0) == -1)
		{
			sprintf(cLog,"收到新设备token %s", strDeviceID.c_str());
			GreenLog(cLog);
		}
		else
		{
			sprintf(cLog, "收到已知设备token %s", strDeviceID.c_str());
			GreenLog(cLog);
			
			GCFileTools ft(datfile.c_str());
			if (!ft.IsFileOpen())
			{
				sprintf(cLog, "打开 %s 错误", datfile);
				ErrorExit(cLog);
			}
			
			string strLine;
			int iLineNum = 0;
			while (ft.GetLine(strLine))
			{
				int iSpacePos = strLine.find(" ");
				if (iSpacePos == -1)
				{
					sprintf(cLog, "解析 %s 错误1 line=%d", datfile, iLineNum);
					ErrorExit(cLog);
				}
				string strFilePath = strLine.substr(0, iSpacePos);
				string strMD5 = strLine.substr(iSpacePos + 1);
				if (strFilePath.empty() || strMD5.empty())
				{
					sprintf(cLog, "解析 %s 错误2 line=%d", datfile, iLineNum);
					ErrorExit(cLog);
				}
				mapFiles[strFilePath] = stoll(strMD5.c_str());
				iLineNum++;
			}
		}
		vector<string> vecSendFile;
		for (int i = 0; i < vecAllFiles.size(); ++i)
		{
			long long id = GetFileUniqueID(vecAllFiles[i].c_str());
			auto it = mapFiles.find(vecAllFiles[i]);
			if (it != mapFiles.end())
			{
				if (id != it->second)
				{
					vecSendFile.push_back(vecAllFiles[i]);
					it->second = id;
				}
			}
			else
			{
				vecSendFile.push_back(vecAllFiles[i]);
				mapFiles[vecAllFiles[i]] = id;
			}
		}
		
		if (!vecSendFile.empty())
		{
			sprintf(cLog, "需要同步文件数量 %d 压缩包[all_data.zip]", vecSendFile.size());
			WhiteLog(cLog);
			
			HZIP hz = CreateZip("all_data.zip", 0);
			for (int i = 0; i < vecSendFile.size();++i)
			{
				string pathname = vecSendFile[i].substr(strMainDir.length(), vecSendFile[i].length()-1);
				ZipAdd(hz, pathname.c_str(), vecSendFile[i].c_str());
			}
			CloseZip(hz);

			size_t iFileSize = 0;
			auto pData = GetFileData("all_data.zip", iFileSize);
			if (!pData)
			{
				ErrorExit("打开压缩包文件异常 [all_data.zip]");
			}

			char cFileSize[16] = { 0 };
			*((int*)cFileSize) = iFileSize;
			SendData(0x02, cFileSize, 4);

			int x, y;
			float fFileSize = iFileSize / 1024.0f;
			get_console_pos(&x, &y);
			WhiteLog("开始同步文件");
			int iReadSize = 0;
			long long lastProgress = 0;
			while (iReadSize < iFileSize)
			{
				int size = 4096;
				if (iReadSize + size > iFileSize)
					size = iFileSize - iReadSize;

				SendData(0x03, (char*)pData+ iReadSize, size);
				iReadSize += size;

				int progress = (long long)iReadSize * 100 / (long long)iFileSize;
				if (progress > lastProgress)
				{
					set_console_pos(x, y);
					sprintf(cLog,"正在同步文件   %fk/%fk   [%d%%]", iReadSize / 1024.0f, fFileSize, progress);
					GreenLog(cLog);
					lastProgress = progress;
				}
			}

			fstream f(datfile.c_str(), ios::out);
			for (auto it = mapFiles.begin(); it != mapFiles.end(); ++it)
			{
				f << it->first << " " << it->second << endl;
			}
			f.close();
		}
		else
		{
			WhiteLog("没有文件需要同步");
		}

		if (strlen(g_local_ip) > 0 && g_local_port > 0)
		{
			struct  IPAndPort
			{
				char szIP[20];
				int iPort;
			};
			IPAndPort iap;
			memset(&iap,0,sizeof(iap));
			strcpy(iap.szIP,g_local_ip);
			iap.iPort = g_local_port;
			GreenLog("发送本地Ip和端口(luaide远程调试使用)");
			SendData(0x05, (char*)&iap, sizeof(iap));
		}
		
		GreenLog("发送重启指令");
		SendData(0x04,NULL,0);
	}
}



