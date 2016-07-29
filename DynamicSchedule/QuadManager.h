#pragma once
#include "QuadID.h"
#include "DeA_AppExtendInclude.h"
#include "kernel_interface/Singleton/Singleton.h"
#include "kernel_interface/ExportInclude.h"
#include "kernel_interface/Tool/Utility.h"


// 块
class Quad : public DoublyLink<Quad>, public Spatial
{
	friend class QuadManager;
public:
	// -1-未加载,0-加载部分,1-加载完成
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

// 加载队列
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

// 块管理器
class QuadManager 
	: public Spatial
	, public Renderable
	, public Singleton<QuadManager>
{
public:
	typedef map<int, LoadQueue*>			LoadQueueList;
	QuadManager();
	~QuadManager();

	// 统计用
	static long_ptr	g_memory_used;
	static DWORD	g_nonloaded_count;
	static DWORD	g_loading_count;
	static DWORD	g_loaded_count;
	static DWORD	g_unloading_count;
protected:
	void Clip(RenderParameter& renderParameter, int intersection);
	void Render(RenderParameter& param);
public:
	// 根据经纬度及块大小计算块ID
	static void caculate_quadid(QuadID& id, const LonLat& ll, Sizef quad_size = Sizef(0.05f,0.05f));

	// 根据块ID及块大小计算包围盒
	static void caculate_box(AxisAlignedBox3d& box, const QuadID& id, Sizef quad_size = Sizef(0.05f,0.05f));

	// 获取LoadQueue（主线程）
	LoadQueue* get_load_queue(int priority);
private:
	RenderQueue*		m_render_queue;
	QuadList			m_loading;
	QuadList			m_loaded;
	LoadQueueList		m_load_queue_list;

private:
	// 是否禁止加载，当该变量为true时
	// 系统会把已加载好的全部卸载
	bool				m_forbidden_load;	
public:
	// 是否禁止加载
	void enable_forbidden_load(bool enable);
	bool is_enable_forbidden_load(void) const;
};