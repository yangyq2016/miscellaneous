#include "stdafx.h"
#include "RemoteFileSystem.h"
#include "..\Tool\StringTool.h"
#include "RemoteFile.h"
#include <sstream>
#include <iostream>
#include "..\tool\Logger.h"
#include <Wininet.h>

RemoteFileSystem::RemoteFileSystem(Logger* logger)
	: m_session(0)
	, m_connect(0)
	, m_logger(logger)
	, m_check_thread(0)
	, m_connect_status(1)
{
	m_exit_event = CreateEvent(0, TRUE, FALSE, 0);
	assert(m_exit_event);
}

RemoteFileSystem::~RemoteFileSystem()
{
	if(m_check_thread)
	{
		SetEvent(m_exit_event);
		WaitForSingleObject(m_check_thread, INFINITE);
		CloseHandle(m_check_thread);
		m_check_thread = 0;
	}
	CloseHandle(m_exit_event);
	m_exit_event = 0;
	if (m_session != NULL)
	{
		InternetCloseHandle(m_session);
		m_session = 0;
	}
	if (m_connect != 0)
	{
		InternetCloseHandle(m_connect);
		m_connect = 0;
	}
}

bool RemoteFileSystem::Initialize(const std::string& root)
{
	if (root.empty())
		return false;
	m_session = InternetOpen("RemoteFileSystem/1.0", 0, 0, 0, 0);
	if (!m_session)
		return false;
	if (InternetSetStatusCallback(m_session, (INTERNET_STATUS_CALLBACK)internet_status_cb) == INTERNET_INVALID_STATUS_CALLBACK)
		return false;
	// Canonicalize the root url
	DWORD dwLen = INTERNET_MAX_URL_LENGTH;
	char* buffer = m_root_url.GetBufferSetLength(dwLen);
	if(!InternetCanonicalizeUrl(root.c_str(), buffer, &dwLen, ICU_BROWSER_MODE))
	{
		m_logger->logEvent("InternetCanonicalizeUrl Failured! ", Errors);
		return false;
	}
	m_root_url.ReleaseBuffer(dwLen);

	// crack it
	URL_COMPONENTS uc;
	memset(&uc, 0, sizeof(URL_COMPONENTS));
	uc.dwStructSize = sizeof(URL_COMPONENTS);
	uc.dwSchemeLength    = -1;
	uc.dwHostNameLength  = -1;
	uc.dwUrlPathLength   = -1;
	uc.dwExtraInfoLength = -1;
	if(!InternetCrackUrl(m_root_url, 0, 0, &uc))
	{
		m_logger->logEvent("InternetCrackUrl Failured! ", Errors);
		return false;
	}
	if(uc.nScheme != INTERNET_SCHEME_HTTP)
	{
		m_logger->logEvent("RemoteFileSystem::Initialize::暂不支持其他协议! ", Errors);
		return false;
	}
	CString strServer(uc.lpszHostName, uc.dwHostNameLength);
	CString strUrlPath(uc.lpszUrlPath, uc.dwUrlPathLength);
	CString strExtraInfo(uc.lpszExtraInfo, uc.dwExtraInfoLength);
	m_uri = strUrlPath + strExtraInfo;
	m_connect = InternetConnect(m_session, strServer, uc.nPort, 0, 0, INTERNET_SERVICE_HTTP, 0, DWORD_PTR(this));
	if(!m_connect)
		return false;
	// CheckConnection Status
	m_connect_status = InternetCheckConnection(m_root_url, FLAG_ICC_FORCE_CONNECTION, 0);
	// CheckConnection Periodic
	m_check_thread = (HANDLE)_beginthreadex(0, 0, periodic_check_connect, this, 0, 0);
	return true;
}

void RemoteFileSystem::SetTimeOut( DWORD dwConnectTimeout, DWORD dwSendTimeout, DWORD dwReceiveTimeout )
{
	if(!InternetSetOption(m_connect, INTERNET_OPTION_CONNECT_TIMEOUT, &dwConnectTimeout, sizeof(DWORD)))
		m_logger->logEvent("SetOption CONNECT_TIMEOUT Failured! ", Errors);
	if(!InternetSetOption(m_connect, INTERNET_OPTION_SEND_TIMEOUT, &dwSendTimeout, sizeof(DWORD)))
		m_logger->logEvent("SetOption SEND_TIMEOUT Failured! ", Errors);
	if(!InternetSetOption(m_connect, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwReceiveTimeout, sizeof(DWORD)))
		m_logger->logEvent("SetOption RECEIVE_TIMEOUT Failured! ", Errors);
}

