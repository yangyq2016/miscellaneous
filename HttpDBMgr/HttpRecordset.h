// 基于HTTP协议的数据库访问服务
// 记录集
// version 2.0
// by yyq

#pragma once
#include "..\R_DBManager.h"
#include <map>
#include <vector>
#include "..\Tool\Utility.h"
#include "..\json\json.h"
#include "HttpConnection.h"


#pragma region HttpRecordset_Async
// 异步读取
// 二进制流
class HttpRecordset_Async : public CR_IRecordset, public receiver
{
	// typedef std::map<std::string, size_t>	FieldList;
	// 由map改为multimap 解决了记录集存在重复字段导致数据读取异常的问题
	typedef std::multimap<std::string, size_t>	FieldList;	
	typedef std::vector<std::pair<size_t, size_t> >	RowList;
	typedef size_t							Cursor;

public:
	HttpRecordset_Async();
	virtual ~HttpRecordset_Async();

public:
	HRESULT		Open (LPCSTR szSQL, CR_IConnection* con ,long CursorType=OpenKeyset,long LockType=LockOptimistic, long Options =CmdText );
	bool		CloseRs();

	bool		GetRecordsetData(double& dValue, LPCTSTR lpszFieldName);
	bool		GetRecordsetData(CString& strValue, LPCTSTR lpszFieldName);
	bool		GetRecordsetData(COleDateTime& time, LPCTSTR lpszFieldName);
	bool		GetRecordsetData(char*& buffer, int& iBufSize, LPCTSTR lpszFieldName);
	bool		GetRecordsetData(int &iValue, LPCSTR lpszFieldName);
	bool		GetRecordsetData(char *pBuf, int iSize, LPCSTR lpszFieldName);
	bool		GetRecordsetData(float &fValue, LPCSTR lpszFieldName);
	bool        GetRecordsetData(DWORD& dwValue,LPCSTR lpszFieldName);
	bool        GetRecordsetData(char*pBuf,LPCSTR lpszFieldName);
	bool        GetRecordsetData_2(char*& buffer, int& iBufSize, LPCTSTR lpszFieldName);

	int         ValueNotNull(LPCSTR lpszFieldName);// 判断字段值是否为空，如果为空返回0
	BOOL		IsEmpty();
	int			ExistField(LPCTSTR strField); // 是否存在某个字段,不存在返回-1
	bool		GetFields(std::vector<_STR_FieldInfo>& fields);


	bool		GetClobData(CString& strValue, int& IN iBufSize, LPCTSTR IN lpszFieldName);
	void		ReleaseBlobData();
	bool		GetBlobData(const char* strField, BYTE*& pData, int& iCount);
	bool		SetBlobData(const char* strField, BYTE* pData, int iDataCount);

	long		AddNew();
	void		PutCollect(const _variant_t& Field, const _variant_t& Value, bool bDate = false);
	void		Update(const char* table_name = NULL, const char* where = NULL);
	long		MoveNext();
	long		MovePrevious();
	long		MoveFirst();
	long		MoveLast();
	bool		IsBOF();
	bool		IsEOF();

	HRESULT		Save(const char* pFileNamePath, DB_PersistFormatEnum enType);
	HRESULT		put_CacheSize(long size);
	HRESULT		put_CursorLocation(CursorLocation location);

private:
	inline bool		find_field_index(size_t& index, const char* field);
	inline const std::pair<size_t, size_t>& get_current_row();
	void		get_column_value(size_t index, bool to_gbk, std::string& value);
private:
	HttpConnection*		m_connection;
private:
	FieldList			m_Fieldlist;	// 列名列表
	RowList				m_RowList;		// 行列表

	Cursor				m_Cursor;		// 记录集游标
	Cursor				m_PageCursor;	// 记录集分页游标

	size_t				m_TotalSize;	// 记录集大小
	size_t				m_PageSize;		// 记录集分页大小

	Json::Value			m_DML_EX;		//	for insert/update
private:
	BYTE*				m_Blob;

private:
	HANDLE				m_close_signal;
	HANDLE				m_read_thread;
	HANDLE				m_readable_signal;

