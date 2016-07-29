#pragma once
#include "DeepEyeKernel\File\DeepEye_NativeFileSystem.h"

using namespace DeepEye;

//由于NativeFile 未提供 SetFileTime接口，于是重写

class NativeFileSystemEx : public NativeFileSystem
{
private:
	NativeFileSystemEx(){};
public:
	~NativeFileSystemEx(){};

	File* OpenFile(const char * name, Uint32 flags);

public:
	static NativeFileSystemEx* Create();
};