#include "stdafx.h"
#include "NativeFileEx.h"


NativeFileEx::NativeFileEx(FileSystem* fileSystem, Platform::FileHandle hFile)
: NativeFile(fileSystem, hFile)
{

}

NativeFileEx::~NativeFileEx()
{

}

bool NativeFileEx::SetFileTime(Uint64 file_time)
{
	return FALSE != ::SetFileTime(m_fileHandle, (FILETIME*)&file_time, (FILETIME*)&file_time, (FILETIME*)&file_time);
}

NativeFileEx* NativeFileEx::Create(const char * name, Uint32 flags)
{
	NativeFileEx* res = 0;
	Platform::FileHandle file = Platform::File_Open(name, flags);
	if (invalid_file_handle != file)
	{
		res = deep_eye_new NativeFileEx(0, file);
	}
	return res;
}

