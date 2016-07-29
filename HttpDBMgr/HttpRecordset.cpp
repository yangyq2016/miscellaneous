#include "stdafx.h"
#include "HttpRecordset.h"
#include "..\Tool\StringTool.h"
#include <sstream>
#include <algorithm>
#include "..\Tool\Base64.h"
#include "..\DefaultLogger.h"
#include <string>
#include <winhttp.h>
#include "..\Tool\Encryptor.h"

using namespace std;

#define SafeDeleteSetNull(p) { if( 0 != (p)) { delete (p); (p) = NULL; } }

static	const COleDateTime	Begin_time = COleDateTime(1970,1,1,0,0,0);

#pragma region HttpRecordset_Async

HttpRecordset_Async::HttpRecordset_Async()
	: m_connection(0)
{
	m_Cursor = 0;
	m_PageCursor = 0;
	m_TotalSize = 0;
	m_PageSize = 0;
	m_Blob = 0;

	m_read_thread = 0;
	m_readable_signal = CreateSemaphore(0, 0, LONG_MAX, 0);
	m_close_signal = CreateEvent(0, TRUE, FALSE, 0);
	m_error_code = 1;
	m_buffer_cursor = 0;
}

HttpRecordset_Async::~HttpRecordset_Async()
{
	CloseRs();
	CloseHandle(m_readable_signal);
	m_readable_signal = 0;
	CloseHandle(m_close_signal);
	m_close_signal = 0;
}

HRESULT HttpRecordset_Async::Open( LPCSTR szSQL, CR_IConnection* con ,long CursorType/*=OpenKeyset*/,long LockType/*=LockOptimistic*/, long Options /*=CmdText */ )
{
	if(!con || !szSQL || strlen(szSQL) == 0)
		return E_FAIL;
	if(m_read_thread)
		throw ComException("记录集未关闭");
	HttpConnectionWrapper* wrapper = dynamic_cast<HttpConnectionWrapper*>(con);
	if(!wrapper || !wrapper->m_connection)
		return E_FAIL;
	m_connection = wrapper->m_connection;

	Json::Value value;
	Json::FastWriter fw;
	value["mode"] = "dql-stream";
	value["sql"] = szSQL;
	std::string post;
	post = fw.write(value);
	post = StringTool::gbk_to_utf8(post.c_str());

	// try des ecb encrypt
	std::string prefix("json=");
	if (m_connection->is_encrypt())
	{
		std::string post_old = post;
		std::vector<char> outbuf, inbuf(post.begin(), post.end());
		unsigned char key[8];
		memcpy(key, HttpConnection::encryptkey.c_str(), 8);
		if (Encryptor::Des_Ecb_Encrypt(outbuf, inbuf, key, Encryptor::deo_encrypt))
		{
			int outlen = Base64::EncodeLen(outbuf.size());
			post.resize(outlen);
			Base64::Encode(&post[0], (unsigned char*)&outbuf[0], outbuf.size());
			prefix = "encrypt=1&" + prefix;
		}
		else
		{
			post = post_old;
		}
	}
	post = StringTool::url_encode(post);
	post = prefix + post;

	// 发起请求
	m_read_thread = m_connection->request_response_async(this, post);
	//等待读取信号
	HANDLE handle[2] = {m_readable_signal, m_read_thread};
	DWORD rt = WaitForMultipleObjects(2, handle, FALSE, INFINITE);
	switch(rt)
	{
	case WAIT_OBJECT_0 + 0:	// have data to read
		{
			if(m_error_code == 0)
			{
				throw ComException(m_error_msg);
			}
			break;
		}
	case WAIT_OBJECT_0 + 1:	// thread exit
		{
			size_t size = m_RowList.size();
			if(size != m_TotalSize)	// 异步不分页，意味着只有一页数据，该页数据大小应该==m_TotalSize
			{
				DefaultLogger::getSingleton().stream() << "HttpRecordset_Async::MoveNext-->m_RowList.size()(" 
					<< size <<") != m_TotalSize("<< m_TotalSize <<")";
				m_TotalSize = size;	// satisfy with new size
			}
			break;
		}
		
	}

	return S_OK;
}

