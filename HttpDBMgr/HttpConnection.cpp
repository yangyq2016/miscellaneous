#include "stdafx.h"
#include "HttpConnection.h"
#include "..\Tool\Exceptions.h"
#include "..\DefaultLogger.h"
#include "..\Tool\StringTool.h"
#include "..\Tool\Encryptor.h"
#include "..\Tool\base64.h"

using namespace std;

extern HANDLE			g_thread_exit;
extern HANDLE			g_thread_semaphore;
extern request_list		g_request_list;
extern __Mutex			g_list_lock;

const std::string HttpConnection::encryptkey = "9D3e5E1p";

HttpConnection::HttpConnection(bool encrypt)
	: m_hConnect(0)
	, m_encrypt(encrypt)
	, m_nPort(80){}

HttpConnection::~HttpConnection()
{
	if(m_hConnect)
	{
		WinHttpCloseHandle(m_hConnect);
		m_hConnect = 0;
	}
}

bool HttpConnection::Init( const char* connstr, HINTERNET hSession)
{
	if(!connstr || strlen(connstr) == 0 || !hSession)
		return false;
	wstring url = StringTool::mb_to_wc(connstr);
	if(!crack_url(url, m_hostName, m_nPort, m_urlPath))
		return false;
	m_hConnect = WinHttpConnect(hSession, m_hostName.c_str(), m_nPort, 0);
	if(!m_hConnect)
		return false;
	return true;
}

int HttpConnection::Execute( LPCSTR szSQL,bool bSaveLog /*= true*/ )
{
	if(!szSQL || strlen(szSQL) == 0)
	{
		throw ComException("SQL语句不能为空！");
	}
	if(!m_hConnect)
	{
		throw ComException("连接句柄不能为空！");
	}
	Json::Value value;
	Json::FastWriter fw;
	value["mode"] = "DML";
	value["sql"] = szSQL;
	std::string post;
	post = fw.write(value);
	post = StringTool::gbk_to_utf8(post.c_str());

	// try des ecb encrypt
	std::string prefix("json=");
	if (m_encrypt)
	{
		std::string post_plain = post;
		std::vector<char> outbuf, inbuf(post.begin(), post.end());
		unsigned char key[8];
		memcpy(key, encryptkey.c_str(), 8);
		if (Encryptor::Des_Ecb_Encrypt(outbuf, inbuf, key, Encryptor::deo_encrypt))
		{
			int outlen = Base64::EncodeLen(outbuf.size());
			post.resize(outlen);
			Base64::Encode(&post[0], (unsigned char*)&outbuf[0], outbuf.size());
			prefix = "encrypt=1&" + prefix;
		}
		else
		{
			post = post_plain;
		}
	}
	post = StringTool::url_encode(post);
	post = prefix + post;

	request_response_sync(value, post);
	return 1;
}

