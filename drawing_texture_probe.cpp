#include "nwn_platform.h"
#include "drawing_probe.h"
#include "drawing_pad_marker.h"
#include "drawing_canvas_quad.h"
#include <cstring>
#include <limits>
#include <vector>
#include <cstdio>
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace {
using TexImage=void(*)(unsigned,int,int,int,int,int,unsigned,unsigned,const void*);
using Delete=void(*)(int,const unsigned*);
using Get=void(*)(unsigned,int*);
TexImage realImage=nullptr;
Delete realDelete=nullptr;
bool observing=false;

void observed_image(unsigned target,int level,int width,int height,unsigned format,unsigned type,const void* pixels) {
    if(!observing || target!=0x0DE1 || level!=0)return;
    const auto context=nwn_drawing_probe_context();
    if(!context)return;
    static auto get=reinterpret_cast<Get>(dlsym(RTLD_DEFAULT,"glGetIntegerv"));
    if(!get)return;
    int texture=0;get(0x8069,&texture);
    if(!texture)return;
    int orientation=-1;
    if(pixels && width==1024 && height==1024 && type==0x1401 && (format==0x1908 || format==0x80E1)){
        int pbo=0,row=0,skipRows=0,skipPixels=0;
        get(0x88EF,&pbo);get(0x0CF2,&row);get(0x0CF3,&skipRows);get(0x0CF4,&skipPixels);
        if(!pbo && (row==0 || row==1024) && !skipRows && !skipPixels)
            orientation=drawing_probe::pad_marker(pixels,format==0x80E1);
    }
    nwn_drawing_probe_texture(unsigned(texture),context,orientation);
}
}

std::uintptr_t nwn_drawing_probe_context() {
    using Current=void*(*)();
#ifdef _WIN32
    static auto current=reinterpret_cast<Current>(dlsym(RTLD_DEFAULT,"wglGetCurrentContext"));
#else
    static auto current=reinterpret_cast<Current>(dlsym(RTLD_DEFAULT,"SDL_GL_GetCurrentContext"));
#endif
    return current?reinterpret_cast<std::uintptr_t>(current()):0;
}

extern "C" void NWN_GL_INTERPOSE(glTexImage2D)(unsigned target,int level,int internal,int width,int height,
                                              int border,unsigned format,unsigned type,const void* pixels) {
    if(!realImage)realImage=reinterpret_cast<TexImage>(dlsym(RTLD_NEXT,"glTexImage2D"));
    if(!realImage)return;
    realImage(target,level,internal,width,height,border,format,type,pixels);
    observed_image(target,level,width,height,format,type,pixels);
}

extern "C" void NWN_GL_INTERPOSE(glDeleteTextures)(int count,const unsigned* textures) {
    if(!realDelete)realDelete=reinterpret_cast<Delete>(dlsym(RTLD_NEXT,"glDeleteTextures"));
    if(observing && textures && count>0){
        const auto context=nwn_drawing_probe_context();
        // Match only our one recorded texture without retaining/deleting it.
        for(int i=0;i<count;++i)nwn_drawing_probe_texture(textures[i],context,-1);
    }
    if(realDelete)realDelete(count,textures);
}

void nwn_drawing_probe_install_texture_observer() {
    if(!nwn_drawing_probe_active())return;
#ifdef _WIN32
    const bool image=nwn_win_patch_iat("OPENGL32.dll","glTexImage2D",
        reinterpret_cast<void*>(NWN_GL_INTERPOSE(glTexImage2D)),reinterpret_cast<void**>(&realImage));
    const bool deletion=nwn_win_patch_iat("OPENGL32.dll","glDeleteTextures",
        reinterpret_cast<void*>(NWN_GL_INTERPOSE(glDeleteTextures)),reinterpret_cast<void**>(&realDelete));
    // Without lifetime invalidation do not capture a resource that could be
    // deleted and have its numeric GL name reused by an unrelated texture.
    observing=image && deletion;
    if(observing){
        nwn_win_register_gl_wrapper("glTexImage2D",reinterpret_cast<void*>(NWN_GL_INTERPOSE(glTexImage2D)),
                                   reinterpret_cast<void**>(&realImage));
        nwn_win_register_gl_wrapper("glDeleteTextures",reinterpret_cast<void*>(NWN_GL_INTERPOSE(glDeleteTextures)),
                                   reinterpret_cast<void**>(&realDelete));
    }
#else
    realImage=reinterpret_cast<TexImage>(dlsym(RTLD_NEXT,"glTexImage2D"));
    realDelete=reinterpret_cast<Delete>(dlsym(RTLD_NEXT,"glDeleteTextures"));
    observing=realImage && realDelete;
#endif
    std::fprintf(stderr,"[drawing-probe] dedicated image upload observer %s\n",observing?"installed":"unavailable");
}

