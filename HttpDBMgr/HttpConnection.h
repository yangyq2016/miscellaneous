// 基于HTTP协议的数据库访问服务
// 数据连接
// version 2.0
// by yyq

#pragma once
#include "..\Tool\Utility.h"
#include "..\json\json.h"
#include "..\R_DBManager.h"
#include <string>
#include <winhttp.h>

class receiver
{
public:
	virtual ~receiver(){}
	virtual bool is_abort(void) const = 0;
	virtual void push_data(char* data, size_t size) = 0;
	virtual	size_t get_recv_size(void) const = 0;
};

class HttpConnection;
struct request_context
{
	HANDLE			m_event;	// 由 requestset 释放
	HttpConnection*	m_conn;
	receiver*		m_recv;
	std::string		m_requst;
};

typedef std::list<request_context>	request_list;

class HttpConnection : public __Mutex
{
	//struct inner_package
	//{
	//	HttpConnection*	m_conn;
	//	receiver*		m_recv;
	//	std::string		m_requst;
	//};
	class string_receiver : public receiver
	{
	public:
		bool is_abort(void) const {return false;}
		void push_data(char* data, size_t size)
		{
			if(data && size) m_data.append(data, size);
		}
		size_t get_recv_size(void) const {return m_data.size();}
		std::string	m_data;
	};
	friend class HttpDBManager;
private:
	HttpConnection(bool encrypt);
public:
	virtual ~HttpConnection();
public:
	bool	Init(const char* connstr, HINTERNET hSession);
	int		Execute(LPCSTR szSQL,bool bSaveLog = true);
	DWORD	CreateINTID(LPCSTR strTableName);
	CR_IConnection::PLANT_DBTYPE GetDBType();
public:
	static bool crack_url(const std::wstring& url, std::wstring& hostName, int& nPort, std::wstring& urlPath);
	void	request_response_sync(Json::Value& value, const std::string& request);
	HANDLE	request_response_async(receiver *recv, const std::string& request);
private:
	static unsigned __stdcall thread_proc(void* lpvoid);
	void	request_response_imp(receiver *recv, const std::string& request);
	HINTERNET						m_hConnect;
	std::wstring					m_hostName;
	std::wstring					m_urlPath;
	int								m_nPort;
	CR_IConnection::PLANT_DBTYPE	m_dbType;
	bool							m_encrypt;
public:
	static const std::string encryptkey;
	bool	is_encrypt(void) const { return m_encrypt; }
	static const char* get_error_msg(DWORD dwErrorCode);
};

//只能在HttpDBManager内部使用
class HttpConnectionWrapper : public CR_IConnection
{
	friend class HttpDBManager;
	friend class HttpRecordset_Async;
	friend class HttpRecordset_Sync;
	HttpConnectionWrapper(HttpConnection* conn);
public:
	virtual ~HttpConnectionWrapper();

public:
	int Execute(LPCSTR szSQL,bool bSaveLog = true);
	DWORD   CreateINTID(LPCSTR strTableName);
	CR_IConnection::PLANT_DBTYPE GetDBType();
private:
	HttpConnection*		m_connection;
};

