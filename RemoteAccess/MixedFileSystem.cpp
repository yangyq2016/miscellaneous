#include "stdafx.h"
#include "MixedFileSystem.h"
#include "RemoteFileSystem.h"
#include "NativeFileSystemEx.h"
#include "NativeFileEx.h"
#include "RemoteFile.h"
#include <algorithm>
#include <sstream>
#include "../Tool/StringTool.h"
#include <afxinet.h>

#define READ_BUFFER_SIZE 4096

template<> MixedFileSystem* Singleton<MixedFileSystem>::ms_Singleton = 0;

MixedFileSystem::MixedFileSystem()
	: m_native_file_system(0)
	, m_remote_file_system(0)
	, m_logger(new Logger)
{
	m_mid_path_list.resize(rmp_count);
	m_mid_path_list[rmp_blank] = "";
	m_mid_path_list[rmp_interface] = "plant\\";
	m_mid_path_list[rmp_service] = "service_plugin\\";
	m_mid_path_list[rmp_texture] = "map\\";
}

MixedFileSystem::~MixedFileSystem()
{
	SafeReleaseSetNull(m_native_file_system);
	SafeReleaseSetNull(m_remote_file_system);
	SafeDeleteSetNull(m_logger);
}

void MixedFileSystem::setNativeRootPath(const std::string& native_root_path)
{
	if(!stricmp(m_native_root_path.c_str(), native_root_path.c_str()))
		return;
	SafeReleaseSetNull(m_native_file_system);
	NativeFileSystemEx* nfs = NativeFileSystemEx::Create();
	m_native_file_system = SubFileSystem::Create(nfs, native_root_path.c_str());
	m_native_root_path.assign(m_native_file_system->GetPathName());
	nfs->Release();
}

void MixedFileSystem::setRemoteRootPath(const std::string& remote_root_path)
{
	if(!stricmp(m_remote_root_path.c_str(), remote_root_path.c_str()))
		return;
	m_remote_root_path = remote_root_path;
	SafeReleaseSetNull(m_remote_file_system);
	m_remote_file_system = new RemoteFileSystem(m_logger);
	if (!m_remote_file_system->Initialize(remote_root_path))
		SafeReleaseSetNull(m_remote_file_system);
}

std::string MixedFileSystem::getPathName(const std::string& short_file_name, RES_MID_PATH rmp /*= rmp_blank*/)
{
	if (rmp < rmp_interface || rmp > rmp_texture)
		rmp = rmp_blank;
	return m_native_root_path + m_mid_path_list[rmp] + short_file_name;;
}

void MixedFileSystem::setResMidPath( RES_MID_PATH rmp, const std::string& mid_path )
{
	if (rmp < rmp_interface || rmp > rmp_texture)
		return;
	std::string path = mid_path;
	if(!mid_path.empty())
	{
		const char* szpath = path.c_str();
		const char * p1 = (const char *)strrchr(szpath, '\\');
		const char * p2 = (const char *)strrchr(szpath, '/');
		if(p1 < p2)
		{
			p1 = p2;
		}
		if(!(0 != p1 && path.size() == p1 - szpath + 1))
		{
			path += '/';
		}
	}
	m_mid_path_list[rmp] = path;
}

bool MixedFileSystem::makeSureFileExistAndLatest( const std::string& full_file_name )
{
	if(full_file_name.empty())
		return false;
	std::string::size_type length = m_native_root_path.length();
	if (strnicmp(full_file_name.c_str(), m_native_root_path.c_str(), length) != 0)
		return false;
	std::string short_file_name = full_file_name.substr(length);
	return makeSureFileExistAndLatest(short_file_name, rmp_blank);
}

