#include "DebugService.h"
#include "cocos2d.h"
#include <random>
#include "GCUnZip.h"
#include "SimpleLuaHander.h"

#ifdef _WIN32
#include "winsock2.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/tcp.h> 
#include <netdb.h>
#endif

DebugService* DebugService::pInstance = nullptr;
DebugService::DebugService()
{
	m_iListenID = -1;
	m_iClientSocket = -1;

	m_iZipFileSize = 0;
	m_iDownloadSize = 0;

	m_pClientFD = nullptr;
}
DebugService::~DebugService()
{
}
DebugService* DebugService::Instance()
{
	if (!pInstance)
		pInstance = new DebugService();
	return pInstance;
}
void DebugService::Start(int iPort)
{
	m_iListenPort = iPort;
	if (m_iListenID > -1)
	{
		cocos2d::log("DebugService::Start Error!!! m_iListenID[%d]", m_iListenID);
		return;
	}
	std::thread t(std::bind(&DebugService::StartListener, this));
	t.detach();
}
void CloseSocket(int iFD)
{
#ifdef _WIN32
	closesocket(iFD);
#else	
	close(iFD);
#endif
}

void DebugService::CloseClient()
{
	if (m_iClientSocket != -1)
	{
		CloseSocket(m_iClientSocket);
		m_iClientSocket = -1;
		if (m_pClientFD)
		{
			fclose(m_pClientFD);
			m_pClientFD = nullptr;
		}
	}
}

void DebugService::StartListener()
{
	m_strDeviceID = cocos2d::UserDefault::getInstance()->getStringForKey("debug_device_id");
	if (m_strDeviceID.empty())
	{
		std::random_device e;
		char cTemp[16] = {0};
		sprintf(cTemp,"%x%x",(unsigned int)time(0), e()%10000);
		m_strDeviceID = cTemp;
		cocos2d::UserDefault::getInstance()->setStringForKey("debug_device_id", m_strDeviceID);
		cocos2d::UserDefault::getInstance()->flush();
	}

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32) || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	struct sockaddr_in 	serv_addr;
	int 				sock;
	int 				flag;

	memset(&serv_addr, 0, sizeof(serv_addr));	
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(m_iListenPort);
	serv_addr.sin_family = AF_INET;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		cocos2d::log("DebugService::StartListener socket Error!!!");
		return;
	}

	/* Set the REUSEADDR option so that we don't fail to start if we're being restarted. */
	flag = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,(char *)&flag, sizeof flag) < 0)
	{
		cocos2d::log("DebugService::StartListener setsockopt Error!!! sock=%d", sock);
		CloseSocket(sock);
		return;
	}

	/* bind socket to specified port */
	if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		cocos2d::log("DebugService::StartListener bind Error!!! sock=%d", sock);
		CloseSocket(sock);
		return;
	}

	/* listen socket */
	if (listen(sock, SOMAXCONN) < 0)
	{
		cocos2d::log("DebugService::StartListener listen Error!!! sock=%d", sock);
		CloseSocket(sock);
		return;
	}
	m_iListenID = sock;
	cocos2d::log("DebugService::StartListener Start Listen fd=%d port=%d", m_iListenID, m_iListenPort);

	char cBuff[10240];
	int iPos = 0;
	char cTempBuff[10240];

	struct sockaddr_in clientAddr;
#if CC_TARGET_PLATFORM==CC_PLATFORM_IOS
	socklen_t len = sizeof(clientAddr);
#else
	int len = sizeof(clientAddr);
