#pragma once
#include <string>
#include <set>
#include "engine\include\Dek_KernalInclude.h"
#include "../Singleton/Singleton.h"
#include "../ExportInclude.h"
#include "../Tool/Logger.h"

using namespace DeepEye;

class RemoteFileSystem;
class MixedFileSystem : public Singleton<MixedFileSystem>
{
public:
	// ��Դ�� Middle Path
	enum RES_MID_PATH
	{
		rmp_blank,			// blank
		rmp_interface,		// interfae resources path
		rmp_service,		// service resources path
		rmp_texture,		// texture resources path
		rmp_count
	};
public:
	MixedFileSystem();
	~MixedFileSystem();

public:
	void setLogFileName(const std::string& filename, LoggingLevel level);
	void setTimeOut(DWORD dwConnectTimeout, DWORD dwSendTimeout = 20000, DWORD dwReceiveTimeout = 20000);

	// ���ñ�����Դ��·��
	void setNativeRootPath(const std::string& native_root_path);

	// ����Զ����Դ��·��
	void setRemoteRootPath(const std::string& remote_root_path);

	// ��ȡȫ·��(root + middle_path + short_file_name)
	// root
	//    |
	//    -----middle_path
	//				|
	//				----short_file_name
	std::string getPathName(const std::string& short_file_name, RES_MID_PATH rmp = rmp_blank);

	// ������Դ·��
	// Ԥ����·������
	// �м�·��
	void setResMidPath(RES_MID_PATH rmp, const std::string& mid_path);

	// ȷ��ָ����·���µ��ļ�����
	// full_file_name ����·��������׺����
	bool makeSureFileExistAndLatest(const std::string& full_file_name);

	// ȷ��ָ����·���µ��ļ�����
	// short_file_name ���·��������׺����
	// root
	//    |
	//    -----middle_path
	//				|
	//				----short_file_name
	bool makeSureFileExistAndLatest(const std::string& short_file_name, RES_MID_PATH rmp);

	// �����м�Ŀ¼��middle_path + short_name��
	std::string appendMidPath(const std::string& short_name, RES_MID_PATH rmp);

	// ȷ��ָ����·����ȫ·��������·���µ��ļ����ڣ���ָ����׺����
	bool makeSurePathExistAndLatest(const std::string& full_path_name, const std::string& extension = "");

	// ȷ��ָ����·������·��������·���µ��ļ����ڣ���ָ����׺����
	// root
	//    |
	//    -----middle_path
	//				|
	//				----short_path_name
	bool makeSurePathExistAndLatest(const std::string& short_path_name, RES_MID_PATH rmp, const std::string& extension = "");
private:
	Logger*				m_logger;
	std::string			m_native_root_path;
	std::string			m_remote_root_path;
	ReadWriteLock		m_read_write_lock;
	SubFileSystem*		m_native_file_system;
	RemoteFileSystem*	m_remote_file_system;

	typedef std::map<std::string, DWORD>	AccessRecordList;
	AccessRecordList		m_access_record_list;	// ��ʷ���ʼ�¼

	typedef std::vector<std::string>	MidPathList;
	MidPathList			m_mid_path_list;			// �м�·���б�
};