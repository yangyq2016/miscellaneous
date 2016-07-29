// 1��ʹ��Winnet���������������ͨ��
// 2������IE���棬�ڴ������������Ч��
// 3�����IE���棬������ӿͻ������������ͨѶ�����������Դδ���£�����������304�룩

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

	// ��Զ���ļ�
	// FileInfo: �������� If-None-Match ������ͷ
	// ����˶Ա�ETAG�������ͬ������ļ����ص�����
	RemoteFile* OpenFile(const char * name, const FileInfoEx& fi);

	// ��ȡԶ��·��(short_path_name)�µĺ�׺��Ϊextension���ļ����б�
	// �б�buffer��RemoteFile�л�ȡ
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