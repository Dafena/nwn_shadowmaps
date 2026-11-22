#pragma once
// Linux ownership of draw-buffer selection. Native state is exposed
// again before foreign operations and at the native scene boundary.
namespace tag_buffer_lease {
enum class Fence { Bind, Clear, Selection, Query, Config, Owned, Context, Blit, Boundary, Count };
struct Counters { unsigned acquired=0,reused=0,released=0,gaps=0;unsigned fences[9]={}; };
#ifndef _WIN32
static thread_local unsigned internalDepth=0;
#ifdef NWN_TAG_WORK_BENCH_DIAGNOSTIC
static bool requested=false;
#else
static bool requested=true;
#endif
struct Internal { Internal(){++internalDepth;}~Internal(){--internalDepth;} };
static bool install();
static void shutdown();
static bool enabled();
static void release(Fence);
static bool native_buffers(GLuint,GLenum*);
static bool already_selected(GLuint,const GLenum*);
static void selected(GLuint,const GLenum*,const GLenum*);
static bool retain();
[[maybe_unused]] static void reset_frame();
[[maybe_unused]] static Counters end_native();
[[maybe_unused]] static bool failed();
static bool safe_context();
static void attachment_selected();
static void attachment_finished();
#else
struct Internal { Internal(){} };
static inline void release(Fence){}
static inline bool native_buffers(GLuint,GLenum*){return false;}
static inline bool already_selected(GLuint,const GLenum*){return false;}
static inline void selected(GLuint,const GLenum*,const GLenum*){}
static inline bool retain(){return false;}
static inline bool safe_context(){return true;}
static inline void attachment_selected(){}
static inline void attachment_finished(){}
#endif
}
