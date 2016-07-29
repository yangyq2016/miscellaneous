#include "stdafx.h"
#include "RemoteFile.h"
#include <afxinet.h>
#include <vector>
#include "RemoteFileSystem.h"

RemoteFile::RemoteFile(RemoteHandle handle)
	: m_handle(handle)
{
	assert(m_handle);
	ZeroMemory(&m_info, sizeof(FileInfo));
}

RemoteFile::RemoteFile(RemoteHandle handle, const FileInfo& fi )
	: m_handle(handle)
{
	assert(m_handle);
	memcpy(&m_info, &fi, sizeof(FileInfo));
}

RemoteFile::~RemoteFile()
{
	if(m_handle)
	{
		InternetCloseHandle(m_handle);
		m_handle = NULL;
	}
}

bool RemoteFile::Read( void* buffer, Uint32 count, Uint32& read )
{
	if (!m_handle)
		return false;
	return TRUE == InternetReadFile(m_handle, buffer, count, &read);
}

bool RemoteFile::GetFileInfo(FileInfo& fi)
{
	if (!m_handle)
		return false;
	memcpy(&fi, &m_info, sizeof(FileInfo));
	return true;
}