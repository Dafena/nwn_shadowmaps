#pragma once
#include <cstddef>

namespace drawing_probe {
// Dedicated test-resource signature, not a secret or an authentication token.
// Check opposite rows so a generic 1024 image cannot be admitted by dimensions.
inline unsigned char marker_channel(unsigned pixel,unsigned channel,bool bottom) {
    const unsigned multiplier[]{37,53,89},offset[]{19,71,139};
    return static_cast<unsigned char>(pixel*multiplier[channel]+offset[channel]+(bottom?77:0));
}
inline bool marker_row(const unsigned char* data,bool bottom,bool bgra) {
    for(unsigned i=0;i<16;++i){
        for(unsigned c=0;c<3;++c)
            if(data[i*4+(bgra?2-c:c)]!=marker_channel(i,c,bottom))return false;
        if(data[i*4+3]!=255)return false;
    }
    return true;
}
// -1 mismatch, 0 top-down source, 1 bottom-up source. Call only for a verified
// 1024x1024 byte RGBA/BGRA CPU upload with contiguous rows and no bound PBO.
inline int pad_marker(const void* pixels,bool bgra) {
    if(!pixels)return -1;
    auto data=static_cast<const unsigned char*>(pixels);
    auto last=data+1024*1023*4;
    if(marker_row(data,false,bgra) && marker_row(last,true,bgra))return 0;
    if(marker_row(data,true,bgra) && marker_row(last,false,bgra))return 1;
    return -1;
}
}
