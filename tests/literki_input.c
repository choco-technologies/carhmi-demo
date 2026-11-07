#include "dmod.h"
#include "libdmview.h"
#include "games.h"
#include "letters.h"
#include "lesson_audio.h"
#include <string.h>

/* Actual controller and renderer; only the physical audio/music endpoints are
 * replaced so the host/board test never plays music or changes an audio device. */
static bool music_playing = true, siren = true, sound_busy;
static unsigned pauses, rewards;
static char spoken[64];
void player_state(player_t p, player_state_t* s) { (void)p; memset(s,0,sizeof(*s)); s->playing=music_playing; }
void player_toggle(player_t p) { (void)p; music_playing=!music_playing; pauses++; }
bool sfx_siren_wanted(sfx_t s) { (void)s; return siren; }
void sfx_siren_set(sfx_t s, bool on) { (void)s; siren=on; }
void sfx_engine_set(sfx_t s, bool on) { (void)s; (void)on; }
void sfx_pump_set(sfx_t s, bool on) { (void)s; (void)on; }
lesson_audio_t lesson_audio_new(const char* directory) { (void)directory; return (lesson_audio_t)1; }
void lesson_audio_play(lesson_audio_t a, const char* file, bool reward) { (void)a; strcpy(spoken,file); if(reward)rewards++; }
void lesson_audio_cancel(lesson_audio_t a) { (void)a; spoken[0]=0; }
bool lesson_audio_busy(lesson_audio_t a) { (void)a; return sound_busy; }
void lesson_audio_free(lesson_audio_t a) { (void)a; }

static libdmview_t view;
static libdmview_surface_t surface;
static games_t game;
static uint32_t claim, now;
static int failures;
static unsigned feedback_checks;
static bool compact;
static void check(bool ok, const char* what) { if(!ok){Dmod_Printf("FAIL %s\n",what); failures++;} }
static int value(const char* property)
{
    int32_t v=0;
    int result=libdmview_prop_get_int(claim,property,&v);
    check(result==0,property);
    return v;
}
static void text(const char* property,char out[128])
{
    out[0]=0;
    check(libdmview_prop_get_string(claim,property,out,128)==0,property);
}
static void step(void)
{
    libdmview_update(view,now);
    libdmview_event_t event;
    while(libdmview_wait_event(claim,&event,0)==0) games_event(game,&event,now);
    games_update(game,now);
    libdmview_update(view,now);
    check(libdmview_render(view,&surface,NULL)>=0,"render");
}
static uint32_t region_hash(int x,int y,int width,int height)
{
    uint32_t hash=2166136261u;
    for(int row=y;row<y+height;row++){
        const uint16_t* pixels=(const uint16_t*)((const uint8_t*)surface.pixels+row*surface.stride);
        for(int column=x;column<x+width;column++)hash=(hash^pixels[column])*16777619u;
    }
    return hash;
}
static void click_region(int x,int y,int left,int top,int width,int height)
{
    uint32_t before=region_hash(left,top,width,height);
    int round=value("game.round"),active=value("game.active");
    dmdrvi_input_state_t input;
    memset(&input,0,sizeof(input));
    input.contact_count=1;input.contacts[0].x=x;input.contacts[0].y=y;input.contacts[0].event=DMDRVI_INPUT_CONTACT_DOWN;
    now+=20;check(libdmview_input(view,&input,now)>=0,"input down");step();
    bool changed=before!=region_hash(left,top,width,height);
    if(!changed)Dmod_Printf("FAIL unchanged pressed button at %d,%d\n",x,y);
    check(changed,"button visibly changes before touch release");
    check(value("game.round")==round&&value("game.active")==active,"touch down does not activate the button");
    feedback_checks++;
    input.contact_count=0;now+=20;check(libdmview_input(view,&input,now)>=0,"input up");step();
}
static void click(int x,int y) { click_region(x,y,x-8,y-8,16,16); }
static void choose(int mode,unsigned option)
{
    int sidebar=compact?40:50;
    int x,y,left,top,width,height;
    if(mode==LETTERS_INITIAL){
        x=sidebar+(compact?143:214)+(int)option*(compact?52:81);y=compact?111:150;
        left=sidebar+(compact?120:179)+(int)option*(compact?52:81);top=compact?101:106;
        width=compact?47:70;height=compact?70:88;
    }else if(mode==LETTERS_SPELL){
        x=sidebar+(compact?22:36)+(int)(option%8)*(compact?33:51);
        y=(compact?183:192)+(int)(option/8)*(compact?35:44);
        left=sidebar+(compact?8:14)+(int)(option%8)*(compact?33:51);
        top=(compact?168:173)+(int)(option/8)*(compact?35:44);
        width=compact?29:44;height=compact?31:38;
    }else{
        x=sidebar+(compact?49:76)+(int)option*(compact?91:139);y=compact?161:176;
        left=sidebar+(compact?8:16)+(int)option*(compact?91:139);top=compact?119:124;
        width=compact?82:120;height=compact?86:104;
    }
    click_region(x,y,left,top,width,height);
}
/* Inspect only the interior of each picture, excluding frames and labels.
 * White cards and the cream page must not make a missing image pass. */
