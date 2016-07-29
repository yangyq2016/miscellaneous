#include "stdafx.h"
#include "QuadManager.h"
#include "kernel_interface/kernel_interface.h"

template<> QuadManager* Singleton<QuadManager>::ms_Singleton = 0;

long_ptr QuadManager::g_memory_used = 0;

DWORD QuadManager::g_loaded_count = 0;

DWORD QuadManager::g_loading_count = 0;

DWORD QuadManager::g_nonloaded_count = 0;

DWORD QuadManager::g_unloading_count = 0;

static const double g_west = -180.0f;	//degree
static const double g_east = 180.0f;	//degree
static const double g_south = -90.0f;	//degree
static const double g_north = 90.0f;	//degree


QuadManager::QuadManager()
	: m_forbidden_load(false)
{
	m_box.SetInfinite();
	EnableAllRenderView(true);
	EnableRenderView(render_view_screen, false);
	CDEA_Framework* framework = CKernelInterface::getSingleton().m_pFramework;
	m_render_queue = framework->m_renderQueueManager->GetRenderQueue((Uint32)-1);
	framework->m_sceneManager->AddSpatial(this);
}

QuadManager::~QuadManager()
{
	RemoveFromOctree();
	SafeReleaseSetNull(m_render_queue);
	LoadQueueList::iterator pos = m_load_queue_list.begin();
	while(pos != m_load_queue_list.end())
	{
		SafeReleaseSetNull(pos->second);
		pos = m_load_queue_list.erase(pos);
	}
}

void QuadManager::Clip( RenderParameter& renderParameter, int intersection )
{
	if(!RenderUsageIsScene(renderParameter.usage))
		return;
	m_render_queue->AddItem(this);
}

void QuadManager::Render( RenderParameter& param )
{
#ifdef _DEBUG
	TraceTimeSpan tts;
#endif
	g_loaded_count = 0;
	g_loading_count = 0;
	g_nonloaded_count = 0;
	g_unloading_count = 0;

	//处理卸载
	Quad* nil = 0;
	Quad* cur = 0;
	Quad* next = 0;
	Uint32 stop_time_stamp = Time::GetTime() + 5;
	Uint32 frame = param.frame - 30;
	int intersection;
	double mindis;
	nil = static_cast<Quad*>(m_loading.Nil_());
	cur = static_cast<Quad*>(m_loading.GetHead());
	while(nil != cur && Time::GetTime() < stop_time_stamp)
	{
		next = cur->get_next_quad();
		intersection = param.sceneSpaceViewFrustum.IntersectAABB(cur->m_box);
		mindis = GetMinDistance(cur->m_box, param.wgsCameraPos);
		if((intersection == frustum_intersection_outside || mindis > cur->GetVisibleDistance()) && cur->m_frame < frame)
		{
			++g_unloading_count;
			if(cur->unload(stop_time_stamp))
				cur->remove_from_quad_list();
		}
		cur = next;
	}
	nil = static_cast<Quad*>(m_loaded.Nil_());
	cur = static_cast<Quad*>(m_loaded.GetHead());
	while(nil != cur && Time::GetTime() < stop_time_stamp)
	{
		next = cur->get_next_quad();
		intersection = param.sceneSpaceViewFrustum.IntersectAABB(cur->m_box);
		mindis = GetMinDistance(cur->m_box, param.wgsCameraPos);
		if((intersection == frustum_intersection_outside || mindis > cur->GetVisibleDistance()) && cur->m_frame < frame)
		{
			++g_unloading_count;
			cur->remove_from_quad_list();
			if(!cur->unload(stop_time_stamp))
				m_loading.AddTail(cur);
		}
		cur = next;
	}
#ifdef _DEBUG
	tts.Trace("QuadManager::FrameMove::UnLoad: ", 5);
#endif

	// 加载被禁用
	if(m_forbidden_load)
		return;

	// 内存占用是否超限
	bool memory_available = !CKernelInterface::getSingletonPtr()->is_memory_overload();

	// 处理加载
	stop_time_stamp = Time::GetTime() + 7;
	LoadQueueList::iterator pos = m_load_queue_list.begin();
	LoadQueueList::iterator end = m_load_queue_list.end();
	while(pos != end)
	{
		nil = static_cast<Quad*>(pos->second->Nil_());
		cur = static_cast<Quad*>(pos->second->GetHead());
		while(nil != cur)
		{
			next = cur->get_next_quad();
			cur->remove_from_quad_list();
			int load_state = cur->get_load_state();
			bool move_on = Time::GetTime() < stop_time_stamp;
			switch(load_state)
			{
			case -1:
				{
					++g_nonloaded_count;
					if(move_on)
					{
						if(cur->load(stop_time_stamp))
							m_loaded.AddTail(cur);
						else
							m_loading.AddTail(cur);
					}
					break;
				}
			case 0:
				{
					++g_loading_count;
					if(move_on && cur->load(stop_time_stamp))
						m_loaded.AddTail(cur);
					else
						m_loading.AddTail(cur);
					break;
				}
			case 1:
				{
					++g_loaded_count;
					m_loaded.AddTail(cur);
					break;
				}
			}
			cur = next;
		}
		++pos;
	}
#ifdef _DEBUG
	tts.Trace("QuadManager::FrameMove::Load: ", 7);
#endif
}

void QuadManager::caculate_quadid( QuadID& id, const LonLat& ll, Sizef quad_size /*= Sizef(0.01f,0.01f)*/ )
{
	if(quad_size.cx < 0.01f)
		quad_size.cx = 0.01f;
	if(quad_size.cy < 0.01f)
		quad_size.cy = 0.01f;
	id = QuadID(floor(ll.m_lat / quad_size.cy), floor(ll.m_lon / quad_size.cx));
}

void QuadManager::caculate_box( AxisAlignedBox3d& box, const QuadID& id, Sizef quad_size /*= Sizef(0.01f,0.01f)*/ )
{
	if(quad_size.cx < 0.01f)
		quad_size.cx = 0.01f;
	if(quad_size.cy < 0.01f)
		quad_size.cy = 0.01f;
	LonLatAlt lla[4];
	lla[0].m_alt = lla[1].m_alt = lla[2].m_alt = lla[3].m_alt = 0.0f;

	lla[0].m_lon = id.m_col_num * quad_size.cx;
	lla[0].m_lat = id.m_row_num * quad_size.cy;

	lla[2].m_lon = lla[0].m_lon + quad_size.cx;
	lla[2].m_lat = lla[0].m_lat + quad_size.cy;

	lla[1].m_lon = lla[0].m_lon;
	lla[1].m_lat = lla[2].m_lat;

	lla[3].m_lon = lla[2].m_lon;
	lla[3].m_lat = lla[0].m_lat;

	Vector3d wgs[4];
	for (int i = 0; i < 4; ++i)
		LonLatAltToWGS(wgs[i], lla[i]);
	AxisAlignedBox3BuildFromPoint(box, wgs, 4);
}

LoadQueue* QuadManager::get_load_queue( int priority )
{
	LoadQueueList::iterator find = m_load_queue_list.find(priority);
	if(find != m_load_queue_list.end())
		return find->second;
	LoadQueue* queue = new LoadQueue;
	m_load_queue_list.insert(make_pair(priority, queue));
	return queue;
}

void QuadManager::enable_forbidden_load( bool enable )
{
	m_forbidden_load = enable;
}

bool QuadManager::is_enable_forbidden_load( void ) const
{
	return m_forbidden_load;
}