bool HttpRecordset_Async::CloseRs()
{
	if(m_read_thread)
	{
		if(WaitForSingleObject(m_read_thread, 0) != WAIT_OBJECT_0)
		{
			SetEvent(m_close_signal);
			WaitForSingleObject(m_read_thread, INFINITE);
			ResetEvent(m_close_signal);
		}
		CloseHandle(m_read_thread);
		m_read_thread = 0;
	}
	// decrease m_readable_signal to zero
	do
	{
		DWORD rt = WaitForSingleObject(m_readable_signal, 0);
		if(rt == WAIT_OBJECT_0)
			continue;
		else
			break;
	}while(true);

	m_error_code = 1;
	m_buffer_cursor = 0;
	m_buffer.clear();

	m_connection = 0;
	m_Fieldlist.clear();
	m_RowList.clear();
	m_Cursor = 0;
	m_PageCursor = 0;
	m_TotalSize = 0;
	m_PageSize = 0;
	SafeDeleteSetNull(m_Blob);
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( double& dValue, LPCTSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, false, value);
	if(value.empty())
	{
		dValue = 0.0f;
	}
	else
	{
		dValue = atof(value.c_str());
	}
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( CString& strValue, LPCTSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, true, value);
	if(value.empty())
	{
		strValue.Empty();
	}
	else
	{
		strValue = value.c_str();
	}
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( COleDateTime& time, LPCTSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, true, value);
	if(value.empty())
	{
		time = 0.0f;
	}
	else
	{
		return time.ParseDateTime(value.c_str());
	}
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( char*& buffer, int& iBufSize, LPCTSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, true, value);
	if(value.empty())
	{
		buffer = 0;
		iBufSize = 0;
	}
	else
	{
		iBufSize = value.length();
		buffer = new char[iBufSize];
		memcpy(buffer, value.c_str(), iBufSize);
	}
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( int &iValue, LPCSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, false, value);
	if(value.empty())
	{
		iValue = 0;
	}
	else
	{
		iValue = atoi(value.c_str());
	}
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( char *pBuf, int iSize, LPCSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, true, value);
	if(value.empty())
	{
		ZeroMemory(pBuf, iSize);
	}
	else
	{
		int iBufSize = value.length();
		memcpy(pBuf, value.c_str(), min(iBufSize, iSize));
	}
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( float &fValue, LPCSTR lpszFieldName )
{
	double dValue = 0.0f;
	if(!GetRecordsetData(dValue, lpszFieldName))
	{
		fValue = 0.0f;
		return false;
	}
	fValue = (float)dValue;
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( DWORD& dwValue,LPCSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, false, value);
	if(value.empty())
	{
		dwValue = 0;
	}
	else
	{
		dwValue = strtoul(value.c_str(), 0, 10);
	}
	return true;
}

bool HttpRecordset_Async::GetRecordsetData( char*pBuf,LPCSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, true, value);
	if(value.empty())
	{
		*pBuf = 0;
	}
	else
	{
		strcpy(pBuf, value.c_str());
	}
	return true;
}

bool HttpRecordset_Async::GetRecordsetData_2( char*& buffer, int& iBufSize, LPCTSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	string value;
	get_column_value(index, true, value);
	if(value.empty())
	{
		buffer = 0;
		iBufSize = 0;
	}
	else
	{
		iBufSize = value.length();
		buffer = new char[iBufSize];
		memcpy(buffer, value.c_str(), iBufSize);
	}
	return true;
}

int HttpRecordset_Async::ValueNotNull( LPCSTR lpszFieldName )
{
	size_t index;
	if(!find_field_index(index, lpszFieldName))
		return 0;
	string value;
	get_column_value(index, false, value);
	return value.empty() ? 0 : 1;
	return 0;
}

BOOL HttpRecordset_Async::IsEmpty()
{
	return 0 == m_TotalSize ? TRUE : FALSE;
}

int HttpRecordset_Async::ExistField( LPCTSTR strField )
{
	size_t index;
	if(!find_field_index(index, strField))
		return -1;
	return 1;
}

bool HttpRecordset_Async::GetFields( std::vector<_STR_FieldInfo>& fields )
{
	for (FieldList::iterator it = m_Fieldlist.begin(); it != m_Fieldlist.end(); ++it)
	{
		fields.push_back(_STR_FieldInfo());
		fields.back().strFieldName = it->first;
	}
	return !fields.empty();
}

bool HttpRecordset_Async::GetClobData( CString& strValue, int& IN iBufSize, LPCTSTR IN lpszFieldName )
{
	if(!GetRecordsetData(strValue, lpszFieldName))
		return false;
	iBufSize = strValue.GetLength();
	return true;
}

void HttpRecordset_Async::ReleaseBlobData()
{
	SafeDeleteSetNull(m_Blob);
}

bool HttpRecordset_Async::GetBlobData( const char* strField, BYTE*& pData, int& iCount )
{
	size_t index;
	if(!find_field_index(index, strField))
		throw ComException("字段名不存在！");
	pData = 0;
	iCount = 0;
	string value;
	get_column_value(index, false, value);
	if(value.empty())
	{
		return false;
	}
	else
	{
		iCount = value.length();
		pData = new BYTE[iCount];
		memcpy(pData, value.c_str(), iCount);
		return true;
	}
	return false;
}

bool HttpRecordset_Async::SetBlobData( const char* strField, BYTE* pData, int iDataCount )
{
	if(!strField || strlen(strField) == 0 || !pData || iDataCount == 0)
		return false;
	Json::Value field;
	field["name"] = strField;
	field["type"] = "data:blob/resafety;base64";
	int inbytes = Base64::EncodeLen(iDataCount);
	char* coded_str = new char[inbytes];
	Base64::Encode(coded_str, pData, iDataCount);
	field["val"] = coded_str;
	delete[] coded_str;
	m_DML_EX["columns"].append(field);
	return true;
}

long HttpRecordset_Async::AddNew()
{
	m_DML_EX.clear();
	m_DML_EX["action"] = "insert";
	return 0;
}

