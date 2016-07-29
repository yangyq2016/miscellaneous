
// 基于HTTP协议的数据库访问服务
// 数据连接池
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
	// async——true：异步二进制流，false：同步字符流
	// encrypt:是否加密
	HttpDBManager(bool async, bool encrypt, const TimeOut& to, const char* logfullpath = NULL);
	virtual ~HttpDBManager();
public:
	// 以下三个函数只能在主线程中调用
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
	bool			m_async;	// 是否采用异步的方式(二进制流)
	bool			m_encrypt;	// 是否加密（目前只是对请求加密——采用des ecb方式加密）
	Logger*			m_pLogger;
	HINTERNET		m_hSession;
	TimeOut			m_time_out;
};