DWORD HttpConnection::CreateINTID( LPCSTR strTableName )
{
	if(!m_hConnect)
	{
		throw ComException("连接句柄不能为空！");
	}
	if(!strTableName || strlen(strTableName) == 0)
	{
		throw ComException("输入参数不能为空！");
	}
	// type目前仅支持int、float、varchar三种
	std::string post = "{\"mode\":\"procedure\",\
					   \"name\":\"SP_NEXT_KEY_PARAM1\",\
					   \"param\":[{\"in_out\":\"out\",\"type\":\"int\"},\
					   {\"in_out\":\"in\",\"type\":\"varchar\",\"val\":\"" + std::string(strTableName) + "\"}]}";
	// try des ecb encrypt
	std::string prefix("json=");
	if (m_encrypt)
	{
		std::string post_old = post;
		std::vector<char> outbuf, inbuf(post.begin(), post.end());
		unsigned char key[8];
		memcpy(key, encryptkey.c_str(), 8);
		if (Encryptor::Des_Ecb_Encrypt(outbuf, inbuf, key, Encryptor::deo_encrypt))
		{
			int outlen = Base64::EncodeLen(outbuf.size());
			post.resize(outlen);
			Base64::Encode(&post[0], (unsigned char*)&outbuf[0], outbuf.size());
			prefix = "encrypt=1&" + prefix;
		}
		else
		{
			post = post_old;
		}
	}
	post = StringTool::url_encode(post);
	post = prefix + post;

	Json::Value value;
	request_response_sync(value, post);
	try
	{
		if(value.isMember("out"))
			return value["out"][0].asUInt();
		else
			return 0;
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
}

CR_IConnection::PLANT_DBTYPE HttpConnection::GetDBType()
{
	return CR_IConnection::DBT_ORACLE;
}

struct AutoCloseHandle
{
	AutoCloseHandle(HINTERNET& handle) 
		: m_handle(handle){}
	~AutoCloseHandle()
	{
		if(!m_handle)
			return;
		WinHttpCloseHandle(m_handle);
		m_handle = 0;
	}
	HINTERNET& m_handle;
};

bool HttpConnection::crack_url( const std::wstring& url, std::wstring& hostName, int& nPort, std::wstring& urlPath )
{
	static URL_COMPONENTS uc;
	ZeroMemory(&uc, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	uc.dwSchemeLength    = -1;
	uc.dwHostNameLength  = -1;
	uc.dwUrlPathLength   = -1;
	uc.dwExtraInfoLength = -1;
	if (!WinHttpCrackUrl(url.c_str(), url.length(), 0, &uc))
	{
		CString err_msg;
		err_msg.Format("Error %s in WinHttpOpenRequest.\n", get_error_msg(GetLastError()));
		DefaultLogger::getSingleton().logEvent(err_msg.GetBuffer());
		return false;
	}
	hostName.assign(uc.lpszHostName, uc.dwHostNameLength);
	nPort = uc.nPort;
	urlPath.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
	return true;
}

//#define LOG_REQUEST_COST
void HttpConnection::request_response_sync( Json::Value& value, const std::string& request )
{
#ifdef LOG_REQUEST_COST
	LARGE_INTEGER t1, t2, tc;
	QueryPerformanceFrequency(&tc); 
	QueryPerformanceCounter(&t1); 
#endif
	string_receiver sr;
	request_response_imp(&sr, request);
#ifdef LOG_REQUEST_COST
	QueryPerformanceCounter(&t2); 
	double interval = (double)(t2.QuadPart- t1.QuadPart) / tc.QuadPart;
	DefaultLogger::getSingleton().stream(Informative) << t1.QuadPart 
		<< "\t" << interval 
		<< "\t" << sr.get_recv_size() 
		<< "\t" << request;
#endif
	try
	{
		// Parse Response
		Json::Reader reader;
		if(!reader.parse(StringTool::utf8_to_gbk(sr.m_data.c_str()), value))
			throw ComException("Response Parse Failed!");

		// 校验返回的状态
		if(value.isMember("status") && 0 == value["status"].asInt())
		{
			std::string err = value["errormsg"].asString();
			throw ComException(err.c_str());
		}
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
}

HANDLE HttpConnection::request_response_async( receiver *recv, const std::string& request )
{
	//inner_package* ip = new inner_package;	// 由线程释放
	//ip->m_conn = this;
	//ip->m_recv = recv;
	//ip->m_requst = request;
	//return (HANDLE)_beginthreadex(0, 0, &HttpConnection::thread_proc, (void*)ip, 0, 0);

	HANDLE event = CreateEvent(0, TRUE, FALSE, 0);
	request_context rc;
	rc.m_event = event;
	rc.m_conn = this;
	rc.m_recv = recv;
	rc.m_requst = request;
	g_list_lock.Lock();
	g_request_list.push_back(rc);
	g_list_lock.Unlock();
	ReleaseSemaphore(g_thread_semaphore, 1, 0);
	return event;
}

unsigned __stdcall HttpConnection::thread_proc( void* lpvoid )
{
//	inner_package* ip = static_cast<inner_package*>(lpvoid);
//	try
//	{
//#ifdef LOG_REQUEST_COST
//		LARGE_INTEGER t1, t2,tc;
//		QueryPerformanceFrequency(&tc); 
//		QueryPerformanceCounter(&t1); 
//#endif
//		ip->m_conn->request_response_imp(ip->m_recv, ip->m_requst);
//#ifdef LOG_REQUEST_COST
//		QueryPerformanceCounter(&t2); 
//		double interval = (double)(t2.QuadPart- t1.QuadPart) / tc.QuadPart;
//		DefaultLogger::getSingleton().stream(Informative) << t1.QuadPart 
//			<< "\t" << interval 
//			<< "\t" << ip->m_recv->get_recv_size() 
//			<< "\t" << ip->m_requst;
//#endif
//	}
//	catch(...)
//	{
//		// do nothing
//	}
//	delete ip;
//	_endthreadex(0);
//	return 0;

	HANDLE obj[2] = {g_thread_semaphore, g_thread_exit};
	bool exit = false;
	while(!exit)
	{
		DWORD rt = WaitForMultipleObjects(2, obj, FALSE, INFINITE);
		switch(rt)
		{
		case WAIT_OBJECT_0 + 0:
			{
				g_list_lock.Lock();
				if(g_request_list.empty())
				{
					g_list_lock.Unlock();
					continue;
				}
				request_context rc = g_request_list.front();
				g_request_list.pop_front();
				g_list_lock.Unlock();

				try
				{
#ifdef LOG_REQUEST_COST
					LARGE_INTEGER t1, t2,tc;
					QueryPerformanceFrequency(&tc); 
					QueryPerformanceCounter(&t1); 
#endif
					rc.m_conn->request_response_imp(rc.m_recv, rc.m_requst);
#ifdef LOG_REQUEST_COST
					QueryPerformanceCounter(&t2); 
					double interval = (double)(t2.QuadPart- t1.QuadPart) / tc.QuadPart;
					DefaultLogger::getSingleton().stream(Informative) << t1.QuadPart 
						<< "\t" << interval 
						<< "\t" << rc.m_recv->get_recv_size() 
						<< "\t" << rc.m_requst;
#endif
				}
				catch(...)
				{
					// do nothing
				}
				SetEvent(rc.m_event);
				break;
			}
		case WAIT_OBJECT_0 + 1:
			{
				exit = true;
				break;
			}
		default:
			break;
		}
	}
	_endthreadex(0);
	return 0;
}

void HttpConnection::request_response_imp( receiver *recv, const std::string& request )
{
	if(!m_hConnect)
		throw ComException("连接句柄不能为空！");
	if(request.empty())
		throw ComException("请求不能为空！");
	HINTERNET hRequest = WinHttpOpenRequest(m_hConnect, L"POST", m_urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_ESCAPE_PERCENT);
	if(!hRequest)
	{
		CString err_msg;
		err_msg.Format("Error %s in WinHttpOpenRequest.\n", get_error_msg(GetLastError()));
		throw ComException(err_msg.GetBuffer());
	}
	AutoCloseHandle ach(hRequest);
	std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\nAccept: */*\r\n";
	BOOL bResults = WinHttpSendRequest( hRequest, headers.c_str(), -1, (void*)request.c_str(), request.length(), request.length(), 0);
	if(!bResults)
	{
		CString err_msg;
		err_msg.Format("Error %s in WinHttpSendRequest.\n", get_error_msg(GetLastError()));
		throw ComException(err_msg.GetBuffer());
	}
	bResults = WinHttpReceiveResponse( hRequest, NULL);
	if(!bResults)
	{
		CString err_msg;
		err_msg.Format("Error %s in WinHttpReceiveResponse.\n", get_error_msg(GetLastError()));
		throw ComException(err_msg.GetBuffer());
	}
	DWORD dwCode = 0;
	DWORD dwSize = sizeof(DWORD);
	bResults = WinHttpQueryHeaders( hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER ,WINHTTP_HEADER_NAME_BY_INDEX, &dwCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
	if(!bResults)
	{
		CString err_msg;
		err_msg.Format("Error %s in WinHttpQueryHeaders.\n", get_error_msg(GetLastError()));
		throw ComException(err_msg.GetBuffer());
	}
	if(dwCode != 200)
	{
		CString err_msg;
		err_msg.Format("STATUS_CODE %u .\n", dwCode);
		throw ComException(err_msg.GetBuffer());
	}
	do 
	{
		dwSize = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
		{
			CString err_msg;
			err_msg.Format("Error %s in WinHttpQueryDataAvailable.\n", get_error_msg(GetLastError()));
			throw ComException(err_msg.GetBuffer());
		}
		char* pszOutBuffer = new char[dwSize + 1];
		if (!pszOutBuffer)
		{
			throw ComException("Out of memory.\n");
		}
		else
		{
			ZeroMemory(pszOutBuffer, dwSize + 1);
			if (!WinHttpReadData(hRequest, (void*)pszOutBuffer, dwSize, 0))
			{
				delete [] pszOutBuffer;
				CString err_msg;
				err_msg.Format("Error %s in WinHttpReadData.\n", get_error_msg(GetLastError()));
				throw ComException(err_msg.GetBuffer());
			}
			else
			{
				recv->push_data(pszOutBuffer, dwSize);
			}
			delete [] pszOutBuffer;
		}
	} while (dwSize > 0 && !recv->is_abort());
}

const char* HttpConnection::get_error_msg(DWORD dwErrorCode)
{
	switch (dwErrorCode)
	{
	case ERROR_WINHTTP_OUT_OF_HANDLES: 
		return "ERROR_WINHTTP_OUT_OF_HANDLES";
	case ERROR_WINHTTP_TIMEOUT: 
		return "ERROR_WINHTTP_TIMEOUT";
	case ERROR_WINHTTP_INTERNAL_ERROR: 
		return "ERROR_WINHTTP_INTERNAL_ERROR";
	case ERROR_WINHTTP_INVALID_URL: 
		return "ERROR_WINHTTP_INVALID_URL";
	case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: 
		return "ERROR_WINHTTP_UNRECOGNIZED_SCHEME";
	case ERROR_WINHTTP_NAME_NOT_RESOLVED: 
		return "ERROR_WINHTTP_NAME_NOT_RESOLVED";
	case ERROR_WINHTTP_INVALID_OPTION: 
		return "ERROR_WINHTTP_INVALID_OPTION";
	case ERROR_WINHTTP_OPTION_NOT_SETTABLE: 
		return "ERROR_WINHTTP_OPTION_NOT_SETTABLE";
	case ERROR_WINHTTP_SHUTDOWN: 
		return "ERROR_WINHTTP_SHUTDOWN";
	case ERROR_WINHTTP_LOGIN_FAILURE: 
		return "ERROR_WINHTTP_LOGIN_FAILURE";
	case ERROR_WINHTTP_OPERATION_CANCELLED: 
		return "ERROR_WINHTTP_OPERATION_CANCELLED";
	case ERROR_WINHTTP_INCORRECT_HANDLE_TYPE: 
		return "ERROR_WINHTTP_INCORRECT_HANDLE_TYPE";
	case ERROR_WINHTTP_INCORRECT_HANDLE_STATE: 
		return "ERROR_WINHTTP_INCORRECT_HANDLE_STATE";
	case ERROR_WINHTTP_CANNOT_CONNECT: 
		return "ERROR_WINHTTP_CANNOT_CONNECT";
	case ERROR_WINHTTP_CONNECTION_ERROR: 
		return "ERROR_WINHTTP_CONNECTION_ERROR";
	case ERROR_WINHTTP_RESEND_REQUEST: 
		return "ERROR_WINHTTP_RESEND_REQUEST";
	case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED: 
		return "ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED";
	case ERROR_WINHTTP_CANNOT_CALL_BEFORE_OPEN: 
		return "ERROR_WINHTTP_CANNOT_CALL_BEFORE_OPEN";
	case ERROR_WINHTTP_CANNOT_CALL_BEFORE_SEND: 
		return "ERROR_WINHTTP_CANNOT_CALL_BEFORE_SEND";
	case ERROR_WINHTTP_CANNOT_CALL_AFTER_SEND: 
		return "ERROR_WINHTTP_CANNOT_CALL_AFTER_SEND";
	case ERROR_WINHTTP_CANNOT_CALL_AFTER_OPEN: 
		return "ERROR_WINHTTP_CANNOT_CALL_AFTER_OPEN";
	case ERROR_WINHTTP_HEADER_NOT_FOUND: 
		return "ERROR_WINHTTP_HEADER_NOT_FOUND";
	case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: 
		return "ERROR_WINHTTP_INVALID_SERVER_RESPONSE";
	case ERROR_WINHTTP_INVALID_HEADER: 
		return "ERROR_WINHTTP_INVALID_HEADER";
	case ERROR_WINHTTP_INVALID_QUERY_REQUEST: 
		return "ERROR_WINHTTP_INVALID_QUERY_REQUEST";
	case ERROR_WINHTTP_HEADER_ALREADY_EXISTS: 
		return "ERROR_WINHTTP_HEADER_ALREADY_EXISTS";
	case ERROR_WINHTTP_REDIRECT_FAILED: 
		return "ERROR_WINHTTP_REDIRECT_FAILED";
	case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR: 
		return "ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR";
	case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT: 
		return "ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT";
	case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT: 
		return "ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT";
	case ERROR_WINHTTP_NOT_INITIALIZED: 
		return "ERROR_WINHTTP_NOT_INITIALIZED";
	case ERROR_WINHTTP_SECURE_FAILURE: 
		return "ERROR_WINHTTP_SECURE_FAILURE";
	case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID: 
		return "ERROR_WINHTTP_SECURE_CERT_DATE_INVALID";
	case ERROR_WINHTTP_SECURE_CERT_CN_INVALID: 
		return "ERROR_WINHTTP_SECURE_CERT_CN_INVALID";
	case ERROR_WINHTTP_SECURE_INVALID_CA: 
		return "ERROR_WINHTTP_SECURE_INVALID_CA";
	case ERROR_WINHTTP_SECURE_CERT_REV_FAILED: 
		return "ERROR_WINHTTP_SECURE_CERT_REV_FAILED";
	case ERROR_WINHTTP_SECURE_CHANNEL_ERROR: 
		return "ERROR_WINHTTP_SECURE_CHANNEL_ERROR";
	case ERROR_WINHTTP_SECURE_INVALID_CERT: 
		return "ERROR_WINHTTP_SECURE_INVALID_CERT";
	case ERROR_WINHTTP_SECURE_CERT_REVOKED: 
		return "ERROR_WINHTTP_SECURE_CERT_REVOKED";
	case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE: 
		return "ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE";
	case ERROR_WINHTTP_AUTODETECTION_FAILED: 
		return "ERROR_WINHTTP_AUTODETECTION_FAILED";
	case ERROR_WINHTTP_HEADER_COUNT_EXCEEDED: 
		return "ERROR_WINHTTP_HEADER_COUNT_EXCEEDED";
	case ERROR_WINHTTP_HEADER_SIZE_OVERFLOW: 
		return "ERROR_WINHTTP_HEADER_SIZE_OVERFLOW";
	case ERROR_WINHTTP_CHUNKED_ENCODING_HEADER_SIZE_OVERFLOW: 
		return "ERROR_WINHTTP_CHUNKED_ENCODING_HEADER_SIZE_OVERFLOW";
	case ERROR_WINHTTP_RESPONSE_DRAIN_OVERFLOW: 
		return "ERROR_WINHTTP_RESPONSE_DRAIN_OVERFLOW";
	case ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY: 
		return "ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY";
	case ERROR_WINHTTP_CLIENT_CERT_NO_ACCESS_PRIVATE_KEY: 
		return "ERROR_WINHTTP_CLIENT_CERT_NO_ACCESS_PRIVATE_KEY";
	default: 
		return "UNKNOW ERROR";
	}
}

HttpConnectionWrapper::HttpConnectionWrapper( HttpConnection* conn )
	: m_connection(conn)
{
	assert(m_connection);
}

HttpConnectionWrapper::~HttpConnectionWrapper()
{
	m_connection->Unlock();
}

int HttpConnectionWrapper::Execute( LPCSTR szSQL,bool bSaveLog /*= true*/ )
{
	return m_connection->Execute(szSQL, bSaveLog);
}

DWORD HttpConnectionWrapper::CreateINTID( LPCSTR strTableName )
{
	return m_connection->CreateINTID(strTableName);
}

CR_IConnection::PLANT_DBTYPE HttpConnectionWrapper::GetDBType()
{
	return m_connection->GetDBType();
}