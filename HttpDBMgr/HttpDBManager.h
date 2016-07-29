
// ����HTTPЭ������ݿ���ʷ���
// �������ӳ�
// version 2.0
// by yyq

#pragma once
#include "..\R_DBManager.h"
#include "..\Singleton\Singleton.h"
#include <map>
#include <winhttp.h>

class Logger;
class HttpConnection;
class HttpDBManager : public CR_IDBManager, public Singleton<HttpDBManager>
{
	typedef std::multimap<int, HttpConnection*> ConnectionList;
public:
	struct TimeOut
	{
		int m_nConnectTimeout;
		int m_nSendTimeout;
		int m_nReceiveTimeout;
	};
	// async����true���첽����������false��ͬ���ַ���
	// encrypt:�Ƿ����
	HttpDBManager(bool async, bool encrypt, const TimeOut& to, const char* logfullpath = NULL);
	virtual ~HttpDBManager();
public:
	// ������������ֻ�������߳��е���
	bool CreateConnection(LPCSTR pStrConnection, int nID = 0, int nConnectNum = 8);
	void Release();
	void Release(int nID);

	CR_IConnection* GetSafeConnection(const char* pRequestFile = NULL, const int nRequestLine = 0, const char* pRequestFunc = NULL);
	CR_IConnection* GetSafeConnection(int nID);
	void ReleaseConnection(CR_IConnection* connection);

	CR_IRecordset* CreateRecordset();
	void ReleaseRecordset(CR_IRecordset* recordset);

	void run_pre_release();
private:
	ConnectionList	m_ConnList;
	bool			m_async;	// �Ƿ�����첽�ķ�ʽ(��������)
	bool			m_encrypt;	// �Ƿ���ܣ�Ŀǰֻ�Ƕ�������ܡ�������des ecb��ʽ���ܣ�
	Logger*			m_pLogger;
	HINTERNET		m_hSession;
	TimeOut			m_time_out;
};