bool MixedFileSystem::makeSureFileExistAndLatest( const std::string& short_file_name, RES_MID_PATH rmp )
{
	if (short_file_name.empty() || rmp < rmp_blank || rmp > rmp_texture)
		return false;
	std::string file_name = m_mid_path_list[rmp] + short_file_name;

	// query native file info
	FileInfoEx fi;
	ZeroMemory(&fi, sizeof(FileInfoEx));
	m_read_write_lock.ReadLock();
	fi.m_exist = m_native_file_system->GetFileInfo(fi.m_info, file_name.c_str());
	m_read_write_lock.ReadUnlock();
	if(!m_remote_file_system)
		return fi.m_exist;

	// to upper
	std::string file_name_upper(file_name);
	std::transform(file_name_upper.begin(), file_name_upper.end(), file_name_upper.begin(), ::toupper);

	// 记录访问记录
	m_read_write_lock.WriteLock();
	AccessRecordList::iterator it = m_access_record_list.find(file_name_upper);
	if(it != m_access_record_list.end())
	{
		if(it->second == 0)
		{
			// 上次访问无错误，直接返回
			m_read_write_lock.WriteUnlock(); 
			return fi.m_exist;
		}
	}
	else
	{
		// 新增访问记录
		it = m_access_record_list.insert(make_pair(file_name_upper, 0)).first;
	}

	// 访问远程服务器
	SharedPtr<RemoteFile> remote_file(m_remote_file_system->OpenFile(file_name.c_str(), fi), false);
	if (!remote_file)
	{
		DWORD dwError = ::GetLastError();
		bool flag = dwError > INTERNET_ERROR_BASE && dwError <= INTERNET_ERROR_LAST;
		if(flag)	
		{
			it->second = dwError;	// 只捕获与网络相关的错误
		}
		// 注意死锁,m_logger内部有锁
		m_read_write_lock.WriteUnlock();
		if(flag)
		{
			CInternetException e(dwError);
			CString errmsg;
			LPTSTR pstr = errmsg.GetBufferSetLength(100);
			if(e.GetErrorMessage(pstr, 100))
			{
				errmsg.Trim("\r\n");
				m_logger->stream(Errors) << "File = " << file_name << ", ErrCode = " << dwError << ", ErrMsg = " << errmsg;
			}
		}
		return fi.m_exist;
	}
	else
	{
		char tmp[READ_BUFFER_SIZE];
		Uint32 read;
		std::vector<byte_t> buffer;
		while(remote_file->Read(tmp, READ_BUFFER_SIZE, read) && read)	// 读取远程文件
			buffer.insert(buffer.end(), tmp, tmp + read);
		size_t count = buffer.size();
		if (0 != count)
		{
			std::string full_name = m_native_root_path + file_name;
			std::string::size_type slash = full_name.find_last_of("\\/");
			if (std::string::npos != slash)// 新建文件夹
			{
				std::string dir = full_name.substr(0, slash + 1);
				Platform::Directory_Create(dir.c_str());
			}
			File* file = m_native_file_system->OpenFile(file_name.c_str(), File::off_create_always | File::off_write);// 新建本地文件
			if (file)
			{
				SharedPtr<NativeFileEx> native_file(static_cast<NativeFileEx*>(file), false);

				Int64 move = 0, newPos;
				if (native_file->Seek(move, File::sf_begin, newPos) && newPos == move)
				{
					Uint32 written;
					if (native_file->Write(&buffer[0], count, written) && written == count)
					{
						// 修改lastWritetime
						if(remote_file->GetFileInfo(fi.m_info))
							native_file->SetFileTime(fi.m_info.lastWriteTime);
						m_read_write_lock.WriteUnlock();
						return true;// 成功
					}
				}
			}
		}
	}
	m_read_write_lock.WriteUnlock();
	return false;
}

std::string MixedFileSystem::appendMidPath( const std::string& short_file_name, RES_MID_PATH rmp )
{
	if (rmp < rmp_interface || rmp > rmp_texture)
		rmp = rmp_blank;
	return m_mid_path_list[rmp] + short_file_name;
}

bool MixedFileSystem::makeSurePathExistAndLatest( const std::string& full_path_name, const std::string& extension /*= ""*/ )
{
	if(full_path_name.empty())
		return false;
	std::string::size_type length = m_native_root_path.length();
	if (strnicmp(full_path_name.c_str(), m_native_root_path.c_str(), length) != 0)
		return false;
	std::string short_path_name = full_path_name.substr(length);
	return makeSurePathExistAndLatest(short_path_name, rmp_blank, extension);
}

bool MixedFileSystem::makeSurePathExistAndLatest( const std::string& short_path_name, RES_MID_PATH rmp, const std::string& extension /*= ""*/ )
{
	if (short_path_name.empty() || rmp < rmp_blank || rmp > rmp_texture)
		return false;
	std::string path_name = m_mid_path_list[rmp] + short_path_name;

	// create empty directory
	std::string full_path = m_native_root_path + path_name;
	bool res = Platform::Directory_Create(full_path.c_str());
	if(!m_remote_file_system)
		return res;
	SharedPtr<RemoteFile> remote_file(m_remote_file_system->GetFileList(path_name, extension), false);
	if (!remote_file)	// 远程文件不存在 || 不需要更新
		return false;
	std::vector<byte_t> buffer;
	char tmp[READ_BUFFER_SIZE];
	Uint32 read; 
	while(remote_file->Read(tmp, READ_BUFFER_SIZE, read) && read)	// 读取远程文件
		buffer.insert(buffer.end(), tmp, tmp + read);
	if(buffer.empty())
		return true;
	std::string content(buffer.begin(), buffer.end());
	content = StringTool::utf8_to_gbk(content.c_str());
	std::istringstream iss(content);
	std::string file_name;
	while(!iss.eof())
	{
		iss >> file_name;
		makeSureFileExistAndLatest(path_name + file_name, rmp_blank);
	}
	return true;
}

void MixedFileSystem::setLogFileName( const std::string& filename, LoggingLevel level)
{
	m_logger->setLoggingLevel(level);
	m_logger->setLogFilename(filename);
}

void MixedFileSystem::setTimeOut( DWORD dwConnectTimeout, DWORD dwSendTimeout /*= 20000*/, DWORD dwReceiveTimeout /*= 20000*/ )
{
	if(m_remote_file_system)
		m_remote_file_system->SetTimeOut(dwConnectTimeout, dwSendTimeout, dwReceiveTimeout);
}

