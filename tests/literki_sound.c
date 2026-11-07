#include "dmod.h"
#include "lesson_audio.h"

/* Opt-in hardware test: plays the actual synthesized reward through the mixer.
 * Never part of ctest: it intentionally makes an audible sound. */
int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    lesson_audio_t audio=lesson_audio_new("/tmp");
    if(!audio){Dmod_Printf("FAIL reward audio allocation\n");return 1;}
    lesson_audio_play(audio,"",true);
    Dmod_Timestamp_t start=Dmod_GetUptime();
    while(lesson_audio_busy(audio)&&Dmod_GetUptime()-start<7000)Dmod_ThreadSleep(20);
    uint32_t frames=lesson_audio_frames(audio);
    bool done=!lesson_audio_busy(audio);
    lesson_audio_free(audio);
    Dmod_Printf("%s reward speaker: %u PCM frames, finished=%u\n",done&&frames>=22050?"PASS":"FAIL",(unsigned)frames,done);
    return done&&frames>=22050?0:1;
}
