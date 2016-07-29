#pragma once
#include "QuadManager.h"
#include "kernel_interface/kernel_interface.h"

//#define DEBUG_QUAD

template<typename MetaData, typename Layer> 
class QuadObject 
	: public SharedObject
	, public Quad
#ifdef DEBUG_QUAD
	, public Renderable
#endif
{
public:
	typedef DoublyLinkedList<MetaData>	MetaDataList;
	QuadObject(Layer* layer, const AxisAlignedBox3d& box, int load_priority)
		: m_layer(layer)
#ifdef DEBUG_QUAD
		, m_render_queue(0)
#endif
	{
		m_box.Assign(box);
		EnableAllRenderView(true);
		EnableRenderView(render_view_screen, false);
		m_framework = CKernelInterface::getSingleton().m_pFramework;
		m_framework->m_sceneManager->AddSpatial(this);
		m_load_queue = QuadManager::getSingleton().get_load_queue(load_priority);
	}
	~QuadObject()
	{
#ifdef DEBUG_QUAD
		SafeReleaseSetNull(m_render_queue);
#endif
		remove_from_quad_list();
		RemoveFromOctree();
	}

protected:
	bool	load(DWORD stop_time_stamp)
	{
		while(Time::GetTime() < stop_time_stamp)
		{
			if(m_loading.Empty())
				return true;
			MetaData* data = static_cast<MetaData*>(m_loading.GetHead());
			m_layer->Load(data);
			m_loading.RemoveHead();
			m_loaded.AddTail(data);
		}
		return m_loading.Empty();
	}

	bool	unload(DWORD stop_time_stamp)
	{
		while(Time::GetTime() < stop_time_stamp)
		{
			if(m_loaded.Empty())
				return true;
			MetaData* data = static_cast<MetaData*>(m_loaded.GetHead());
			m_layer->UnLoad(data);
			m_loaded.RemoveHead();
			m_loading.AddTail(data);
		}
		return m_loaded.Empty();
	}

public:
	int		get_load_state(void)
	{
		bool loading = m_loading.Empty();
		bool loaded = m_loaded.Empty();
		if(!loading && !loaded)
			return 0;	// 加载部分
		else if(loading)
			return 1;	// 加载完成
		else
			return -1;	// 未加载
	}

	void Merge(const Vector3d& wgs)
	{
		m_box.Merge(wgs);
	}

	void	add_meta_data(MetaData* data, bool already_been_loaded = false)
	{
		if(!data)
			return;
		if(already_been_loaded)
		{
			m_loaded.AddTail(data);
		}
		else
		{
			m_loading.AddTail(data);
		}
		QuadManager::g_memory_used += data->get_size();
	}

	void Clip(RenderParameter& renderParameter, int intersection)
	{
		if(!RenderUsageIsScene(renderParameter.usage))
			return;
		// 禁止加载
		if(QuadManager::getSingleton().is_enable_forbidden_load())
			return;
		// 内存超限
		if(CKernelInterface::getSingleton().is_memory_overload())
			return;
		double mindis = GetMinDistance(m_box, renderParameter.wgsCameraPos);
		if(mindis > GetVisibleDistance())
			return;
		m_frame = renderParameter.frame;
		m_load_queue->add_item(this);
#ifdef DEBUG_QUAD
		if(!m_render_queue)
			m_render_queue = m_framework->m_renderQueueManager->GetRenderQueue(900);
		if(m_render_queue)
			m_render_queue->AddItem(this);
#endif
	}

#ifdef DEBUG_QUAD
	void Render(RenderParameter& param)
	{
		AxisAlignedBox3f box;
		AxisAlignedBox3Translate(box, m_box, -m_framework->m_sceneManager->GetSceneOrigin());
		m_framework->m_canvas->BeginDraw(param);
		bool loading = m_loading.Empty();
		bool loaded = m_loaded.Empty();
		Uint32 color = 0xffffffff;
		if(!loading && !loaded)
			color = 0xffffff00;	//部分加载
		else if(loading)
			color = 0xffffffff;	// 已加载完成
		else
			color = 0xffffff00;	// 未开始加载
		m_framework->m_canvas->SetColor(color);
		m_framework->m_canvas->DrawAxisAlignedBoxFrame(box, 0);
		m_framework->m_canvas->EndDraw();
	}
#endif

private:
	CDEA_Framework*	m_framework;
	Layer*			m_layer;
	MetaDataList	m_loading;
	MetaDataList	m_loaded;
	LoadQueue*		m_load_queue;

#ifdef DEBUG_QUAD
	RenderQueue*	m_render_queue;
#endif
};