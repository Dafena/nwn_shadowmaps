#pragma once
#include "drawing_probe.h"
#include "drawing_canvas_quad.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace drawing_probe {
struct Point { float x=0,y=0; };

// Gate-1 local ink only. Native D is still acknowledged through the server;
// buffering hides lost samples, not that round trip. No shared board writes.
class Preview {
public:
    static constexpr unsigned side=1024, max_strokes=128, max_points=8192,
                              max_gesture_points=1024, max_raster_steps=8192;
    static constexpr std::uint64_t acknowledgement_us=250000;
    bool active=false, calibrated=false, full=false;
    std::uint64_t revision=0;
    unsigned rejected=0;

    void scale(float value) {
        if(!std::isfinite(value) || value<=0 || value>16) value=0;
        if(value!=guiScale){guiScale=value;invalidate();}
    }
    void close() {active=false;invalidate();}
    // Preserve committed ink if diagnostics run out of movement samples. Do not
    // turn later unsampled gestures into misleading straight endpoint lines.
    void pause() {full=true;cancel();}
    void image(const CanvasRect& next,std::uint64_t time) {
        if(!active)return;
        if(!calibrated || next.x!=rectangle.x || next.y!=rectangle.y ||
           next.w!=rectangle.w || next.h!=rectangle.h){cancel();rectangle=next;}
        calibrated=true;imageTime=time;
    }

    bool native(const Message& m,std::uint64_t time) {
        if(m.kind=='O') {
            if(m.sequence!=1 || (active && nonce==m.nonce))return false;
            nonce=m.nonce;sequence=1;active=true;strokes.clear();points=steps=0;
            full=false;rejected=0;lastDown=ambiguousUntil=0;haveDown=false;
            geometry={};invalidate();++revision;return true;
        }
        if(!active || nonce!=m.nonce || m.sequence<=sequence)return false;
        sequence=m.sequence;
        if(m.kind=='C'){close();return true;}
        if(m.kind=='G') {
            std::array<float,4> next{m.x,m.y,m.w,m.h};
            if(next!=geometry){geometry=next;invalidate();}
            return true;
        }
        if(m.kind=='B'){cancel();return true;}
        // In particular, an unowned native U must never create a mark.
        if(m.kind!='D')return true;
        if(!pending || owned || time<downTime || time-downTime>acknowledgement_us ||
           time<ambiguousUntil || guiScale<=0 || !calibrated || time<imageTime ||
           time-imageTime>250000 || m.w!=400 || m.h!=300 ||
           m.x<0 || m.y<0 || m.x>m.w*guiScale || m.y>m.h*guiScale) {
            ++rejected;cancel();return true;
        }
        // Native down owns the gesture; its widget-relative position has a
        // different inset from draw-list images. Use the rendered quad instead.
        if(raw[0].x<rectangle.x || raw[0].x>rectangle.x+rectangle.w ||
           raw[0].y<rectangle.y || raw[0].y>rectangle.y+rectangle.h){
            ++rejected;cancel();return true;
        }
        if(!admit())return true;
        owned=true;++revision;
        if(ended)commit();
        return true;
    }

    void input(int kind,Point p,std::uint64_t time) {
        if(!active || !std::isfinite(p.x) || !std::isfinite(p.y))return;
        if(kind==3){invalidate();return;}
        if(kind==1) {
            cancel();
            if(haveDown && (time<lastDown || time-lastDown<acknowledgement_us)) {
                ambiguousUntil=time+acknowledgement_us;lastDown=time;++rejected;return;
            }
            haveDown=true;lastDown=time;
            if(time<ambiguousUntil || full)return;
            pending=true;downTime=time;raw.push_back(p);return;
        }
        if(!pending || ended || (kind!=0 && kind!=2))return;
        if(raw.size()>=max_gesture_points){++rejected;cancel();return;}
        if(p.x!=raw.back().x || p.y!=raw.back().y)raw.push_back(p);
        if(owned && !admit())return;
        if(owned)++revision;
        if(kind==2){ended=true;if(owned)commit();}
    }

    unsigned stroke_count() const{return static_cast<unsigned>(strokes.size());}
    unsigned point_count() const{return points;}

