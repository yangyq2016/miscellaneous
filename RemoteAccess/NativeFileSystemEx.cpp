#include "stdafx.h"
#include "NativeFileSystemEx.h"
#include "NativeFileEx.h"

File* NativeFileSystemEx::OpenFile(const char * name, Uint32 flags)
{
	return NativeFileEx::Create(name, flags);
}

NativeFileSystemEx* NativeFileSystemEx::Create()
{
	return new NativeFileSystemEx;
}
