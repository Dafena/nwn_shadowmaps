#pragma once

namespace drawing_probe {
// All entry points are driver functions, resolved only with a current context.
// No engine object offsets, texture creation/deletion or engine binding-cache changes.
struct UploadApi {
    void (*get)(unsigned,int*);
    void (*active)(unsigned);
    void (*bind)(unsigned,unsigned);
    unsigned char (*isTexture)(unsigned);
    void (*level)(unsigned,int,unsigned,int*);
    void (*parameter)(unsigned,unsigned,int*);
    void (*pixelStore)(unsigned,int);
    void (*bindBuffer)(unsigned,unsigned);
    void (*subImage)(unsigned,int,int,int,int,int,unsigned,unsigned,const void*);
    void (*mipmap)(unsigned);
};

inline bool upload(const UploadApi& gl,unsigned texture,const unsigned char* rgba,int side=1024) {
    if(side!=1024 && side!=2048)return false;
    if(!texture || !rgba || !gl.get || !gl.active || !gl.bind || !gl.isTexture || !gl.level ||
       !gl.parameter || !gl.pixelStore || !gl.bindBuffer || !gl.subImage || !gl.mipmap ||
       !gl.isTexture(texture))return false;
    constexpr unsigned target=0x0DE1;
    int active=0,bound=0;
    gl.get(0x84E0,&active);gl.active(0x84C0);gl.get(0x8069,&bound);
    gl.bind(target,texture);
    int w=0,h=0,format=0,compressed=0,filter=0;
    gl.level(target,0,0x1000,&w);gl.level(target,0,0x1001,&h);
    gl.level(target,0,0x1003,&format);gl.level(target,0,0x86A1,&compressed);
    gl.parameter(target,0x2801,&filter);
    if(w!=side || h!=side || (format!=0x8058 && format!=0x1908 && format!=0x8051 && format!=0x1907) || compressed){
        gl.bind(target,unsigned(bound));gl.active(unsigned(active));return false;
    }
    int pbo=0,alignment=0,row=0,skipRows=0,skipPixels=0;
    gl.get(0x88EF,&pbo);gl.get(0x0CF5,&alignment);gl.get(0x0CF2,&row);
    gl.get(0x0CF3,&skipRows);gl.get(0x0CF4,&skipPixels);
    gl.bindBuffer(0x88EC,0);gl.pixelStore(0x0CF5,1);gl.pixelStore(0x0CF2,0);
    gl.pixelStore(0x0CF3,0);gl.pixelStore(0x0CF4,0);
    gl.subImage(target,0,0,0,side,side,0x1908,0x1401,rgba);
    if(filter>=0x2700 && filter<=0x2703)gl.mipmap(target);
    gl.pixelStore(0x0CF5,alignment);gl.pixelStore(0x0CF2,row);
    gl.pixelStore(0x0CF3,skipRows);gl.pixelStore(0x0CF4,skipPixels);
    gl.bindBuffer(0x88EC,unsigned(pbo));gl.bind(target,unsigned(bound));gl.active(unsigned(active));
    return true;
}
}
