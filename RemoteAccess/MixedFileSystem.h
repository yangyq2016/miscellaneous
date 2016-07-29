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
	// 资源的 Middle Path
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

	// 设置本地资源根路径
	void setNativeRootPath(const std::string& native_root_path);

	// 设置远程资源根路径
	void setRemoteRootPath(const std::string& remote_root_path);

	// 获取全路径(root + middle_path + short_file_name)
	// root
	//    |
	//    -----middle_path
	//				|
	//				----short_file_name
	std::string getPathName(const std::string& short_file_name, RES_MID_PATH rmp = rmp_blank);

	// 设置资源路径
	// 预定义路径类型
	// 中间路径
	void setResMidPath(RES_MID_PATH rmp, const std::string& mid_path);

	// 确保指定的路径下的文件存在
	// full_file_name 绝对路径（带后缀名）
	bool makeSureFileExistAndLatest(const std::string& full_file_name);

	// 确保指定的路径下的文件存在
	// short_file_name 相对路径（带后缀名）
	// root
	//    |
	//    -----middle_path
	//				|
	//				----short_file_name
	bool makeSureFileExistAndLatest(const std::string& short_file_name, RES_MID_PATH rmp);

	// 附加中间目录（middle_path + short_name）
	std::string appendMidPath(const std::string& short_name, RES_MID_PATH rmp);

	// 确保指定的路径（全路径）及该路径下的文件存在（可指定后缀名）
	bool makeSurePathExistAndLatest(const std::string& full_path_name, const std::string& extension = "");

	// 确保指定的路径（短路径）及该路径下的文件存在（可指定后缀名）
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
	AccessRecordList		m_access_record_list;	// 历史访问记录

	typedef std::vector<std::string>	MidPathList;
	MidPathList			m_mid_path_list;			// 中间路径列表
};