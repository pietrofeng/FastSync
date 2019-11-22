#ifndef __DEBUG_SERVICE_H__
#define __DEBUG_SERVICE_H__

#include <string>

class DebugService
{
public:
	virtual ~DebugService();
	static DebugService* Instance();
	void Start(int iPort);
	bool IfStart() { return m_iListenID > 0; }
	void End();

private:
	DebugService();
	void StartListener();
	void CloseClient();

	static DebugService* pInstance;
	std::string m_strDeviceID;

	int m_iListenPort;
	int m_iListenID;
	int m_iClientSocket;
	int m_iZipFileSize;
	int m_iDownloadSize;
	int m_iLastDownloadPercent;
	FILE *m_pClientFD;
	std::string m_strZipFilePath;

	int ParseMsg(int iMstType,char *pData,int iMsgLen);
};

#endif
