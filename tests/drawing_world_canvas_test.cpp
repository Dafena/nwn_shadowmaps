#include "../drawing_world_canvas.h"
#include "../drawing_preview.h"
#include <cassert>
using namespace drawing_probe;
int main() {
    std::vector<unsigned char> base(world_side*world_side*4,31);
    for(unsigned i=0;i<world_side*world_side;++i)base[i*4+3]=177;
    std::vector<unsigned char> ink(1024*1024*4,0);
    assert(world_composite(base,ink,false)==base); // reset restores the exact base
    base[40000]=123;
    assert(world_composite(base,ink,false,true)==base); // never double-flip the exported artwork
    ink[0]=80;ink[1]=225;ink[2]=255;ink[3]=255;
    auto composed=world_composite(base,ink,false);
    const unsigned at=(world_y*world_side+world_x)*4;
    assert(composed[at]==80 && composed[at+1]==225 && composed[at+2]==255 && composed[at+3]==177);
    for(unsigned y=0;y<world_side;++y)for(unsigned x=0;x<world_side;++x){
        if(x>=world_x && x<world_x+world_w && y>=world_y && y<world_y+world_h)continue;
        for(unsigned c=0;c<4;++c)assert(composed[(y*world_side+x)*4+c]==base[(y*world_side+x)*4+c]);
    }
    auto flipped=world_composite(base,ink,true);
    const unsigned reverse=((world_side-1-world_y)*world_side+world_x)*4;
    assert(flipped[reverse]==80 && flipped[at]==31);
    auto mirrored=world_composite(base,ink,false,true);
    const unsigned mirrorAt=(world_y*world_side+world_side-1-world_x)*4;
    assert(mirrored[mirrorAt]==80 && mirrored[mirrorAt+3]==177);
    assert(mirrored[at]==31); // no ghost at the former uncorrected position
    assert(world_composite({},ink,false).empty());
    assert(world_marker(base.data())==-1);
    for(unsigned bottom=0;bottom<2;++bottom)for(unsigned i=0;i<16;++i){
        const auto index=((bottom?world_side-1:0)*world_side+i)*4;
        for(unsigned c=0;c<3;++c)base[index+c]=static_cast<unsigned char>(marker_channel(i,c,bottom)+113);
        base[index+3]=255;
    }
    assert(world_marker(base.data())==0);
    for(unsigned i=0;i<world_side*4;++i)std::swap(base[i],base[(world_side-1)*world_side*4+i]);
    assert(world_marker(base.data())==1);
    base[5]^=1;assert(world_marker(base.data())==-1);
    Preview preview;preview.scale(1);Message open;open.kind='O';open.sequence=1;
    preview.native(open,0);
    auto transparent=preview.raster(false,true);
    for(unsigned i=0;i<1024*1024;++i)assert(transparent[i*4+3]==0);
}