void HttpRecordset_Async::PutCollect( const _variant_t& Field, const _variant_t& Value, bool bDate /*= false*/ )
{
	if(V_VT(&Field) != VT_BSTR)
		throw ComException("字段名必须是字符串！");
	Json::Value field;
	char* name = _com_util::ConvertBSTRToString(V_BSTR(&Field));
	field["name"] = name;
	delete [] name;
	switch(V_VT(&Value))
	{
	case VT_BOOL:
		{
			field["type"] = "int";
			field["val"] = V_BOOL(&Value) ? 1 : 0;
			break;
		}
	case VT_I1:
		{
			field["type"] = "int";
			field["val"] = V_I1(&Value);
			break;
		}
	case VT_UI1:
		{
			field["type"] = "int";
			field["val"] = V_UI1(&Value);
			break;
		}
	case VT_I2:
		{
			field["type"] = "int";
			field["val"] = V_I2(&Value);
			break;
		}
	case VT_UI2:
		{
			field["type"] = "int";
			field["val"] = V_UI2(&Value);
			break;
		}
	case VT_I4:
		{
			field["type"] = "int";
			field["val"] = V_I4(&Value);
			break;
		}
	case VT_UI4:
		{
			field["type"] = "int";
			field["val"] = (unsigned int)V_UI4(&Value);
			break;
		}
#if (_WIN32_WINNT >= 0x0501)
	case VT_I8:
		{
			field["type"] = "int";
			field["val"] = V_I8(&Value);
			break;
		}
	case VT_UI8:
		{
			field["type"] = "int";
			field["val"] = V_UI8(&Value);
			break;
		}
#endif
	case VT_INT:
		{
			field["type"] = "int";
			field["val"] = V_INT(&Value);
			break;
		}
	case VT_UINT:
		{
			field["type"] = "int";
			field["val"] = V_UINT(&Value);
			break;
		}
	case VT_R4:
		{
			field["type"] = "float";
			field["val"] = V_R4(&Value);
			break;
		}
	case VT_R8:
		{
			field["type"] = "double";
			field["val"] = V_R8(&Value);
			break;
		}
	case VT_DATE:
		{
			COleDateTime date = V_R8(&Value);
			COleDateTimeSpan span = date - Begin_time;
			field["type"] = "date";
			field["val"] = 1000 * span.GetTotalSeconds();
			break;
		}
	case VT_BSTR:
		{
			char* value = _com_util::ConvertBSTRToString(V_BSTR(&Value));
			if(bDate)
			{
				COleDateTime date;
				if(!date.ParseDateTime(value))
					throw ComException("日期格式不正确!");
				COleDateTimeSpan span = date - Begin_time;
				field["type"] = "date";
				field["val"] = 1000 * span.GetTotalSeconds();
			}
			else
			{
				field["type"] = "varchar";
				field["val"] = std::string(value);
			}
			delete [] value;
			break;
		}
	default:
		throw ComException("类型不支持！");
	}
	m_DML_EX["columns"].append(field);
}

void HttpRecordset_Async::Update(const char* table_name /*= NULL*/, const char* where /*= NULL*/)
{
	if(!table_name || strlen(table_name) == 0)
	{
		m_DML_EX.clear();
		throw ComException(" 表名不能为空！");
	}
	m_DML_EX["mode"] = "DML_EX";
	m_DML_EX["table"] = table_name;
	if(!m_DML_EX.isMember("action"))
	{
		m_DML_EX["action"] = "update";
		if(!where || strlen(where) == 0)
		{
			m_DML_EX.clear();
			throw ComException("未指定 where 条件！");
		}
		m_DML_EX["where"] = where;
	}
	std::string post;
	try
	{
		Json::FastWriter fw;
		post = fw.write(m_DML_EX);
		post = StringTool::gbk_to_utf8(post.c_str());

		// try des ecb encrypt
		std::string prefix("json=");
		if (m_connection->is_encrypt())
		{
			std::string post_old = post;
			std::vector<char> outbuf, inbuf(post.begin(), post.end());
			unsigned char key[8];
			memcpy(key, HttpConnection::encryptkey.c_str(), 8);
			if (Encryptor::Des_Ecb_Encrypt(outbuf, inbuf, key, Encryptor::deo_encrypt))
			{
				int outlen = Base64::EncodeLen(outbuf.size());
				post.resize(outlen);
				Base64::Encode(&post[0], (unsigned char*)&outbuf[0], outbuf.size());
				prefix = "encrypt=1&" + prefix;
			}
			else
			{
				post = post_old;
			}
		}
		post = StringTool::url_encode(post);
		post = prefix + post;


		m_DML_EX.clear();
	}
	catch(std::runtime_error& e)
	{
		m_DML_EX.clear();
		throw ComException(e.what());
	}

	Json::Value ret;
	m_connection->request_response_sync(ret, post);
}

long HttpRecordset_Async::MoveNext()
{
	if(IsEOF())
		throw ComException("已到达记录集末端！");
	++ m_PageCursor;
	++ m_Cursor;
	if(IsEOF())		// 当全局游标位置到达记录集末端的下一个位置时，结束读取
		return 1;


	HANDLE handle[2] = {m_readable_signal, m_read_thread};
	DWORD rt = WaitForMultipleObjects(2, handle, FALSE, INFINITE);
	switch(rt)
	{
	case WAIT_OBJECT_0 + 0:	// have data to read
		{
			break;
		}
	case WAIT_OBJECT_0 + 1:	// thread exit(means work completed)
		{
			size_t size = m_RowList.size();
			if(size != m_TotalSize)	// 异步不分页，意味着只有一页数据，该页数据大小应该==m_TotalSize
			{
				DefaultLogger::getSingleton().stream() << "HttpRecordset_Async::MoveNext-->m_RowList.size()(" 
					<< size <<") != m_TotalSize("<< m_TotalSize <<")";
				m_TotalSize = size;		// satisfy with new size
				if(m_Cursor > m_TotalSize)
				{
					m_Cursor = m_TotalSize;	// go the end
				}
				if(m_PageCursor > m_TotalSize)
				{
					m_PageCursor = m_TotalSize;	// go the end
				}
			}
			break;
		}
	}
	return 1;
}