RemoteFile* RemoteFileSystem::OpenFile(const char * name, const FileInfoEx& fi)
{
	if(m_connect_status == 0)
	{	// for the caller to get error code
		SetLastError(ERROR_INTERNET_CANNOT_CONNECT); 
		return 0;
	}
	if(!name || strlen(name) == 0)
		return 0;
	std::string filename = StringTool::gbk_to_utf8(name);
	filename = StringTool::url_encode(filename);
	filename = "&filename=" + filename;
	CString request = m_uri + filename.c_str();

	RemoteHandle handle = HttpOpenRequest(m_connect, "GET", request, HTTP_VERSION
		, 0, 0, INTERNET_FLAG_EXISTING_CONNECT, DWORD_PTR(this));
	if(!handle)
		return 0;
	// 检查本地缓存
	if(fi.m_exist)
	{
		DWORD dwNeeded = 0;
		CString url = m_root_url + filename.c_str();
		GetUrlCacheEntryInfo(url, 0, &dwNeeded);
		if(GetLastError() == ERROR_FILE_NOT_FOUND)
		{
			std::ostringstream header;
			header << "If-None-Match: \"" << fi.m_info.fileSize << "-" << fi.m_info.lastWriteTime << "\"\r\n";
			HttpAddRequestHeaders(handle, header.str().c_str(), header.str().length(), HTTP_ADDREQ_FLAG_ADD_IF_NEW);
		}
	}
	if(!HttpSendRequest(handle, 0, 0, 0, 0))
	{
		InternetCloseHandle(handle);
		return 0;
	}
	DWORD status_code;
	DWORD dwLen = sizeof(DWORD);
	if(!HttpQueryInfo(handle, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER , &status_code, &dwLen, 0))
	{
		m_logger->logEvent("Query Status Code Failured! ", Errors);
		InternetCloseHandle(handle);
		return 0;
	}
	if(status_code == 200)
	{
		SYSTEMTIME st;
		dwLen = sizeof(SYSTEMTIME);
		if(!HttpQueryInfo(handle, HTTP_QUERY_FLAG_SYSTEMTIME | HTTP_QUERY_LAST_MODIFIED, &st, &dwLen, 0))
		{
			m_logger->logEvent("Query Last Modified Failured! ", Errors);
			InternetCloseHandle(handle);
			return 0;
		}
		FILETIME ft;
		SystemTimeToFileTime(&st, &ft);
		CString strLength;
		dwLen = 0;
		if (!HttpQueryInfo(handle, HTTP_QUERY_CONTENT_LENGTH, NULL, &dwLen, 0))
		{
			if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			{
				m_logger->logEvent("Query Content Length Failured! ", Errors);
				InternetCloseHandle(handle);
				return 0;
			}
			LPTSTR pstr = strLength.GetBufferSetLength(dwLen);
			if (HttpQueryInfo(handle, HTTP_QUERY_CONTENT_LENGTH, pstr, &dwLen, 0))
			{
				strLength.ReleaseBuffer(dwLen);
			}
			else
			{
				strLength.ReleaseBuffer(0);
				m_logger->logEvent("Query Content Length Failured! ", Errors);
				InternetCloseHandle(handle);
				return 0;
			}
		}
		FileInfo info;
		info.fileSize = _atoi64(strLength);
		info.lastWriteTime = *(Uint64*)&ft;
		// check remote file is newer or not
		if(!memcmp(&info, &fi.m_info, sizeof(FileInfo)))
		{
			m_logger->stream(Informative) << "StatusCode = 304, request = " << name;
			InternetCloseHandle(handle);
			return 0;
		}
		m_logger->stream(Informative) << "StatusCode = 200, request = " << name;
		return new RemoteFile(handle, info);
	}
	else
	{
		m_logger->stream(status_code == 304 ? Informative : Errors) 
			<< "StatusCode = " << status_code << ", request = " << name;
		InternetCloseHandle(handle);
		return 0;
	}
}

RemoteFile* RemoteFileSystem::GetFileList( const std::string& short_path_name, const std::string& extension /*= ""*/ )
{
	if(m_connect_status == 0)
	{	// for the caller to get error code
		SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
		return 0;
	}
	if(short_path_name.empty())
		return 0;
	std::string path = StringTool::gbk_to_utf8(short_path_name.c_str());
	path = StringTool::url_encode(path);
	std::string uri = "&filename=" + path + "&extension=" + extension;
	CString request = m_uri + uri.c_str();

	RemoteHandle handle = HttpOpenRequest(m_connect, "GET", request, HTTP_VERSION
		, 0, 0, INTERNET_FLAG_EXISTING_CONNECT | INTERNET_FLAG_DONT_CACHE, DWORD_PTR(this));// do not cache
	if(!handle)
		return 0;
	if(!HttpSendRequest(handle, 0, 0, 0, 0))
	{
		InternetCloseHandle(handle);
		return 0;
	}
	DWORD status_code;
	DWORD dwLen = sizeof(DWORD);
	if(!HttpQueryInfo(handle, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER , &status_code, &dwLen, 0))
	{
		m_logger->logEvent("Query Status Code Failured! ", Errors);
		InternetCloseHandle(handle);
		return 0;
	}
	if(status_code == 200)
	{
		return new RemoteFile(handle);
	}
	else
	{
		m_logger->stream(Errors) << "StatusCode = " << status_code << ", request = " << uri;
		InternetCloseHandle(handle);
		return 0;
	}
}