#endif
	while (true)
	{
		if (m_iClientSocket == -1)
		{
			int connSocket = accept(sock, (struct sockaddr *)&clientAddr, &len);
			if (connSocket <= 0)
			{
				cocos2d::log("DebugService::StartListener accept Error!!! sock=%d", sock);
				return;
			}
			m_iClientSocket = connSocket;
			iPos = 0;
			cocos2d::log("DebugService::StartListener connected sock=%d", m_iClientSocket);

			int iLen = m_strDeviceID.length();
			char cBuff[100] = { 0 };
			cBuff[0] = 0xD;
			cBuff[1] = 0x01;
			cBuff[2] = (unsigned char)(iLen >>8);
			cBuff[3] = (unsigned char)iLen;
			strcpy(cBuff+4, m_strDeviceID.c_str());
#ifdef _WIN32
			int len = send(connSocket, cBuff, iLen + 4, 0);
#else
			int len = write(connSocket, cBuff, iLen + 4);
#endif
			if (len < iLen + 4)
			{
				cocos2d::log("DebugService::StartListener send Error!!! sock=%d len=%d", m_iClientSocket, len);
				CloseClient();
				continue;
			}
		}
		else
		{
			int iReadLen = recv(m_iClientSocket, cBuff + iPos, sizeof(cBuff)- iPos, 0);
			if (iReadLen <= 0)
			{
				cocos2d::log("DebugService::StartListener recv Error!!! sock=%d", m_iClientSocket);
				CloseClient();
				continue;
			}
			iPos += iReadLen;
			if (iPos < 4)
			{
				continue;
			}
			if ((unsigned char)cBuff[0] != 0xD)
			{
				cocos2d::log("DebugService::StartListener recv tag Error!!! sock=%d tag=%d", m_iClientSocket, cBuff[0]);
				CloseClient();
				continue;
			}

			while (true)
			{
				int msglen = ((unsigned char)cBuff[2] << 8) | (unsigned char)cBuff[3];
				//log("iPos=%d,msglen = %d", iPos, msglen);

				if (iPos < msglen + 4)
					break;

				int msgtype = cBuff[1];
				int rt = ParseMsg(msgtype, cBuff+4, msglen);
				if (rt == -1)
				{
					CloseClient();
					break;
				}

				int leftlen = iPos - msglen - 4;
				iPos = leftlen;
				if (leftlen > 0)
				{
					memcpy(cTempBuff, cBuff + msglen + 4, leftlen);
					memcpy(cBuff, cTempBuff, leftlen);
				}
			}
		}
	}

	CloseClient();
	CloseSocket(m_iListenID);
	cocos2d::log("DebugService::StartListener Service Exit!!!");
}
int DebugService::ParseMsg(int iMstType, char *pData, int iMsgLen)
{
	auto fileutil = cocos2d::FileUtils::getInstance();
	if (iMstType == 0x03)
	{
		if (m_pClientFD == nullptr)
		{
			cocos2d::log("DebugService::StartListener file fd null Error!!! ");
			return -1;
		}
		fwrite(pData, iMsgLen, 1, m_pClientFD);
		m_iDownloadSize += iMsgLen;
		int percent = m_iDownloadSize*100 / m_iZipFileSize;
		if (percent-m_iLastDownloadPercent>10 || percent==100)
		{
			m_iLastDownloadPercent = percent;
			cocos2d::log("DebugService::StartListener Download %d:%d [%d%%]", m_iDownloadSize, m_iZipFileSize, percent);
		}
		if (m_iDownloadSize >= m_iZipFileSize)
		{
			cocos2d::log("DebugService::StartListener Download Zip Success Size=%d", m_iZipFileSize);
			fclose(m_pClientFD);
			m_pClientFD = nullptr;
			GCUnZip::Instance()->UnZip(m_strZipFilePath.c_str(), fileutil->getWritablePath().c_str());
		}
	}
	else if (iMstType == 0x02)//开始接收文件
	{
		m_iZipFileSize = *((int*)pData);
		if (m_iZipFileSize <= 0)
		{
			cocos2d::log("DebugService::StartListener file len Error!!! len=%d", m_iZipFileSize);
			return -1;
		}
		m_iLastDownloadPercent = 0;
		m_iDownloadSize = 0;
		m_strZipFilePath = fileutil->getWritablePath() + "debug_file.zip";
		if (fileutil->isFileExist(m_strZipFilePath))
			fileutil->removeFile(m_strZipFilePath);

		m_pClientFD = fopen(m_strZipFilePath.c_str(), "ab");
		cocos2d::log("DebugService::StartListener Begin Download file=%s size=%d", m_strZipFilePath.c_str(), m_iZipFileSize);
		if (m_pClientFD == nullptr)
		{
			cocos2d::log("DebugService::StartListener file fd null Error!!! ");
			return -1;
		}
	}
	else if (iMstType == 0x04)
	{
		//重启
		cocos2d::log("DebugService::StartListener Reboot Game!!!");
		Director::getInstance()->getScheduler()->performFunctionInCocosThread([] {
			SimpleLuaHander::Instance()->CallLuaHander("debug_reboot_game");
		});
		return -1;
	}
	else if (iMstType == 0x05)
	{
		struct IPPort {
			char cIP[20];
			int iPort;
		};
		IPPort* pMsg = (IPPort*)pData;
		CCUserDefault::getInstance()->setStringForKey("luaide_debug_ip",pMsg->cIP);
		CCUserDefault::getInstance()->setIntegerForKey("luaide_debug_port",pMsg->iPort);
		CCUserDefault::getInstance()->flush();
		cocos2d::log("DebugService::StartListener Set LuaIDE Debug IP[%s] Port[%d]", pMsg->cIP, pMsg->iPort);
	}
	return 0;
}
void DebugService::End()
{
	CloseClient();
	if (m_iListenID > 0)
		CloseSocket(m_iListenID);
}