long HttpRecordset_Async::MovePrevious()
{
	DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::MovePrevious-->不支持该项操作");
	DebugBreak();	// 暂时屏蔽逆向读取 by yyq
	return 1;
}

long HttpRecordset_Async::MoveFirst()
{
	m_Cursor = 0;
	m_PageCursor = 0;
	return 1;
}

long HttpRecordset_Async::MoveLast()
{
	DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::MoveLast-->不支持该项操作");
	DebugBreak();	// 暂时屏蔽逆向读取 by yyq
	return 0;
}

bool HttpRecordset_Async::IsBOF()
{
	DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::IsBOF-->不支持该项操作");
	DebugBreak();	// 暂时屏蔽 by yyq
	return true;
}

bool HttpRecordset_Async::IsEOF()
{
	return m_Cursor == m_TotalSize;
}

HRESULT HttpRecordset_Async::Save( const char* pFileNamePath, DB_PersistFormatEnum enType )
{
	return E_FAIL;
}

HRESULT HttpRecordset_Async::put_CacheSize( long size )
{
	return E_FAIL;
}

HRESULT HttpRecordset_Async::put_CursorLocation( CursorLocation location)
{
	return E_FAIL;
}

inline bool HttpRecordset_Async::find_field_index( size_t& index, const char* field )
{
	if(!field || strlen(field) == 0)
		return false;
	std::string temp = field;
	std::transform(temp.begin(), temp.end(), temp.begin(), ::toupper);
	FieldList::iterator find = m_Fieldlist.find(temp);
	if(find == m_Fieldlist.end())
		return false;
	index = find->second;
	return true;
}

inline const pair<size_t, size_t>& HttpRecordset_Async::get_current_row()
{
	if(IsEOF())
		throw ComException("已到达记录集末端！");
	else
	{
		try
		{
			return m_RowList.at(m_PageCursor);
		}
		catch(std::out_of_range& e)
		{
			throw ComException(e.what());
		}
	}
}

void HttpRecordset_Async::get_column_value( size_t index, bool to_gbk, string& value )
{
	bool flag = WAIT_OBJECT_0 == WaitForSingleObject(m_read_thread, 0);// 线程数据是否退出
	if(!flag)
		m_mutex.Lock();
	const pair<size_t, size_t>& row = get_current_row();
	if(row.first == row.second)
	{
		value.clear();
		if(!flag)
			m_mutex.Unlock();
		return;
	}
	char* begin = &m_buffer[0];
	size_t cursor = row.first;
	int len = *(int*)(begin + cursor + index * 4);
	if(len < 0)
	{
		DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::get_column_value-->len < 0");
		DebugBreak();// 不能直接return
	}
	else if(len == 0)
	{
		value.clear();
	}
	else
	{
		int pass = 0;
		for (size_t i = 0; i < index; ++i)
			pass += *(int*)(begin + cursor + i * 4);
		int start = m_Fieldlist.size() * 4 + pass;
		cursor += start;
		value.assign(begin + cursor, len);
		if(to_gbk)
			value = StringTool::utf8_to_gbk(value.c_str());
	}
	if(!flag)
		m_mutex.Unlock();
}

