#pragma once
#include "def.h"
#include <vector>

using namespace DeepEye;

class RemoteFile : public SharedObject
{
public:
	RemoteFile(RemoteHandle handle);
	RemoteFile(RemoteHandle handle, const FileInfo& fi);
	~RemoteFile();

	// 读取文件
	bool Read(void* buffer, Uint32 count, Uint32& read);

	// 读取文件信息
	bool GetFileInfo(FileInfo& fi);

protected:
	RemoteHandle	m_handle;
	FileInfo		m_info;
};