#pragma once
#include "QuadID.h"
#include "DeA_AppExtendInclude.h"
#include "kernel_interface/Singleton/Singleton.h"
#include "kernel_interface/ExportInclude.h"
#include "kernel_interface/Tool/Utility.h"


// ��
class Quad : public DoublyLink<Quad>, public Spatial
{
	friend class QuadManager;
public:
	// -1-δ����,0-���ز���,1-�������
	virtual int		get_load_state(void) = 0;

public:
	Quad*	get_next_quad(void){return static_cast<Quad*>(DoublyLink<Quad>::GetNext());}

	Quad*	get_prev_quad(void){return static_cast<Quad*>(DoublyLink<Quad>::GetPrev());}

	bool	is_quad_free(void){return DoublyLink<Quad>::IsFree();}

	void	remove_from_quad_list(void){DoublyLink<Quad>::Remove();}
public:

	bool	is_spatial_free(void){ return Spatial::IsFree(); }

protected:
	virtual bool	load(DWORD stop_time_stamp) = 0;
	virtual bool	unload(DWORD stop_time_stamp) = 0;
	Uint32 m_frame;
};

typedef DoublyLinkedList<Quad>	QuadList;

// ���ض���
class LoadQueue : public QuadList, public SharedObject
{
public:
	LoadQueue(){}
	~LoadQueue(){}

public:
	void add_item(Quad* quad)
	{
		quad->remove_from_quad_list();
		this->AddTail(quad);
	}
};

// �������
class QuadManager 
	: public Spatial
	, public Renderable
	, public Singleton<QuadManager>
{
public:
	typedef map<int, LoadQueue*>			LoadQueueList;
	QuadManager();
	~QuadManager();

	// ͳ����
	static long_ptr	g_memory_used;
	static DWORD	g_nonloaded_count;
	static DWORD	g_loading_count;
	static DWORD	g_loaded_count;
	static DWORD	g_unloading_count;
protected:
	void Clip(RenderParameter& renderParameter, int intersection);
	void Render(RenderParameter& param);
public:
	// ���ݾ�γ�ȼ����С�����ID
	static void caculate_quadid(QuadID& id, const LonLat& ll, Sizef quad_size = Sizef(0.05f,0.05f));

	// ���ݿ�ID�����С�����Χ��
	static void caculate_box(AxisAlignedBox3d& box, const QuadID& id, Sizef quad_size = Sizef(0.05f,0.05f));

	// ��ȡLoadQueue�����̣߳�
	LoadQueue* get_load_queue(int priority);
private:
	RenderQueue*		m_render_queue;
	QuadList			m_loading;
	QuadList			m_loaded;
	LoadQueueList		m_load_queue_list;

private:
	// �Ƿ��ֹ���أ����ñ���Ϊtrueʱ
	// ϵͳ����Ѽ��غõ�ȫ��ж��
	bool				m_forbidden_load;	
public:
	// �Ƿ��ֹ����
	void enable_forbidden_load(bool enable);
	bool is_enable_forbidden_load(void) const;
};