void HttpRecordset_Async::push_data( char* data, size_t size )
{
	__AutoLock<__Mutex> lock(m_mutex);
	if(size == 0 || !data || m_error_code == 0)
		return;
	m_buffer.insert(m_buffer.end(), data, data + size);
	size_t length = m_buffer.size();
	size_t cursor = 0;
	char* begin = &m_buffer[0];
	if(m_Fieldlist.empty())
	{
		if(length < cursor + 4)
			return;
		// get requset status
		m_error_code = *begin;
		if(m_error_code != 0 && m_error_code != 1)	// error_code 只能为0或者1
		{
			DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::push_data-->m_error_code != 0 && m_error_code != 1");
			DebugBreak();// 不能直接return
		}
		cursor += 4;	// reserve 4 bytes
		if(0 == m_error_code)// 有异常，直接返回，主线程抛出异常
		{
			// get error msg length
			if(length < cursor + 4)
				return;
			int msg_len = *(int*)(begin + cursor);
			if(msg_len < 0)
			{
				DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::push_data-->msg_len < 0");
				DebugBreak();// 不能直接return
			}
			cursor += 4;
			// get error msg
			if(length < cursor + msg_len)
				return;
			m_error_msg.assign(begin + cursor, msg_len);
			m_error_msg = StringTool::utf8_to_gbk(m_error_msg.c_str());
			cursor += msg_len;
			ReleaseSemaphore(m_readable_signal, 1, 0);	
			return;
		}
		// get total size
		if(length < cursor + 4)
			return;
		int total_size = *(int*)(begin + cursor);
		if(total_size < 0)
		{
			DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::push_data-->total_size < 0");
			DebugBreak();// 不能直接return
		}
		m_TotalSize = total_size;
		cursor += 4;

		// get page size
		if(length < cursor + 4)
			return;
		int page_size = *(int*)(begin + cursor);
		if(page_size < 0)
		{
			DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::push_data-->page_size < 0");
			DebugBreak();// 不能直接return
		}
		m_PageSize = page_size;
		cursor += 4;

		// row total length
		if(length < cursor + 4)
			return;
		int row_len = *(int*)(begin + cursor);
		if(row_len < 0)
		{
			DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::push_data-->row_len < 0");
			DebugBreak();// 不能直接return
		}
		cursor += 4;

		// the end of row
		if(length < cursor + row_len)
			return;
		char* end = begin + cursor + row_len;

		int index = 0;
		while (begin + cursor != end)
		{
			// get the length of name
			int name_len = *(int*)(begin + cursor);
			if(name_len < 0)
			{
				DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::push_data-->name_len < 0");
				DebugBreak();// 不能直接return
			}
			cursor += 4;
			// get column name
			string name(begin + cursor, name_len);
			std::transform(name.begin(), name.end(), name.begin(), ::toupper);
			m_Fieldlist.insert(make_pair(name, index++));
			cursor += name_len;
		}
		m_buffer_cursor = cursor;
	}
	else
	{
		cursor = m_buffer_cursor;
	}
	do
	{
		// get row length
		if(length < cursor + 4)
			return;
		int row_len = *(int*)(begin + cursor);
		if(row_len < 0)
		{
			DefaultLogger::getSingleton().logEvent("HttpRecordset_Async::push_data-->row_len < 0");
			DebugBreak();// 不能直接return
		}
		cursor += 4;
		// get row data
		if(length < cursor + row_len)
			return;
		m_RowList.push_back(make_pair(cursor, cursor + row_len));	// 记录该行的起止索引
		ReleaseSemaphore(m_readable_signal, 1, 0);
		cursor += row_len;
		m_buffer_cursor = cursor;
	}while(true);
}
#pragma endregion


#pragma region HttpRecordset_Sync

std::string HttpRecordset_Sync::base64_prefix = "data:blob/resafety;base64,";

HttpRecordset_Sync::Cursor HttpRecordset_Sync::Cursor_end = (Cursor)-1;

HttpRecordset_Sync::HttpRecordset_Sync()
	: m_connection(0)
{
	m_Cursor = 0;
	m_PageCursor = 0;
	m_TotalSize = 0;
	m_PageSize = 0;
	m_Blob = 0;
}

HttpRecordset_Sync::~HttpRecordset_Sync()
{
	CloseRs();
}

HRESULT HttpRecordset_Sync::Open( LPCSTR szSQL, CR_IConnection* con ,long CursorType/*=OpenKeyset*/,long LockType/*=LockOptimistic*/, long Options /*=CmdText */ )
{
	if(!con || !szSQL || strlen(szSQL) == 0)
		return E_FAIL;
	HttpConnectionWrapper* wrapper = dynamic_cast<HttpConnectionWrapper*>(con);
	if(!wrapper || !wrapper->m_connection)
		return E_FAIL;
	m_sql = szSQL;
	m_connection = wrapper->m_connection;

	Json::Value value;
	Json::FastWriter fw;
	value["mode"] = "DQL";
	value["sql"] = m_sql;
	std::string post;
	post = fw.write(value);
	post = StringTool::gbk_to_utf8(post.c_str());

	// try des ecb encrypt
	std::string prefix("json=");
	if (m_connection->is_encrypt())
	{
		std::string post_old = post;
		std::vector<char> outbuf, inbuf(post.begin(), post.end());
		unsigned char key[8];
		memcpy(key, HttpConnection::encryptkey.c_str(), 8);
		if (Encryptor::Des_Ecb_Encrypt(outbuf, inbuf, key, Encryptor::deo_encrypt))
		{
			int outlen = Base64::EncodeLen(outbuf.size());
			post.resize(outlen);
			Base64::Encode(&post[0], (unsigned char*)&outbuf[0], outbuf.size());
			prefix = "encrypt=1&" + prefix;
		}
		else
		{
			post = post_old;
		}
	}
	post = StringTool::url_encode(post);
	post = prefix + post;

	// 发起请求
	m_connection->request_response_sync(value, post);
	// 解析回应
	phase_recordset(value);
	return S_OK;
}

