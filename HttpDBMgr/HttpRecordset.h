// ����HTTPЭ������ݿ���ʷ���
// ��¼��
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
// �첽��ȡ
// ��������
class HttpRecordset_Async : public CR_IRecordset, public receiver
{
	// typedef std::map<std::string, size_t>	FieldList;
	// ��map��Ϊmultimap ����˼�¼�������ظ��ֶε������ݶ�ȡ�쳣������
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

	int         ValueNotNull(LPCSTR lpszFieldName);// �ж��ֶ�ֵ�Ƿ�Ϊ�գ����Ϊ�շ���0
	BOOL		IsEmpty();
	int			ExistField(LPCTSTR strField); // �Ƿ����ĳ���ֶ�,�����ڷ���-1
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
	FieldList			m_Fieldlist;	// �����б�
	RowList				m_RowList;		// ���б�

	Cursor				m_Cursor;		// ��¼���α�
	Cursor				m_PageCursor;	// ��¼����ҳ�α�

	size_t				m_TotalSize;	// ��¼����С
	size_t				m_PageSize;		// ��¼����ҳ��С

	Json::Value			m_DML_EX;		//	for insert/update
private:
	BYTE*				m_Blob;

private:
	HANDLE				m_close_signal;
	HANDLE				m_read_thread;
	HANDLE				m_readable_signal;

	char				m_error_code;
	std::string				m_error_msg;
	std::vector<char>	m_buffer;			//	���յ���buffer
	Cursor				m_buffer_cursor;	//	buffer�Ķ�ȡ�α�

	__Mutex				m_mutex;
public:
	bool is_abort(void) const{ return WaitForSingleObject(m_close_signal, 0) == WAIT_OBJECT_0;}
	void push_data(char* data, size_t size);
	size_t get_recv_size(void) const {return m_buffer.size();}
};
#pragma endregion

/************************************�ָ���*****************************************/

#pragma region HttpRecordset_Sync
// ͬ����ȡ
// �ַ���
class HttpRecordset_Sync : public CR_IRecordset
{
	// typedef std::map<std::string, Json::ArrayIndex>	FieldList;
	// ��map��Ϊmultimap ����˼�¼�������ظ��ֶε������ݶ�ȡ�쳣������
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

	int         ValueNotNull(LPCSTR lpszFieldName);// �ж��ֶ�ֵ�Ƿ�Ϊ�գ����Ϊ�շ���0
	BOOL		IsEmpty();
	int			ExistField(LPCTSTR strField); // �Ƿ����ĳ���ֶ�,�����ڷ���-1
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
	FieldList			m_Fieldlist;	// �����б�
	RowList				m_RowList;		// ���б�

	Cursor				m_Cursor;		// ��¼���α�
	Cursor				m_PageCursor;	// ��¼����ҳ�α�

	size_t				m_TotalSize;	// ��¼����С
	size_t				m_PageSize;		// ��¼����ҳ��С

	Json::Value			m_DML_EX;		//for insert/update
private:
	BYTE*				m_Blob;
	static std::string	base64_prefix;
	static	Cursor		Cursor_end;
};

#pragma endregion