void RemoteFileSystem::internet_status_cb( RemoteHandle handle, DWORD_PTR context, DWORD status, LPVOID info, DWORD length )
{
	RemoteFileSystem* rfs = (RemoteFileSystem*)context;
	CString strInfo;
	switch(status)
	{
	case INTERNET_STATUS_RESOLVING_NAME:
		{
			strInfo.Format("resolving name for %s", info);
			break;
		}
	case INTERNET_STATUS_NAME_RESOLVED:
		{
			strInfo.Format("resolved name for %s", info);
			break;
		}
	case INTERNET_STATUS_CONNECTING_TO_SERVER:
		{
			sockaddr_in* sa = (sockaddr_in*) info;
			strInfo.Format("connecting to socket address '%s'", inet_ntoa(sa->sin_addr));
			break;
		}
	case INTERNET_STATUS_CONNECTED_TO_SERVER:
		{
			sockaddr_in* sa = (sockaddr_in*) info;
			strInfo.Format("connected to socket address '%s'", inet_ntoa(sa->sin_addr));
			break;
		}
	case INTERNET_STATUS_SENDING_REQUEST:
		strInfo = "sending request...";
		break;
	case INTERNET_STATUS_REQUEST_SENT:
		strInfo.Format("request sent! (%d)bytes", *(DWORD*)info);
		break;
	case INTERNET_STATUS_RECEIVING_RESPONSE:
		strInfo = "receiving response...";
		break;
	case INTERNET_STATUS_RESPONSE_RECEIVED:
		strInfo.Format("response received! (%d)bytes", *(DWORD*)info);
		break;
	case INTERNET_STATUS_CLOSING_CONNECTION:
		strInfo = "closing connection!";
		break;
	case INTERNET_STATUS_CONNECTION_CLOSED:
		strInfo = "connection closed!";
		break;
	case INTERNET_STATUS_HANDLE_CREATED:
		strInfo.Format("handle %8.8X created!", handle);
		break;
	case INTERNET_STATUS_HANDLE_CLOSING:
		strInfo.Format("handle %8.8X closed!", handle);
		break;
	case INTERNET_STATUS_DETECTING_PROXY:
		strInfo = "DETECTING_PROXY";
		break;
	case INTERNET_STATUS_REQUEST_COMPLETE:
		strInfo = "request complete.";
		break;
	case INTERNET_STATUS_INTERMEDIATE_RESPONSE:
		strInfo = "INTERMEDIATE_RESPONSE";
		break;
	case INTERNET_STATUS_USER_INPUT_REQUIRED:
		strInfo = "USER_INPUT_REQUIRED";
		break;
	case INTERNET_STATUS_STATE_CHANGE:
		strInfo = "STATE_CHANGE";
		break;
	case INTERNET_STATUS_COOKIE_SENT:
		strInfo = "COOKIE_SENT";
		break;
	case INTERNET_STATUS_COOKIE_RECEIVED:
		strInfo = "COOKIE_RECEIVED";
		break;
	case INTERNET_STATUS_P3P_HEADER:
		strInfo = "P3P_HEADER";
		break;
	case INTERNET_STATUS_COOKIE_HISTORY:
		strInfo = "COOKIE_HISTORY";
		break;
	default:
		return;
	}
	rfs->m_logger->stream(Informative) << strInfo;
}

unsigned __stdcall RemoteFileSystem::periodic_check_connect( void* lpvoid )
{
	RemoteFileSystem* mfs = (RemoteFileSystem*)lpvoid;
	HANDLE timer = CreateWaitableTimer(0, TRUE, 0);
	if(timer)
	{
		LARGE_INTEGER liTime;
		liTime.QuadPart = -50000000;  // 5 seconds
		if(SetWaitableTimer(timer, &liTime, 0, 0, 0, 0))
		{
			HANDLE obj[2] = {mfs->m_exit_event, timer};
			while(true)
			{
				DWORD rt = WaitForMultipleObjects(2, obj, FALSE, INFINITE);
				if(rt == WAIT_OBJECT_0 + 0)
				{
					break;
				}
				else if(rt == WAIT_OBJECT_0 + 1)
				{
					BOOL rt = InternetCheckConnection(mfs->m_root_url, FLAG_ICC_FORCE_CONNECTION, 0);
					InterlockedExchange(&mfs->m_connect_status, rt == TRUE ? 1 : 0);
					SetWaitableTimer(timer, &liTime, 0, 0, 0, 0);
				}
				else
				{
					break;
				}
			}
		}
		CloseHandle(timer);
		timer = 0;
	}
	_endthreadex(0);
	return 0;
}

