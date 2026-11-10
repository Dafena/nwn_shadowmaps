#include "../drawing_preview.h"
#include "../drawing_texture_upload.h"
#include "../drawing_pad_marker.h"
#include <cassert>
#include <map>
#include <string>
using namespace drawing_probe;
static Message message(char kind,unsigned seq,float x=0,float y=0,float w=400,float h=300) {
    Message m;std::fill_n(m.nonce.begin(),64,'a');m.kind=kind;m.sequence=seq;
    m.x=x;m.y=y;m.w=w;m.h=h;return m;
}
static void click(Preview& p,unsigned seq,std::uint64_t time,Point raw,Point local,
                  CanvasRect rectangle={100,200,600,450}) {
    p.image(rectangle,time);
    p.input(1,raw,time);p.input(2,raw,time+10000);
    assert(p.native(message('D',seq,local.x,local.y),time+20000));
}

static std::map<unsigned,int> state;
static std::map<int,int> bindings;
static unsigned writes=0,mips=0;static bool valid=true;static int width=1024,format=0x8058;
static void get(unsigned n,int* p){*p=n==0x8069?bindings[state[0x84E0]]:state[n];}
static void active(unsigned n){state[0x84E0]=int(n);}
static void bind(unsigned,unsigned n){bindings[state[0x84E0]]=int(n);}
static unsigned char isTexture(unsigned){return valid;}
static void level(unsigned,int,unsigned n,int* p){
    *p=n==0x1000?width:n==0x1001?1024:n==0x1003?format:0;
}
static void parameter(unsigned,unsigned,int* p){*p=0x2703;}
static void store(unsigned n,int v){state[n]=v;}
static void buffer(unsigned,unsigned n){state[0x88EF]=int(n);}
static void sub(unsigned,int,int,int,int w,int h,unsigned,unsigned,const void*) {
    assert(w==1024 && h==1024 && state[0x88EF]==0 && state[0x0CF5]==1 && state[0x0CF2]==0 &&
           state[0x0CF3]==0 && state[0x0CF4]==0 && bindings[state[0x84E0]]==99);++writes;
}
static void mip(unsigned){++mips;}
int main() {
    Preview p;p.scale(1.5f);p.native(message('O',1),0);
    p.native(message('U',2,10,10),100);assert(p.stroke_count()==0);
    click(p,3,1000000,{250,350},{150,150});assert(p.calibrated && p.stroke_count()==1);
    // Continuous raw samples before and after the native acknowledgement.
    p.image({100,200,600,450},2000000);
    p.input(1,{250,350},2000000);p.input(0,{265,365},2010000);
    p.native(message('D',4,150,150),2020000);
    p.input(0,{280,380},2030000);p.input(2,{295,395},2040000);
    assert(p.stroke_count()==2 && p.point_count()==5);
    auto pixels=p.raster();assert(pixels.size()==1024*1024*4);
    assert(pixels[((1023-341)*1024+256)*4]==80); // 100x100 logical at GUI scale 1.5
    assert(pixels[(1023*1024)*4]==12); // top-left remains the blank background
    auto topDown=p.raster(false);
    assert(topDown[(341*1024+256)*4]==80);
    unsigned cyan=0;for(unsigned i=0;i<1024*1024;++i)cyan+=pixels[i*4]==80;
    assert(cyan>300);
    // No ink on a cover down or a stray native up; focus cancels ownership.
    p.input(1,{250,350},3000000);p.native(message('B',5),3010000);p.input(2,{280,380},3020000);
    p.native(message('U',6),3030000);assert(p.stroke_count()==2);
    p.image({100,200,600,450},4000000);
    p.input(1,{250,350},4000000);p.native(message('D',7,150,150),4010000);
    p.input(3,{},4020000);p.input(2,{280,380},4030000);assert(!p.calibrated && p.stroke_count()==2);
    // GUI changes require a fresh image measurement; old geometry cannot align ink.
    p.scale(1);p.input(1,{250,350},5000000);p.input(0,{290,390},5010000);
    p.native(message('D',8,150,150),5020000);p.input(2,{290,390},5030000);
    assert(!p.calibrated && p.stroke_count()==2);
    click(p,9,6000000,{250,350},{150,150});assert(p.calibrated);
    p.native(message('G',10,80,80,440,450),6020000);assert(!p.calibrated);
    auto wrong=message('D',11);wrong.nonce[0]='b';assert(!p.native(wrong,6100000));
    assert(!p.native(message('U',10),6100000)); // replay
    // Ambiguous fast downs cannot borrow an older canvas event.
    p.input(1,{250,350},7000000);p.input(2,{250,350},7010000);
    p.input(1,{250,350},7020000);p.native(message('D',11,150,150),7030000);
    assert(p.stroke_count()==3);
    p.native(message('C',12),8000000);assert(!p.active);
    auto reopened=message('O',1);reopened.nonce[0]='c';p.native(reopened,9000000);
    assert(p.stroke_count()==0 && !p.full);
    // Proper segment clipping, including parallel misses and crossings.
    Point a{-10,-10},b{-5,200};assert(!Preview::clip(a,b));
    a={-10,150};b={410,150};assert(Preview::clip(a,b) && a.x==0 && b.x==400);
    a={200,-10};b={200,400};assert(Preview::clip(a,b) && a.y==0 && b.y==300);
    // Filling the diagnostic board refuses new work without deleting old marks.
    Preview cap;cap.scale(1);cap.native(message('O',1),0);
    for(unsigned i=0;i<Preview::max_strokes;++i)click(cap,i+2,1000000ULL*(i+1),{100,100},{50,50},{0,0,400,300});
    assert(cap.stroke_count()==Preview::max_strokes);
    click(cap,200,200000000,{100,100},{50,50},{0,0,400,300});assert(cap.full && cap.stroke_count()==Preview::max_strokes);
    // Oversized single path is rejected before rasterizing expensive work.
    Preview longPath;longPath.scale(1);longPath.native(message('O',1),0);
    click(longPath,2,1000000,{100,100},{50,50},{0,0,400,300});
    longPath.image({0,0,400,300},2000000);
    longPath.input(1,{100,100},2000000);longPath.native(message('D',3,50,50),2010000);
    for(unsigned i=0;i<100;++i)longPath.input(0,{i%2?450.f:50.f,150},2020000+i*10000);
    assert(longPath.full && longPath.stroke_count()==1);

    // Native quad projection is the authority for placement, including UV flips.
    std::array<CanvasVertex,6> quad{{{100,200,0,0},{500,200,1,0},{500,500,1,1},
                                  {100,200,0,0},{500,500,1,1},{100,500,0,1}}};
    const float projection[]{2.f/1920,0,0,0,0,-2.f/1080,0,0,0,0,-1,0,-1,1,0,1};
    const int viewport[]{0,0,1920,1080};CanvasRect rect;
    assert(canvas_rect(quad,projection,viewport,rect));
    assert(std::fabs(rect.x-100)<0.001 && std::fabs(rect.y-200)<0.001 &&
           std::fabs(rect.w-400)<0.001 && std::fabs(rect.h-300)<0.001 && !rect.bottomUp);
    for(auto& v:quad)v.v=1-v.v;
    assert(canvas_rect(quad,projection,viewport,rect) && rect.bottomUp);
    quad[0].u=0.4f;assert(!canvas_rect(quad,projection,viewport,rect));
    // Without recent measured geometry even a native down is refused.
    Preview stale;stale.scale(1);stale.native(message('O',1),0);stale.image({0,0,400,300},1);
    stale.input(1,{100,100},1000000);stale.native(message('D',2,100,100),1010000);
    stale.input(2,{100,100},1020000);assert(stale.stroke_count()==0);
    Preview paused;paused.scale(1);paused.native(message('O',1),0);
    click(paused,2,1000000,{100,100},{50,50},{0,0,400,300});
    paused.image({0,0,400,300},2000000);paused.input(1,{100,100},2000000);
    paused.native(message('D',3,100,100),2010000);paused.input(0,{200,200},2020000);
    paused.pause();paused.input(2,{300,200},2030000);
    assert(paused.full && paused.stroke_count()==1 && paused.active);
    click(paused,4,3000000,{100,100},{50,50},{0,0,400,300});
    assert(paused.stroke_count()==1); // no misleading endpoint-only replacement

    UploadApi gl{get,active,bind,isTexture,level,parameter,store,buffer,sub,mip};
    state={{0x84E0,0x84C5},{0x88EF,21},{0x0CF5,8},{0x0CF2,500},{0x0CF3,7},{0x0CF4,11}};
    bindings={{0x84C0,43},{0x84C5,88}};
    const auto original=state;
    const auto originalBindings=bindings;
    assert(upload(gl,99,pixels.data()) && writes==1 && mips==1);
    assert(state==original && bindings==originalBindings);

    // Dedicated resource identification, including STBI's row/channel order.
    std::vector<unsigned char> marker(1024*1024*4,0);
    for(unsigned bottom=0;bottom<2;++bottom)for(unsigned i=0;i<16;++i){
        const unsigned at=((bottom?1023:0)*1024+i)*4;
        for(unsigned c=0;c<3;++c)marker[at+c]=marker_channel(i,c,bottom);
        marker[at+3]=255;
    }
    assert(pad_marker(marker.data(),false)==0);
    assert(pad_marker(marker.data(),true)==-1);
    for(unsigned i=0;i<1024*4;++i)std::swap(marker[i],marker[(1023*1024*4)+i]);
    assert(pad_marker(marker.data(),false)==1);
    marker[6]^=1;assert(pad_marker(marker.data(),false)==-1);
    width=512;assert(!upload(gl,99,pixels.data()));
    assert(state==original && bindings==originalBindings && writes==1);
    width=1024;format=0x83F3;assert(!upload(gl,99,pixels.data()));
    assert(state==original && bindings==originalBindings && writes==1);
    valid=false;assert(!upload(gl,99,pixels.data()));
    assert(state==original && bindings==originalBindings);
}