// Read-only gate-1 measurement. The executable contains the Nuklear GLSL 330
// vertex source with Position, TexCoord and ProjMtx. Resolve those by name and
// query the actual VAO layout; never assume a native vertex struct or VBO offset.
void nwn_drawing_probe_draw(unsigned mode,int count,unsigned type,const void* indices) {
    if(!nwn_drawing_probe_active())return;
    const unsigned texture=nwn_drawing_probe_texture_for_context(nwn_drawing_probe_context());
    if(!texture)return;
    using Location=int(*)(unsigned,const char*);
    using Uniform=void(*)(unsigned,int,float*);
    using UniformInt=void(*)(unsigned,int,int*);
    using Attribute=void(*)(unsigned,unsigned,int*);
    using Pointer=void(*)(unsigned,unsigned,void**);
    using Bind=void(*)(unsigned,unsigned);
    using BufferSize=void(*)(unsigned,unsigned,int*);
    using Read=void(*)(unsigned,std::intptr_t,std::intptr_t,void*);
    static auto get=reinterpret_cast<Get>(dlsym(RTLD_DEFAULT,"glGetIntegerv"));
    static auto attributeLocation=reinterpret_cast<Location>(dlsym(RTLD_DEFAULT,"glGetAttribLocation"));
    static auto uniformLocation=reinterpret_cast<Location>(dlsym(RTLD_DEFAULT,"glGetUniformLocation"));
    static auto uniform=reinterpret_cast<Uniform>(dlsym(RTLD_DEFAULT,"glGetUniformfv"));
    static auto uniformInt=reinterpret_cast<UniformInt>(dlsym(RTLD_DEFAULT,"glGetUniformiv"));
    static auto attribute=reinterpret_cast<Attribute>(dlsym(RTLD_DEFAULT,"glGetVertexAttribiv"));
    static auto pointer=reinterpret_cast<Pointer>(dlsym(RTLD_DEFAULT,"glGetVertexAttribPointerv"));
    static auto bind=reinterpret_cast<Bind>(dlsym(RTLD_DEFAULT,"glBindBuffer"));
    static auto size=reinterpret_cast<BufferSize>(dlsym(RTLD_DEFAULT,"glGetBufferParameteriv"));
    static auto read=reinterpret_cast<Read>(dlsym(RTLD_DEFAULT,"glGetBufferSubData"));
    if(!get || !attributeLocation || !uniformLocation || !uniform || !uniformInt || !attribute ||
       !pointer || !bind || !size || !read)return;
    int currentTexture=0,program=0,fbo=0,viewport[4]{};
    get(0x8069,&currentTexture);
    if(unsigned(currentTexture)!=texture)return;
    get(0x8B8D,&program);get(0x8CA6,&fbo);get(0x0BA2,viewport);
    static unsigned seen=0;
    if(seen++<2){
        std::fprintf(stderr,"[drawing-probe] image_draw mode=%u count=%d type=%u fbo=%d viewport=%d,%d,%d,%d\n",
                     mode,count,type,fbo,viewport[0],viewport[1],viewport[2],viewport[3]);std::fflush(stderr);
    }
    if(program<=0 || fbo!=0 || mode!=4 || count!=6 || (type!=0x1403 && type!=0x1405))return;
    const int pos=attributeLocation(unsigned(program),"Position"),uv=attributeLocation(unsigned(program),"TexCoord"),
              projection=uniformLocation(unsigned(program),"ProjMtx"),sampler=uniformLocation(unsigned(program),"Texture");
    if(pos<0 || uv<0 || projection<0 || sampler<0)return;
    int unit=-1,active=0;uniformInt(unsigned(program),sampler,&unit);get(0x84E0,&active);
    if(unit<0 || active!=0x84C0+unit)return;
    int element=0;get(0x8895,&element);if(element<=0)return;
    int elementSize=0;size(0x8893,0x8764,&elementSize);
    const auto offset=reinterpret_cast<std::uintptr_t>(indices);
    const unsigned indexBytes=type==0x1403?2:4;
    if(elementSize<0 || offset>unsigned(elementSize) || unsigned(elementSize)-offset<6*indexBytes)return;
    unsigned char packed[24]{};read(0x8893,std::intptr_t(offset),6*indexBytes,packed);
    std::array<unsigned,6> ids{};
    for(unsigned i=0;i<6;++i){
        if(indexBytes==2){std::uint16_t id=0;std::memcpy(&id,packed+i*2,2);ids[i]=id;}
        else std::memcpy(&ids[i],packed+i*4,4);
    }
    const auto minmax=std::minmax_element(ids.begin(),ids.end());
    const unsigned first=*minmax.first,last=*minmax.second;
    // A native rectangle has exactly four adjacent vertices. Reject arbitrary
    // geometry before any VBO readback or allocation.
    if(last<first || last-first!=3)return;
    struct Layout {int buffer=0,stride=0;std::uintptr_t offset=0;};
    auto layout=[&](int location,Layout& out){
        int enabled=0,components=0,format=0;
        attribute(unsigned(location),0x8622,&enabled);attribute(unsigned(location),0x8623,&components);
        attribute(unsigned(location),0x8625,&format);attribute(unsigned(location),0x8624,&out.stride);
        attribute(unsigned(location),0x889F,&out.buffer);
        void* value=nullptr;pointer(unsigned(location),0x8645,&value);out.offset=reinterpret_cast<std::uintptr_t>(value);
        return enabled && components==2 && format==0x1406 && out.buffer>0 && out.stride>=8 && out.stride<=256;
    };
    Layout p,t;if(!layout(pos,p) || !layout(uv,t) || p.buffer!=t.buffer || p.stride!=t.stride ||
                  p.offset>std::uintptr_t(std::numeric_limits<int>::max()) ||
                  t.offset>std::uintptr_t(std::numeric_limits<int>::max()))return;
    const std::uint64_t begin=std::uint64_t(first)*unsigned(p.stride)+std::min(p.offset,t.offset);
    const std::uint64_t end=std::uint64_t(last)*unsigned(p.stride)+std::max(p.offset,t.offset)+8;
    if(end<begin || end-begin>1024 || end>std::uint64_t(std::numeric_limits<int>::max()))return;
    int originalArray=0;get(0x8894,&originalArray);bind(0x8892,unsigned(p.buffer));
    int vertexSize=0;size(0x8892,0x8764,&vertexSize);
    if(vertexSize<0 || end>unsigned(vertexSize)){bind(0x8892,unsigned(originalArray));return;}
    unsigned char data[1024]{};read(0x8892,std::intptr_t(begin),std::intptr_t(end-begin),data);
    bind(0x8892,unsigned(originalArray));
    std::array<drawing_probe::CanvasVertex,6> vertices{};
    for(unsigned i=0;i<6;++i){
        const auto at=std::uint64_t(ids[i])*unsigned(p.stride);
        std::memcpy(&vertices[i].x,data+std::size_t(at+p.offset-begin),8);
        std::memcpy(&vertices[i].u,data+std::size_t(at+t.offset-begin),8);
    }
    float matrix[16]{};uniform(unsigned(program),projection,matrix);
    drawing_probe::CanvasRect rectangle;
    if(drawing_probe::canvas_rect(vertices,matrix,viewport,rectangle))
        nwn_drawing_probe_geometry(rectangle.x,rectangle.y,rectangle.w,rectangle.h,rectangle.bottomUp);
}