bool HttpRecordset_Sync::CloseRs()
{
	m_connection = 0;
	m_Fieldlist.clear();
	m_RowList.clear();
	m_Cursor = 0;
	m_PageCursor = 0;
	m_TotalSize = 0;
	m_PageSize = 0;
	SafeDeleteSetNull(m_Blob);
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( double& dValue, LPCTSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
			dValue = 0.0f;
		else
			dValue = atof(entry.asCString());
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( CString& strValue, LPCTSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
			strValue.Empty();
		else
			strValue = entry.asCString();
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( COleDateTime& time, LPCTSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
			time = 0.0f;
		else
		{
			const char* str = entry.asCString();
			return time.ParseDateTime(str);
		}
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( char*& buffer, int& iBufSize, LPCTSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
		{
			buffer = 0;
			iBufSize = 0;
		}
		else
		{
			const char* str = entry.asCString();
			iBufSize = strlen(str) + 1;
			buffer = new char[iBufSize];
			memcpy(buffer, str, iBufSize);
		}
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( int &iValue, LPCSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
			iValue = 0;
		else
			iValue = atoi(entry.asCString());
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( char *pBuf, int iSize, LPCSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
		{
			ZeroMemory(pBuf, iSize);
		}
		else
		{
			const char* str = entry.asCString();
			int iBufSize = strlen(str) + 1;
			memcpy(pBuf, str, min(iBufSize, iSize));
		}
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( float &fValue, LPCSTR lpszFieldName )
{
	double dValue = 0.0f;
	if(!GetRecordsetData(dValue, lpszFieldName))
	{
		fValue = 0.0f;
		return false;
	}
	fValue = (float)dValue;
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( DWORD& dwValue,LPCSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
			dwValue = 0;
		else
			dwValue = strtoul(entry.asCString(), 0, 10);
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData( char*pBuf,LPCSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		throw ComException("字段名不存在！");
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
			*pBuf = 0;
		else
			strcpy(pBuf, entry.asCString());
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return true;
}

bool HttpRecordset_Sync::GetRecordsetData_2( char*& buffer, int& iBufSize, LPCTSTR lpszFieldName )
{
	return true;
}

int HttpRecordset_Sync::ValueNotNull( LPCSTR lpszFieldName )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, lpszFieldName))
		return 0;
	const Json::Value& row = get_current_row();
	try
	{
		return row[index].isNull() ? 0 : 1;
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return 0;
}

BOOL HttpRecordset_Sync::IsEmpty()
{
	return 0 == m_TotalSize ? TRUE : FALSE;
}

int HttpRecordset_Sync::ExistField( LPCTSTR strField )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, strField))
		return -1;
	return 1;
}

bool HttpRecordset_Sync::GetFields( std::vector<_STR_FieldInfo>& fields )
{
	for (FieldList::iterator it = m_Fieldlist.begin(); it != m_Fieldlist.end(); ++it)
	{
		fields.push_back(_STR_FieldInfo());
		fields.back().strFieldName = it->first;
	}
	return !fields.empty();
}

bool HttpRecordset_Sync::GetClobData( CString& strValue, int& IN iBufSize, LPCTSTR IN lpszFieldName )
{
	if(!GetRecordsetData(strValue, lpszFieldName))
		return false;
	iBufSize = strValue.GetLength();
	return true;
}

void HttpRecordset_Sync::ReleaseBlobData()
{
	SafeDeleteSetNull(m_Blob);
}

bool HttpRecordset_Sync::GetBlobData( const char* strField, BYTE*& pData, int& iCount )
{
	Json::ArrayIndex index;
	if(!find_field_index(index, strField))
		throw ComException("字段名不存在！");
	pData = 0;
	iCount = 0;
	const Json::Value& row = get_current_row();
	try
	{
		const Json::Value& entry = row[index];
		if(entry.isNull())
			return false;
		std::string srcstr = entry.asString();
		size_t pos = srcstr.find(base64_prefix);
		if(pos != std::string::npos)
		{
			srcstr = srcstr.erase(pos, base64_prefix.length());
			int outbytes = Base64::DecodeLen(srcstr.c_str());
			SafeDeleteSetNull(m_Blob);
			m_Blob = new BYTE[outbytes];
			iCount = Base64::Decode(m_Blob, srcstr.c_str());
			pData = m_Blob;
			return true;
		}
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
	return false;
}

bool HttpRecordset_Sync::SetBlobData( const char* strField, BYTE* pData, int iDataCount )
{
	if(!strField || strlen(strField) == 0 || !pData || iDataCount == 0)
		return false;
	Json::Value field;
	field["name"] = strField;
	field["type"] = "data:blob/resafety;base64";
	int inbytes = Base64::EncodeLen(iDataCount);
	char* coded_str = new char[inbytes];
	Base64::Encode(coded_str, pData, iDataCount);
	field["val"] = coded_str;
	delete[] coded_str;
	m_DML_EX["columns"].append(field);
	return true;
}

long HttpRecordset_Sync::AddNew()
{
	m_DML_EX.clear();
	m_DML_EX["action"] = "insert";
	return 0;
}

void HttpRecordset_Sync::PutCollect( const _variant_t& Field, const _variant_t& Value, bool bDate /*= false*/ )
{
	if(V_VT(&Field) != VT_BSTR)
		throw ComException("字段名必须是字符串！");
	Json::Value field;
	field["name"] = _com_util::ConvertBSTRToString(V_BSTR(&Field));
	switch(V_VT(&Value))
	{
	case VT_BOOL:
		{
			field["type"] = "int";
			field["val"] = V_BOOL(&Value) ? 1 : 0;
			break;
		}
	case VT_I1:
		{
			field["type"] = "int";
			field["val"] = V_I1(&Value);
			break;
		}
	case VT_UI1:
		{
			field["type"] = "int";
			field["val"] = V_UI1(&Value);
			break;
		}
	case VT_I2:
		{
			field["type"] = "int";
			field["val"] = V_I2(&Value);
			break;
		}
	case VT_UI2:
		{
			field["type"] = "int";
			field["val"] = V_UI2(&Value);
			break;
		}
	case VT_I4:
		{
			field["type"] = "int";
			field["val"] = V_I4(&Value);
			break;
		}
	case VT_UI4:
		{
			field["type"] = "int";
			field["val"] = (unsigned int)V_UI4(&Value);
			break;
		}
#if (_WIN32_WINNT >= 0x0501)
	case VT_I8:
		{
			field["type"] = "int";
			field["val"] = V_I8(&Value);
			break;
		}
	case VT_UI8:
		{
			field["type"] = "int";
			field["val"] = V_UI8(&Value);
			break;
		}
#endif
	case VT_INT:
		{
			field["type"] = "int";
			field["val"] = V_INT(&Value);
			break;
		}
	case VT_UINT:
		{
			field["type"] = "int";
			field["val"] = V_UINT(&Value);
			break;
		}
	case VT_R4:
		{
			field["type"] = "float";
			field["val"] = V_R4(&Value);
			break;
		}
	case VT_R8:
		{
			field["type"] = "double";
			field["val"] = V_R8(&Value);
			break;
		}
	case VT_DATE:
		{
			COleDateTime date = V_R8(&Value);
			COleDateTimeSpan span = date - Begin_time;
			field["type"] = "date";
			field["val"] = 1000 * span.GetTotalSeconds();
			break;
		}
	case VT_BSTR:
		{
			char* value = _com_util::ConvertBSTRToString(V_BSTR(&Value));
			if(bDate)
			{
				COleDateTime date;
				if(!date.ParseDateTime(value))
					throw ComException("日期格式不正确!");
				COleDateTimeSpan span = date - Begin_time;
				field["type"] = "date";
				field["val"] = 1000 * span.GetTotalSeconds();
			}
			else
			{
				field["type"] = "varchar";
				field["val"] = value;
			}
			break;
		}
	default:
		throw ComException("类型不支持！");
	}
	m_DML_EX["columns"].append(field);
}

void HttpRecordset_Sync::Update(const char* table_name /*= NULL*/, const char* where /*= NULL*/)
{
	if(!table_name || strlen(table_name) == 0)
	{
		m_DML_EX.clear();
		throw ComException(" 表名不能为空！");
	}
	m_DML_EX["mode"] = "DML_EX";
	m_DML_EX["table"] = table_name;
	if(!m_DML_EX.isMember("action"))
	{
		m_DML_EX["action"] = "update";
		if(!where || strlen(where) == 0)
		{
			m_DML_EX.clear();
			throw ComException("未指定 where 条件！");
		}
		m_DML_EX["where"] = where;
	}
	std::string post;
	try
	{
		Json::FastWriter fw;
		post = fw.write(m_DML_EX);
		post = StringTool::gbk_to_utf8(post.c_str());

		// try des ecb encrypt
		std::string prefix("json=");
		if (m_connection->is_encrypt())
		{
			std::string post_old = post;
			std::vector<char> outbuf, inbuf(post.begin(), post.end());
			unsigned char key[8];
			memcpy(key, HttpConnection::encryptkey.c_str(), 8);
			if (Encryptor::Des_Ecb_Encrypt(outbuf, inbuf, key, Encryptor::deo_encrypt))
			{
				int outlen = Base64::EncodeLen(outbuf.size());
				post.resize(outlen);
				Base64::Encode(&post[0], (unsigned char*)&outbuf[0], outbuf.size());
				prefix = "encrypt=1&" + prefix;
			}
			else
			{
				post = post_old;
			}
		}
		post = StringTool::url_encode(post);
		post = prefix + post;

		m_DML_EX.clear();
	}
	catch(std::runtime_error& e)
	{
		m_DML_EX.clear();
		throw ComException(e.what());
	}

	Json::Value ret;
	m_connection->request_response_sync(ret, post);
}

long HttpRecordset_Sync::MoveNext()
{
	if(IsEOF())
		throw ComException("已到达记录集末端！");
	++ m_PageCursor;
	++ m_Cursor;
	if(IsEOF())		// 当全局游标位置到达记录集末端的下一个位置时，结束读取
		return 1;
	if(m_PageCursor == m_RowList.size())	// 当分页游标位置到达本页末端的下一个位置时，需要读取下一页数据
	{
		// 数据库中的行号从1开始
		Json::Value value;
		Json::FastWriter fw;
		value["mode"] = "DQL";
		value["sql"] = m_sql;
		value["start"] = m_Cursor + 1;
		value["end"] = m_Cursor + m_PageSize;
		std::string post;
		post = fw.write(value);
		post = StringTool::gbk_to_utf8(post.c_str());

		// try des ecb encrypt
		std::string prefix("json=");
		if (m_connection->is_encrypt())
		{
			std::string post_old = post;
			std::vector<char> outbuf, inbuf(post.begin(), post.end());
			unsigned char key[8];
			memcpy(key, HttpConnection::encryptkey.c_str(), 8);
			if (Encryptor::Des_Ecb_Encrypt(outbuf, inbuf, key, Encryptor::deo_encrypt))
			{
				int outlen = Base64::EncodeLen(outbuf.size());
				post.resize(outlen);
				Base64::Encode(&post[0], (unsigned char*)&outbuf[0], outbuf.size());
				prefix = "encrypt=1&" + prefix;
			}
			else
			{
				post = post_old;
			}
		}
		post = StringTool::url_encode(post);
		post = prefix + post;


		// 发起请求
		m_connection->request_response_sync(value, post);
		// 解析回应
		phase_recordset(value);
		if(m_Fieldlist.empty() || m_RowList.empty())
		{
			// 未读取到任何数据，则认为数据读取完毕
			m_Cursor = m_TotalSize;
			return 1;
		}
		else
		{
			m_PageCursor = 0;
		}
	}
	return 1;
}

long HttpRecordset_Sync::MovePrevious()
{
	if(IsBOF())
		throw ComException("已到达记录集首端！");
	-- m_PageCursor;
	-- m_Cursor;
	if(IsBOF())	// 当全局游标位置到达记录集首端的上一个位置时，结束读取
		return 1;
	if(m_PageCursor == Cursor_end)		// 当游标位置到达本页首端的上一个位置时，需要读取上一页数据
	{
		// 数据库中的行号从1开始
		Json::Value value;
		Json::FastWriter fw;
		value["mode"] = "DQL";
		value["sql"] = m_sql;
		value["start"] = m_Cursor + 2 - m_PageSize;
		value["end"] = m_Cursor + 1;
		std::string post;
		post = fw.write(value);
		post = StringTool::gbk_to_utf8(post.c_str());

		// try des ecb encrypt
		std::string prefix("json=");
		if (m_connection->is_encrypt())
		{
			std::string post_old = post;
			std::vector<char> outbuf, inbuf(post.begin(), post.end());
			unsigned char key[8];
			memcpy(key, HttpConnection::encryptkey.c_str(), 8);
			if (Encryptor::Des_Ecb_Encrypt(outbuf, inbuf, key, Encryptor::deo_encrypt))
			{
				int outlen = Base64::EncodeLen(outbuf.size());
				post.resize(outlen);
				Base64::Encode(&post[0], (unsigned char*)&outbuf[0], outbuf.size());
				prefix = "encrypt=1&" + prefix;
			}
			else
			{
				post = post_old;
			}
		}
		post = StringTool::url_encode(post);
		post = prefix + post;

		// 发起请求
		m_connection->request_response_sync(value, post);
		// 解析回应
		phase_recordset(value);
		if(m_Fieldlist.empty() || m_RowList.empty())
		{
			// 未读取到任何数据，则认为数据读取完毕
			m_Cursor = Cursor_end;
			return 1;
		}
		else
		{
			m_PageCursor = m_RowList.size() - 1;
		}
	}
	return 1;
}

long HttpRecordset_Sync::MoveFirst()
{
	m_Cursor = 0;
	m_PageCursor = 0;
	return 1;
}

long HttpRecordset_Sync::MoveLast()
{
	m_Cursor = m_TotalSize - 1;
	m_PageCursor = m_RowList.size() - 1;
	return 0;
}

bool HttpRecordset_Sync::IsBOF()
{
	return m_Cursor == Cursor_end;
}

bool HttpRecordset_Sync::IsEOF()
{
	return m_Cursor == m_TotalSize;
}

HRESULT HttpRecordset_Sync::Save( const char* pFileNamePath, DB_PersistFormatEnum enType )
{
	return E_FAIL;
}

HRESULT HttpRecordset_Sync::put_CacheSize( long size )
{
	return E_FAIL;
}

HRESULT HttpRecordset_Sync::put_CursorLocation( CursorLocation location)
{
	return E_FAIL;
}

inline bool HttpRecordset_Sync::find_field_index( Json::ArrayIndex& index, const char* field )
{
	if(!field || strlen(field) == 0)
		return false;
	std::string temp = field;
	std::transform(temp.begin(), temp.end(), temp.begin(), ::toupper);
	FieldList::iterator find = m_Fieldlist.find(temp);
	if(find == m_Fieldlist.end())
		return false;
	index = find->second;
	return true;
}

inline const Json::Value& HttpRecordset_Sync::get_current_row()
{
	if(IsBOF())
		throw ComException("已到达记录集首端！");
	else if(IsEOF())
		throw ComException("已到达记录集末端！");
	else
		return m_RowList[m_PageCursor];
}

void HttpRecordset_Sync::phase_recordset( const Json::Value& root)
{
	// 清空上页数据
	m_Fieldlist.clear();
	m_RowList.clear();

	// 解析数据
	if(root.isMember("totalsize"))
		m_TotalSize = root["totalsize"].asUInt();
	if(root.isMember("pagesize"))
		m_PageSize = root["pagesize"].asUInt();

	try
	{
		// 表头数据
		const Json::Value& col = root["columns"];
		Json::ArrayIndex size = col.size();
		for (Json::ArrayIndex i = 0; i < size; ++i)
		{
			std::string& name = col[i].asString();
			std::transform(name.begin(), name.end(), name.begin(), ::toupper);
			m_Fieldlist.insert(make_pair(name, i));
		}
		// 行数据
		const Json::Value& datas = root["data"];
		size = datas.size();
		for (Json::ArrayIndex i = 0; i < size; ++i)
		{
			m_RowList.push_back(datas[i]);
		}
	}
	catch(std::runtime_error& e)
	{
		throw ComException(e.what());
	}
}

#pragma endregion