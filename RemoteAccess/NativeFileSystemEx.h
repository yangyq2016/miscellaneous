#pragma once
#include "DeepEyeKernel\File\DeepEye_NativeFileSystem.h"

using namespace DeepEye;

//����NativeFile δ�ṩ SetFileTime�ӿڣ�������д

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