    // RGBA row order follows the observed native image upload. NUI Stretch
    // logical 400x300 retains uniform logical pen thickness on the square atlas.
    std::vector<unsigned char> raster(bool bottomUp=true,bool transparent=false) const {
        std::vector<unsigned char> result(side*side*4);
        for(unsigned i=0;i<side*side;++i){
            result[i*4]=12;result[i*4+1]=20;result[i*4+2]=28;result[i*4+3]=transparent?0:255;
        }
        auto draw=[&](const std::vector<Point>& path) {
            if(path.empty())return;
            for(std::size_t i=0;i<path.size();++i) {
                Point a=path[i?i-1:0],b=path[i];
                if(!clip(a,b))continue;
                const float dx=b.x-a.x,dy=b.y-a.y;
                const unsigned steps=std::max(1u,unsigned(std::ceil(std::max(std::fabs(dx)*side/400,
                                                                        std::fabs(dy)*side/300))));
                for(unsigned step=0;step<=steps;++step) {
                    float t=float(step)/steps;
                    const float x=(a.x+dx*t)*side/400, y=(a.y+dy*t)*side/300;
                    for(int yy=std::max(0,int(y)-6);yy<=std::min(int(side)-1,int(y)+6);++yy)
                        for(int xx=std::max(0,int(x)-5);xx<=std::min(int(side)-1,int(x)+5);++xx) {
                            float lx=(xx+0.5f-x)*400/side,ly=(yy+0.5f-y)*300/side;
                            if(lx*lx+ly*ly>1.75f*1.75f)continue;
                            unsigned at=((bottomUp?side-1-yy:yy)*side+xx)*4;
                            result[at]=80;result[at+1]=225;result[at+2]=255;
                            result[at+3]=255;
                        }
                }
            }
        };
        for(const auto& s:strokes)draw(s);
        if(owned)draw(logical());
        return result;
    }

    static bool clip(Point& a,Point& b) {
        // Liang-Barsky: outside segments never turn into false border strokes.
        const float dx=b.x-a.x,dy=b.y-a.y;
        float enter=0,leave=1;
        const float p[]{-dx,dx,-dy,dy},q[]{a.x,400-a.x,a.y,300-a.y};
        for(unsigned i=0;i<4;++i){
            if(p[i]==0){if(q[i]<0)return false;continue;}
            float r=q[i]/p[i];
            if(p[i]<0)enter=std::max(enter,r);else leave=std::min(leave,r);
            if(enter>leave)return false;
        }
        Point start=a;a={start.x+enter*dx,start.y+enter*dy};
        b={start.x+leave*dx,start.y+leave*dy};return true;
    }
private:
    std::array<char,65> nonce{};
    std::array<float,4> geometry{};
    unsigned sequence=0,points=0,steps=0;
    float guiScale=0;
    CanvasRect rectangle{};
    std::uint64_t imageTime=0;
    std::uint64_t downTime=0,lastDown=0,ambiguousUntil=0;
    bool pending=false,owned=false,ended=false,haveDown=false;
    std::vector<Point> raw;
    std::vector<std::vector<Point>> strokes;
    void cancel(){if(owned)++revision;pending=owned=ended=false;raw.clear();}
    void invalidate(){cancel();calibrated=false;}
    std::vector<Point> logical() const {
        std::vector<Point> out;out.reserve(raw.size());
        for(auto p:raw)out.push_back({(p.x-rectangle.x)*400/rectangle.w,
                                    (p.y-rectangle.y)*300/rectangle.h});
        return out;
    }
    static unsigned cost(const std::vector<Point>& path) {
        unsigned total=0;
        for(std::size_t i=0;i<path.size();++i){
            Point a=path[i?i-1:0],b=path[i];
            if(!clip(a,b))continue;
            total+=std::max(1u,unsigned(std::ceil(std::max(std::fabs(b.x-a.x)*side/400,
                                                         std::fabs(b.y-a.y)*side/300))))+1;
            if(total>max_raster_steps)return total;
        }
        return total;
    }
    bool admit() {
        if(strokes.size()>=max_strokes || points+raw.size()>max_points ||
           steps+cost(logical())>max_raster_steps){full=true;cancel();return false;}
        return true;
    }
    void commit() {
        if(!admit())return;
        steps+=cost(logical());
        points+=static_cast<unsigned>(raw.size());strokes.push_back(logical());
        cancel();++revision;
    }
};
}