static bool pictures_visible(int mode)
{
    unsigned count=(mode==LETTERS_INITIAL||mode==LETTERS_SPELL)?1:3;
    for(unsigned option=0;option<count;option++){
        int x,y,size;
        if(mode==LETTERS_INITIAL){x=compact?54:74;y=compact?90:83;size=compact?92:128;}
        else if(mode==LETTERS_SPELL){x=compact?50:69;y=70;size=compact?68:84;}
        else{x=(compact?52:80)+(int)option*(compact?91:139);y=compact?125:130;size=compact?74:92;}
        unsigned colored=0;
        for(int row=y;row<y+size;row++){
            /* The centered loading caption is not evidence of an image. */
            if(row>=y+size/2-8&&row<y+size/2+8)continue;
            const uint16_t* pixels=(const uint16_t*)((const uint8_t*)surface.pixels+row*surface.stride);
            for(int column=x;column<x+size;column++){
                uint16_t pixel=pixels[column];
                if((pixel>>11)<24||((pixel>>5)&63)<48||(pixel&31)<24)colored++;
            }
        }
        if(colored<10)return false;
    }
    return true;
}
static void save_frame(const char* prefix,const char* name)
{
    if(!prefix)return;
    char path[256],head[64];
    Dmod_SnPrintf(path,sizeof(path),"%s-%s.ppm",prefix,name);
    void* file=Dmod_FileOpen(path,"wb");
    if(!file){check(false,"screenshot open");return;}
    int n=Dmod_SnPrintf(head,sizeof(head),"P6\n%u %u\n255\n",surface.width,surface.height);
    Dmod_FileWrite(head,1,(size_t)n,file);
    uint8_t* row=Dmod_Malloc(surface.width*3u);
    if(row){
        for(unsigned y=0;y<surface.height;y++){
            const uint16_t* pixels=(const uint16_t*)((const uint8_t*)surface.pixels+y*surface.stride);
            for(unsigned x=0;x<surface.width;x++){
                uint16_t c=pixels[x];row[x*3]=((c>>11)&31)*255/31;row[x*3+1]=((c>>5)&63)*255/63;row[x*3+2]=(c&31)*255/31;
            }
            Dmod_FileWrite(row,3,surface.width,file);
        }
        Dmod_Free(row);
    }
    Dmod_FileClose(file);
}
static void cancel_press(const char* preview)
{
    int left=compact?277:437,top=28;
    uint32_t before=region_hash(left,top,36,30);
    char previous_spoken[64];strcpy(previous_spoken,spoken);
    dmdrvi_input_state_t input;
    memset(&input,0,sizeof(input));input.contact_count=1;
    input.contacts[0].x=left+18;input.contacts[0].y=top+15;input.contacts[0].event=DMDRVI_INPUT_CONTACT_DOWN;
    now+=20;libdmview_input(view,&input,now);step();
    check(before!=region_hash(left,top,36,30),"repeat highlights while held");
    save_frame(preview,"pressed-repeat");
    input.contacts[0].x=1;input.contacts[0].y=1;input.contacts[0].event=DMDRVI_INPUT_CONTACT_MOVE;
    now+=20;libdmview_input(view,&input,now);step();
    input.contact_count=0;now+=20;libdmview_input(view,&input,now);step();
    check(before==region_hash(left,top,36,30),"cancelled press restores the button");
    check(!strcmp(spoken,previous_spoken),"dragging off does not activate repeat");
    save_frame(preview,"released-repeat");
}
static void loading_placeholder(int mode,const char* source,const char* preview)
{
    const char* variable=mode==LETTERS_INITIAL?"game_object_i0":"game_spell_image_i4";
    check(libdmview_set_string(view,variable,"")==0,"clear image source");
    libdmview_invalidate(view); /* Inspect the complete empty-image state. */
    libdmview_render(view,&surface,NULL);
    if(pictures_visible(mode)){Dmod_Printf("FAIL image clear in mode %d\n",mode);save_frame(preview,"loading-failure");}
    check(!pictures_visible(mode),"loading caption cannot pass the image pixel check");
    char frame[24];Dmod_SnPrintf(frame,sizeof(frame),"loading-%d",mode);save_frame(preview,frame);
    int x,y,size;
    if(mode==LETTERS_INITIAL){x=compact?54:74;y=compact?90:83;size=compact?92:128;}
    else{x=compact?50:69;y=70;size=compact?68:84;}
    unsigned colored=0;
    for(int row=y+size/2-8;row<y+size/2+8;row++){
        const uint16_t* pixels=(const uint16_t*)((const uint8_t*)surface.pixels+row*surface.stride);
        for(int column=x;column<x+size;column++){
            uint16_t pixel=pixels[column];
            if((pixel>>11)<24||((pixel>>5)&63)<48||(pixel&31)<24)colored++;
        }
    }
    check(colored>=10,"loading caption is visible before the image");
    char path[192];
    if(mode==LETTERS_INITIAL)Dmod_SnPrintf(path,sizeof(path),"%s-%dx%d.dmvi",source,size,size);
    else Dmod_SnPrintf(path,sizeof(path),"%s-%dx%d-k0-0-%dx%d-r10.dmvi",source,size,size,size,size);
    check(libdmview_set_string(view,variable,path)==0,"restore image source");
    libdmview_invalidate(view);
    Dmod_Timestamp_t start=Dmod_GetUptime();
    while(!pictures_visible(mode)&&Dmod_GetUptime()-start<10000){Dmod_ThreadSleep(20);now+=20;step();}
    check(pictures_visible(mode),"restored illustration covers loading caption");
}
int main(int argc,char** argv)
{
    if(argc<2)return 1;
    int status=0;view=libdmview_open(argv[1],&status);
    if(!view){Dmod_Printf("FAIL open %d\n",status);return 1;}
    uint16_t w=0,h=0;libdmview_get_size(view,&w,&h);compact=w==320;
    surface.width=w;surface.height=h;surface.stride=w*2;surface.format=DMDRVI_GFX_PIXEL_FORMAT_RGB565;
    surface.pixels=Dmod_Malloc(surface.stride*h);
    if(!surface.pixels)return 1;
    libdmview_display_t display=libdmview_display_register("carhmi_letters_test");
    check(display!=NULL,"display register");
    check(libdmview_claim("carhmi_letters_test",argv[1],&claim)==0,"claim");
    check(libdmview_attach(view,display)==0,"attach");
    game=games_new(claim,argv[1],(player_t)1,(sfx_t)2);
    check(game!=NULL,"game allocation");step();
    click(20,compact?218:247);
    check(games_is_open(game),"games sidebar click");
    now+=500;step(); /* Sidebar page transition has to finish before input. */
    check(!music_playing&&!siren&&pauses==1,"music and siren paused");
    save_frame(argc>2?argv[2]:NULL,"menu");
    click(compact?222:318,compact?185:200);
    check(value("game.active")==1,"start button");
    unsigned modes=0;
    for(unsigned round_index=0;round_index<8;round_index++){
        int mode=value("game.mode"),round=value("game.round");
        modes|=1u<<mode;
        char source[128];text("game.object",source);
        unsigned word=0;
        while(word<letters_word_count&&strcmp(source+4,letters_words[word].id))word++;
        check(word<letters_word_count,"round word image");if(word==letters_word_count)break;
        for(unsigned settle=0;settle<20;settle++){Dmod_ThreadSleep(10);now+=10;step();}
        Dmod_Timestamp_t image_start=Dmod_GetUptime();
        while(!pictures_visible(mode)&&Dmod_GetUptime()-image_start<10000){
            Dmod_ThreadSleep(20);now+=20;step();
        }
        check(pictures_visible(mode),"all task pictures contain rendered image pixels");
        if(round_index==0)cancel_press(argc>2?argv[2]:NULL);
        if(mode==LETTERS_INITIAL||mode==LETTERS_SPELL)loading_placeholder(mode,source,argc>2?argv[2]:NULL);
        if (mode == LETTERS_SPELL) {
            /* A property can be right while a rendered label lags one round.
             * Reapplying every expected label must leave the framebuffer unchanged. */
            uint32_t before = 2166136261u;
            for (unsigned byte=0;byte<surface.stride*surface.height;byte++)
                before=(before^((const uint8_t*)surface.pixels)[byte])*16777619u;
            for (unsigned tile_index=0;tile_index<(unsigned)value("game.length");tile_index++) {
                char property[32],variable[32],tile[128];
                Dmod_SnPrintf(property,sizeof(property),"game.tile%u",tile_index);text(property,tile);
                Dmod_SnPrintf(variable,sizeof(variable),"lesson_tile%u_text",tile_index);
                check(libdmview_set_string(view,variable,tile)==0,"rendered tile binding");
            }
            libdmview_render(view,&surface,NULL);
            uint32_t after=2166136261u;
            for(unsigned byte=0;byte<surface.stride*surface.height;byte++)
                after=(after^((const uint8_t*)surface.pixels)[byte])*16777619u;
            check(before==after,"rendered letters match the current round");
        }
        char name[24];Dmod_SnPrintf(name,sizeof(name),"mode%u",mode);save_frame(argc>2?argv[2]:NULL,name);
        if(mode==LETTERS_READ)check(!strcmp(spoken,"read.mp3"),"reading does not speak the answer");
        int score_before=round_index;
        if(mode==LETTERS_SPELL){
            const unsigned char* at=(const unsigned char*)letters_words[word].upper;
            unsigned glyph=0;
            while(*at&&!value("game.solved")&&glyph<LETTERS_TILES){
                char expected[4]={0};unsigned bytes=*at<128?1:((*at&0xe0)==0xc0?2:3);
                memcpy(expected,at,bytes);
                int used=value("game.used");unsigned slot=0;
                for(;slot<LETTERS_TILES;slot++){
                    char property[32],tile[128];Dmod_SnPrintf(property,sizeof(property),"game.tile%u",slot);text(property,tile);
                    if(!(used&(1<<slot))&&!strcmp(tile,expected))break;
                }
                check(slot<LETTERS_TILES,"spelling matching tile");if(slot==LETTERS_TILES)break;
                choose(mode,slot);at+=bytes;glyph++;
            }
        }else{
            unsigned right=0;
            for(;right<3;right++){
                char property[32],option[128];Dmod_SnPrintf(property,sizeof(property),mode==0?"game.letter%u":"game.picture%u",right);text(property,option);
                if(!strcmp(option,mode==0?letters_words[word].letter:source))break;
            }
            check(right<3,"matching choice");
            choose(mode,(right+1)%3);check(value("game.wrong")==1&&!value("game.solved"),"wrong answer retries");
            choose(mode,right);
        }
        check(value("game.solved")==1,"correct answer celebration");
        check(rewards==(unsigned)score_before+1,"one reward per round");
        choose(mode,0);check(rewards==(unsigned)score_before+1,"double tap blocked");
        sound_busy=true;now+=LETTERS_REWARD_MS+1;step();
        check(value("game.round")==round,"wait for applause to finish");
        sound_busy=false;step();
        check(value("game.round")!=round,"automatic next round");
    }
    check(modes==15,"all four task kinds");
    click(compact?65:75,43);check(value("game.active")==0,"back to game library");
    click(20,compact?45:48);check(!games_is_open(game),"leave games");
    check(music_playing&&siren&&pauses==2,"music and siren restored");
    games_free(game);libdmview_release(claim);libdmview_display_unregister(display);
    libdmview_close(view);Dmod_Free(surface.pixels);
    Dmod_Printf("Button feedback: %u touch-down checks\n",feedback_checks);
    Dmod_Printf("%s Literki real input %ux%u: %d failure(s)\n",failures?"FAIL":"PASS",w,h,failures);
    return failures;
}
