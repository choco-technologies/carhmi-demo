#include "dmod.h"
#include "libdmview.h"
#include "dmosi.h"
#include <string.h>
#include "literki_image_audit.inc"

/* Verify exact on-card bytes AND actual rendered pixels, not just properties.
 * A magenta background makes a missing/blank image measurable. */
int main(int argc,char** argv)
{
    if(argc<3)return 1;
    int status=0;
    libdmview_t view=libdmview_open(argv[1],&status);
    if(!view){Dmod_Printf("FAIL image probe open: %d\n",status);return 1;}
    libdmview_surface_t surface;
    memset(&surface,0,sizeof(surface));
    surface.width=surface.height=128;surface.stride=256;surface.format=DMDRVI_GFX_PIXEL_FORMAT_RGB565;
    surface.pixels=Dmod_Malloc(32768);
    unsigned failures=0,checked=0,max_wait=0;
    uint8_t* data=Dmod_Malloc(512);
    if(!surface.pixels||!data)return 1;
    libdmview_render(view,&surface,NULL);
    for(unsigned i=0;i<sizeof(pictures)/sizeof(pictures[0]);i++){
        char path[192];Dmod_SnPrintf(path,sizeof(path),"%s/%s",argv[2],pictures[i].name);
        void* file=Dmod_FileOpen(path,"rb");
        uint32_t hash=2166136261u;unsigned bytes=0;
        if(file){
            size_t n;
            while((n=Dmod_FileRead(data,1,512,file))>0){
                for(size_t j=0;j<n;j++)hash=(hash^data[j])*16777619u;
                bytes+=(unsigned)n;
            }
            Dmod_FileClose(file);
        }
        if(!file||bytes!=pictures[i].size||hash!=pictures[i].hash){
            Dmod_Printf("FAIL file %s: bytes=%u hash=%08x expected=%u/%08x\n",pictures[i].name,bytes,(unsigned)hash,pictures[i].size,pictures[i].hash);
            failures++;continue;
        }
        libdmview_set_string(view,"picture",path);
        Dmod_Timestamp_t start=Dmod_GetUptime();
        unsigned colored=0;
        do{
            libdmview_update(view,(uint32_t)Dmod_GetUptime());
            libdmview_render(view,&surface,NULL);
            colored=0;
            const uint16_t* px=surface.pixels;
            for(unsigned p=0;p<128u*128u;p++){
                uint16_t c=px[p];
                if(c!=0xf81f && ((c>>11)<24 || ((c>>5)&63)<48 || (c&31)<24))colored++;
            }
            if(colored<10)Dmod_ThreadSleep(10);
        }while(colored<10&&Dmod_GetUptime()-start<10000);
        unsigned waited=(unsigned)(Dmod_GetUptime()-start);
        if(waited>max_wait)max_wait=waited;
        if(waited>500)Dmod_Printf("IMAGE_AUDIT slow %s: %u ms\n",pictures[i].name,waited);
        if(colored<10){
            Dmod_Printf("FAIL rendered image %s: %u colored pixels (loader stalled/missing image)\n",pictures[i].name,colored);
            failures++;break;
        }
        checked++;
        if(checked%27==0)Dmod_Printf("IMAGE_AUDIT %u/%u passed\n",checked,(unsigned)(sizeof(pictures)/sizeof(pictures[0])));
    }
    Dmod_Free(data);libdmview_close(view);Dmod_Free(surface.pixels);
    Dmod_Printf("IMAGE_AUDIT maximum load wait: %u ms\n",max_wait);
    Dmod_Printf("%s image audit: %u/%u rendered, %u failures\n",failures?"FAIL":"PASS",checked,(unsigned)(sizeof(pictures)/sizeof(pictures[0])),failures);
    return failures?1:0;
}
