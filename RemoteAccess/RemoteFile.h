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

	// ��ȡ�ļ�
	bool Read(void* buffer, Uint32 count, Uint32& read);

	// ��ȡ�ļ���Ϣ
	bool GetFileInfo(FileInfo& fi);

protected:
	RemoteHandle	m_handle;
	FileInfo		m_info;
};