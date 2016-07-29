#include "stdafx.h"
#include "HttpDBManager.h"
#include <io.h>
#include "..\Tool\Logger.h"
#include "HttpConnection.h"
#include "HttpRecordset.h"

#pragma comment(lib, "Winhttp.lib")

// HttpDBManager�ڲ�ά�����̳߳�
// ����ȫ�ֱ����ķ�ʽֻ�����ڵ�ǰ����ģʽ
// ���HttpDBManager��Ҫ�������ʵ��
// ��ô�Ͳ�����ȫ�ֱ�����
static const DWORD	g_thread_count = 8;				// Ĭ���߳�����
HANDLE				g_worker_thread[g_thread_count];	// �߳̾������
HANDLE				g_thread_exit = 0;					// �˳��ź�
HANDLE				g_thread_semaphore = 0;			// ͬ���ź�
request_list		g_request_list;					// �����б�FIFO
__Mutex				g_list_lock;						// �б���

template<> HttpDBManager* Singleton<HttpDBManager>::ms_Singleton = 0;

HttpDBManager::HttpDBManager(bool async, bool encrypt, const TimeOut& to, const char* logfullpath /*= NULL*/ )
	: CR_IDBManager(DBM_HTTP)
	, m_async(async)
	, m_encrypt(encrypt)
	, m_pLogger(0)
	, m_hSession(0)
{
	memcpy(&m_time_out, &to, sizeof(TimeOut));
	if(logfullpath && _access(logfullpath, 0) != -1)
	{
		m_pLogger = new Logger;
		m_pLogger->setLogFilename(logfullpath);
	}
	if(m_async)
	{
		g_thread_exit = CreateEvent(0, TRUE, FALSE, 0);
		g_thread_semaphore = CreateSemaphore(0, 0, LONG_MAX, 0);
		for (DWORD i = 0; i < g_thread_count; ++i)
			g_worker_thread[i] = (HANDLE)_beginthreadex(0, 0, &HttpConnection::thread_proc, 0, 0, 0);
	}
}

HttpDBManager::~HttpDBManager()
{
	if(m_hSession)
	{
		WinHttpCloseHandle(m_hSession);
		m_hSession = 0;
	}
	if(m_pLogger)
	{
		delete m_pLogger;
		m_pLogger = 0;
	}
	Release();
}

bool HttpDBManager::CreateConnection( LPCSTR pStrConnection, int nID /*= 0*/, int nConnectNum /*= 8*/ )
{
	if( nConnectNum <= 0 || NULL == pStrConnection || strlen(pStrConnection) <= 0 )
		return false;
	if(!m_hSession)
	{
		if(!WinHttpCheckPlatform())
		{
			if(m_pLogger)
				m_pLogger->logEvent("This platform is NOT supported by WinHTTP.\n", Errors);
			return false;
		}
		m_hSession = WinHttpOpen( L"HttpDBManager/2.0"
			, WINHTTP_ACCESS_TYPE_NO_PROXY
			, WINHTTP_NO_PROXY_NAME
			, WINHTTP_NO_PROXY_BYPASS, 0);
		if(!m_hSession)
		{
			m_pLogger->stream(Errors) << "Error " 
				<< HttpConnection::get_error_msg(GetLastError())
				<< " in HttpDBManager::CreateConnection::WinHttpOpen.";
			return false;
		}
		if(!WinHttpSetTimeouts(m_hSession, 0
			, m_time_out.m_nConnectTimeout
			, m_time_out.m_nSendTimeout
			, m_time_out.m_nReceiveTimeout))
		{
			WinHttpCloseHandle(m_hSession);
			m_hSession = 0;
			m_pLogger->stream(Errors) << "Error "
				<< HttpConnection::get_error_msg(GetLastError())
				<< " in HttpDBManager::CreateConnection::WinHttpSetTimeouts.";
			return false;
		}
	}
	ConnectionList::iterator find = m_ConnList.find(nID);
	if(find != m_ConnList.end())
		return true;
	for (int i = 0; i < nConnectNum; ++i)
	{
		HttpConnection* conn = new HttpConnection(m_encrypt);
		try
		{
			if(!conn->Init(pStrConnection, m_hSession))
			{
				delete conn;
				conn = 0;
				continue;
			}
		}
		catch(...)
		{
			delete conn;
			conn = 0;
			continue;
		}
		m_ConnList.insert(std::make_pair(nID, conn));
	}
	return !m_ConnList.empty();
}

void HttpDBManager::Release()
{
	for(ConnectionList::iterator pos = m_ConnList.begin(); pos != m_ConnList.end(); ++pos)
	{
		delete pos->second;
		pos->second = 0;
	}
	m_ConnList.clear();
}

void HttpDBManager::Release( int nID )
{
	std::pair<ConnectionList::iterator, ConnectionList::iterator> _pairii = m_ConnList.equal_range(nID);
	for (ConnectionList::iterator pos = _pairii.first; pos != _pairii.second; ++pos)
	{
		delete pos->second;
		pos->second = 0;
	}
	m_ConnList.erase(nID);
}

CR_IConnection* HttpDBManager::GetSafeConnection( const char* pRequestFile /*= NULL*/, const int nRequestLine /*= 0*/, const char* pRequestFunc /*= NULL*/ )
{
	CR_IConnection* conn = GetSafeConnection(0);
	if(  pRequestFile && strlen(pRequestFile) > 0 
		&& pRequestFunc && strlen(pRequestFunc) > 0 
		&& m_pLogger)
	{
		//��¼��־
		m_pLogger->stream() 
			<< (conn ? "��ȡ���ݿ�����(�ɹ�)" : "��ȡ���ݿ�����(ʧ��)")
			<< " �ļ�:" << pRequestFile 
			<< " �к�:" << nRequestLine 
			<< " ����:" << pRequestFunc;
	}
	return conn;
}

CR_IConnection* HttpDBManager::GetSafeConnection( int nID )
{
	std::pair<ConnectionList::iterator, ConnectionList::iterator> _pairii = m_ConnList.equal_range(nID);
	for (ConnectionList::iterator pos = _pairii.first; pos != _pairii.second; ++pos)
	{
		if(pos->second && pos->second->TryLock())	// HttpConnectionWrapper����ʱUnlock
		{
			return new HttpConnectionWrapper(pos->second);	//����ȷ��New�ɹ�
		}
	}
	return 0;
}

void HttpDBManager::ReleaseConnection( CR_IConnection* connection)
{
	if(connection)
	{
		delete connection;
		connection = 0;
	}
}

CR_IRecordset* HttpDBManager::CreateRecordset()
{
	if(m_async)
		return new HttpRecordset_Async;
	else
		return new HttpRecordset_Sync;
}

void HttpDBManager::ReleaseRecordset( CR_IRecordset* recordset)
{
	if(recordset)
	{
		delete recordset;
		recordset = 0;
	}
}

void HttpDBManager::run_pre_release()
{
	if(m_async)
	{
		g_list_lock.Lock();
		for (auto it = g_request_list.begin(); it != g_request_list.end(); ++it)
			SetEvent((*it).m_event);
		g_request_list.clear();
		g_list_lock.Unlock();
		SetEvent(g_thread_exit);
		WaitForMultipleObjects(g_thread_count, g_worker_thread, TRUE, INFINITE);
		for (DWORD i = 0; i < g_thread_count; ++i)
		{
			CloseHandle(g_worker_thread[i]);
			g_worker_thread[i] = 0;
		}
		CloseHandle(g_thread_exit);
		g_thread_exit = 0;
		CloseHandle(g_thread_semaphore);
		g_thread_semaphore = 0;
	}
}
