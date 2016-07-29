// 1、使用Winnet方法与服务器进行通信
// 2、引入IE缓存，在大多数情况能提高效率
// 3、清除IE缓存，则会增加客户端与服务器的通讯次数（如果资源未更新，服务器返回304码）

#pragma once
#include "def.h"

using namespace DeepEye; 

class Logger;

class RemoteFile;
class RemoteFileSystem : public SharedObject
{
public:
	RemoteFileSystem(Logger* logger);
	~RemoteFileSystem();

	bool Initialize(const std::string& root);
	void SetTimeOut(DWORD dwConnectTimeout, DWORD dwSendTimeout = 20000, DWORD dwReceiveTimeout = 20000);

	// 打开远程文件
	// FileInfo: 用于生成 If-None-Match 请求报文头
	// 服务端对比ETAG，如果不同，则把文件下载到本地
	RemoteFile* OpenFile(const char * name, const FileInfoEx& fi);

	// 获取远程路径(short_path_name)下的后缀名为extension的文件的列表
	// 列表buffer从RemoteFile中获取
	RemoteFile* GetFileList(const std::string& short_path_name, const std::string& extension = "");

private:
	static void CALLBACK internet_status_cb(RemoteHandle handle, DWORD_PTR context, DWORD status, LPVOID info, DWORD length);
private:
	Logger*				m_logger;
	RemoteHandle		m_session;
	RemoteHandle		m_connect;
	CString				m_root_url;
	CString				m_uri;
	
private:
	static unsigned __stdcall periodic_check_connect(void* lpvoid);
	HANDLE	m_check_thread;
	HANDLE	m_exit_event;
	long	m_connect_status;
};