	char				m_error_code;
	std::string				m_error_msg;
	std::vector<char>	m_buffer;			//	接收到的buffer
	Cursor				m_buffer_cursor;	//	buffer的读取游标

	__Mutex				m_mutex;
public:
	bool is_abort(void) const{ return WaitForSingleObject(m_close_signal, 0) == WAIT_OBJECT_0;}
	void push_data(char* data, size_t size);
	size_t get_recv_size(void) const {return m_buffer.size();}
};
#pragma endregion

/************************************分割线*****************************************/

#pragma region HttpRecordset_Sync
// 同步读取
// 字符流
class HttpRecordset_Sync : public CR_IRecordset
{
	// typedef std::map<std::string, Json::ArrayIndex>	FieldList;
	// 由map改为multimap 解决了记录集存在重复字段导致数据读取异常的问题
	typedef std::multimap<std::string, Json::ArrayIndex>	FieldList;	
	typedef std::vector<Json::Value>		RowList;
	typedef size_t							Cursor;
public:
	HttpRecordset_Sync();
	virtual ~HttpRecordset_Sync();

public:
	HRESULT		Open (LPCSTR szSQL, CR_IConnection* con ,long CursorType=OpenKeyset,long LockType=LockOptimistic, long Options =CmdText );
	bool		CloseRs();

	bool		GetRecordsetData(double& dValue, LPCTSTR lpszFieldName);
	bool		GetRecordsetData(CString& strValue, LPCTSTR lpszFieldName);
	bool		GetRecordsetData(COleDateTime& time, LPCTSTR lpszFieldName);
	bool		GetRecordsetData(char*& buffer, int& iBufSize, LPCTSTR lpszFieldName);
	bool		GetRecordsetData(int &iValue, LPCSTR lpszFieldName);
	bool		GetRecordsetData(char *pBuf, int iSize, LPCSTR lpszFieldName);
	bool		GetRecordsetData(float &fValue, LPCSTR lpszFieldName);
	bool        GetRecordsetData(DWORD& dwValue,LPCSTR lpszFieldName);
	bool        GetRecordsetData(char*pBuf,LPCSTR lpszFieldName);
	bool        GetRecordsetData_2(char*& buffer, int& iBufSize, LPCTSTR lpszFieldName);

	int         ValueNotNull(LPCSTR lpszFieldName);// 判断字段值是否为空，如果为空返回0
	BOOL		IsEmpty();
	int			ExistField(LPCTSTR strField); // 是否存在某个字段,不存在返回-1
	bool		GetFields(std::vector<_STR_FieldInfo>& fields);


	bool		GetClobData(CString& strValue, int& IN iBufSize, LPCTSTR IN lpszFieldName);
	void		ReleaseBlobData();
	bool		GetBlobData(const char* strField, BYTE*& pData, int& iCount);
	bool		SetBlobData(const char* strField, BYTE* pData, int iDataCount);

	long		AddNew();
	void		PutCollect(const _variant_t& Field, const _variant_t& Value, bool bDate = false);
	void		Update(const char* table_name = NULL, const char* where = NULL);
	long		MoveNext();
	long		MovePrevious();
	long		MoveFirst();
	long		MoveLast();
	bool		IsBOF();
	bool		IsEOF();

	HRESULT		Save(const char* pFileNamePath, DB_PersistFormatEnum enType);
	HRESULT		put_CacheSize(long size);
	HRESULT		put_CursorLocation(CursorLocation location);

private:
	inline bool		find_field_index(Json::ArrayIndex& index, const char* field);
	inline const Json::Value& get_current_row();
	void		phase_recordset(const Json::Value& root);
private:
	HttpConnection*		m_connection;
	std::string			m_sql;

private:
	FieldList			m_Fieldlist;	// 列名列表
	RowList				m_RowList;		// 行列表

	Cursor				m_Cursor;		// 记录集游标
	Cursor				m_PageCursor;	// 记录集分页游标

	size_t				m_TotalSize;	// 记录集大小
	size_t				m_PageSize;		// 记录集分页大小

	Json::Value			m_DML_EX;		//for insert/update
private:
	BYTE*				m_Blob;
	static std::string	base64_prefix;
	static	Cursor		Cursor_end;
};

#pragma endregion