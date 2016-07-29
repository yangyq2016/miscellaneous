#pragma once
#include "DeepEyeKernel\File\DeepEye_NativeFile.h"

using namespace DeepEye;

//����NativeFile δ�ṩ SetFileTime�ӿڣ�������д

class NativeFileEx : public NativeFile
{
	friend class NativeFileSystemEx;
public:
	NativeFileEx(FileSystem* fileSystem, Platform::FileHandle hFile);
	~NativeFileEx();

	bool SetFileTime(Uint64 file_time);
private:
	NativeFileEx(const NativeFileEx&);
	NativeFileEx& operator=(const NativeFileEx&);

public:
	static NativeFileEx* Create(const char * name, Uint32 flags);
};