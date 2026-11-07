#include "dmod.h"
#include "libdmview.h"
#include "fuel.h"
#include "sfx_synth.h"
#include "libdmaudio.h"
#include <string.h>

/* Run through the real renderer/input interpreter on host or board. This is
 * an independent off-screen view: synthetic contacts do not touch the UI. */
static libdmview_t view;
static libdmview_surface_t surface;
static uint32_t now_ms;
static int failures;
static int previous_contacts;

static void expect(const char* name, int32_t expected)
{
    int32_t actual = 0;
    int rc = libdmview_get_int(view, name, &actual);
    if (rc != 0 || actual != expected)
    {
        Dmod_Printf("FAIL %s: rc=%d got=%ld wanted=%ld\n", name, rc, (long)actual, (long)expected);
        failures++;
    }
}

static void input(int count, int x0, int y0, int x1, int y1)
{
    dmdrvi_input_state_t state;
    memset(&state, 0, sizeof(state));
    state.contact_count = (uint8_t)count;
    state.contacts[0].id = 0;
    state.contacts[0].x = (uint16_t)x0;
    state.contacts[0].y = (uint16_t)y0;
    state.contacts[1].id = 1;
    state.contacts[1].x = (uint16_t)x1;
    state.contacts[1].y = (uint16_t)y1;
    state.contacts[0].event = previous_contacts > 0 ? DMDRVI_INPUT_CONTACT_MOVE : DMDRVI_INPUT_CONTACT_DOWN;
    state.contacts[1].event = previous_contacts > 1 ? DMDRVI_INPUT_CONTACT_MOVE : DMDRVI_INPUT_CONTACT_DOWN;
    previous_contacts = count;
    now_ms += 20;
    int rc = libdmview_input(view, &state, now_ms);
    if (rc < 0) { Dmod_Printf("FAIL input: %d\n", rc); failures++; }
    rc = libdmview_render(view, &surface, NULL);
    if (rc < 0) { Dmod_Printf("FAIL render: %d\n", rc); failures++; }
}

static void click(int x, int y)
{
    input(1, x, y, 0, 0);
    input(0, 0, 0, 0, 0);
}

/* Optional PPM previews from the actual RGB565 renderer (second argument). */
static void save_frame(const char* prefix, const char* page)
{
    char path[256], header[48];
    Dmod_SnPrintf(path, sizeof(path), "%s-%s.ppm", prefix, page);
    void* file = Dmod_FileOpen(path, "wb");
    uint8_t* row = Dmod_Malloc(surface.width * 3u);
    if (file == NULL || row == NULL)
    {
        if (file != NULL) Dmod_FileClose(file);
        Dmod_Free(row); failures++; return;
    }
    int n = Dmod_SnPrintf(header, sizeof(header), "P6\n%u %u\n255\n", surface.width, surface.height);
    Dmod_FileWrite(header, 1, (size_t)n, file);
    for (uint16_t y = 0; y < surface.height; y++)
    {
        const uint16_t* pixels = (const uint16_t*)((const uint8_t*)surface.pixels + y * surface.stride);
        for (uint16_t x = 0; x < surface.width; x++)
        {
            uint16_t c = pixels[x];
            row[x * 3] = ((c >> 11) & 31) * 255 / 31;
            row[x * 3 + 1] = ((c >> 5) & 63) * 255 / 63;
            row[x * 3 + 2] = (c & 31) * 255 / 31;
        }
        if (Dmod_FileWrite(row, 3, surface.width, file) != surface.width)
            failures++;
    }
    Dmod_FileClose(file); Dmod_Free(row);
}

static void sync_fuel(uint32_t claim, const fuel_t* tank)
{
    libdmview_prop_set_int(claim, "fuel.stage", tank->stage);
    libdmview_prop_set_int(claim, "fuel.level", fuel_level(tank));
    libdmview_prop_set_int(claim, "fuel.notice", tank->notice);
    libdmview_prop_set_int(claim, "fuel.warning", fuel_warning_visible(tank) ? 1 : 0);
    libdmview_prop_set_int(claim, "engine", tank->engine_on ? 1 : 0);
    libdmview_update(view, now_ms);
    libdmview_render(view, &surface, NULL);
    expect("fuel_engine_blocked_alpha", fuel_warning_visible(tank) ? 255 : 0);
    bool empty = tank->notice == FUEL_NOTICE_EMPTY;
    expect("engine_empty_alpha", empty ? 255 : 0);
    expect("engine_on_alpha", !empty && tank->engine_on ? 255 : 0);
    expect("engine_off_alpha", !empty && !tank->engine_on ? 255 : 0);
    int32_t sounds = 0, panel = 0;
    libdmview_get_int(view, "view_sounds_alpha", &sounds);
    libdmview_get_int(view, "fuel_panel_alpha", &panel);
    if (panel == 255 && fuel_warning_visible(tank))
    {
        int cx = surface.width == 320 ? 280 : 430;
        int cy = surface.width == 320 ? 108 : 120;
        int radius = surface.width == 320 ? 22 : 24;
        int red = 0;
        for (int y = cy - radius; y < cy + radius; y++)
        {
            const uint16_t* row = (const uint16_t*)((const uint8_t*)surface.pixels + y * surface.stride);
            for (int x = cx - radius; x < cx + radius; x++)
            {
                uint16_t c = row[x];
                int r = ((c >> 11) & 31) * 255 / 31;
                int g = ((c >> 5) & 63) * 255 / 63;
                int b = (c & 31) * 255 / 31;
                if (r > 180 && g < 160 && b < 160) red++;
            }
        }
        if (red < 150) { Dmod_Printf("FAIL crossed-out engine icon not drawn\n"); failures++; }
    }
    if (sounds == 255 && panel == 0)
    {
        /* Check the rendered icon, not just the application's engine property. */
        int sidebar = surface.width == 320 ? 40 : 50;
        int end = sidebar + (surface.width - sidebar) / 2 - 4;
        int amber = 0, red = 0;
        for (int y = 80; y < 180; y++)
        {
            const uint16_t* row = (const uint16_t*)((const uint8_t*)surface.pixels + y * surface.stride);
            for (int x = sidebar + 8; x < end; x++)
            {
                uint16_t c = row[x];
                int r = ((c >> 11) & 31) * 255 / 31;
                int g = ((c >> 5) & 63) * 255 / 63;
                int b = (c & 31) * 255 / 31;
                if (r > 180 && g > 150 && g < 230 && b < 100) amber++;
                if (r > 180 && g < 150 && b < 150) red++;
            }
        }
        bool running = !empty && tank->engine_on;
        if ((running && amber < 100) || (!running && amber != 0) || (empty && red < 100) || (!empty && red > 10))
        {
            Dmod_Printf("FAIL engine icon color: running=%d empty=%d amber=%d red=%d\n", running, empty, amber, red);
            failures++;
        }
    }
}

/* Exercise emitted UI events against the same fuel model as carhmi. */
static void fuel_action(uint32_t claim, fuel_t* tank, const char* wanted)
{
    libdmview_event_t event;
    int rc;
    do { rc = libdmview_wait_event(claim, &event, 0); }
    while (rc == 0 && strncmp(event.name, "game.", 5) == 0);
    if (rc != 0 || strcmp(event.name, wanted) != 0)
    {
        Dmod_Printf("FAIL fuel event: rc=%d actual=%s wanted=%s\n", rc, rc == 0 ? event.name : "none", wanted);
        failures++;
        return;
    }
    fuel_event(tank, event.name);
    sync_fuel(claim, tank);
    do { rc = libdmview_wait_event(claim, &event, 0); }
    while (rc == 0 && strncmp(event.name, "game.", 5) == 0);
    if (rc == 0)
    {
        Dmod_Printf("FAIL duplicate fuel event: %s\n", event.name);
        failures++;
    }
}

int main(int argc, char** argv)
{
    int status = 0;
    bool play_pump = argc > 2 && strcmp(argv[2], "--pump") == 0;
    const char* preview = argc > 2 && !play_pump ? argv[2] : NULL;
    const char* path = argc > 1 ? argv[1] : "/mnt/MEDIA/views/carhmi_view/carhmi.dmv";
    view = libdmview_open(path, &status);
    if (view == NULL)
    {
        Dmod_Printf("FAIL open: %d\n", status);
        return 1;
    }
    uint16_t width = 0, height = 0;
    libdmview_get_size(view, &width, &height);
    bool compact = width == 320 && height == 240;
    if (!compact && !(width == 480 && height == 272))
    {
        Dmod_Printf("FAIL unsupported view size: %ux%u\n", width, height);
        libdmview_close(view); return 1;
    }
    surface.width = width; surface.height = height; surface.stride = width * 2u;
    surface.format = DMDRVI_GFX_PIXEL_FORMAT_RGB565;
    surface.pixels = Dmod_Malloc(surface.stride * surface.height);
    if (surface.pixels == NULL)
    {
        Dmod_Printf("FAIL framebuffer allocation\n");
        libdmview_close(view); return 1;
    }
    libdmview_render(view, &surface, NULL);
    if (preview != NULL) save_frame(preview, "nav");
    expect("nav_map_near_x", compact ? -160 : -110);
    input(1, 140, 140, 0, 0);
    input(1, 170, 160, 0, 0);
    expect("nav_map_near_x", compact ? -130 : -80);
    expect("nav_map_near_y", compact ? -344 : -312);
    expect("navg_car_x", compact ? 156 : 206);
    expect("navg_car_y", compact ? 148 : 180);
    expect("navRunning", 0);
    input(0, 0, 0, 0, 0);
    /* Two contacts move closer: native PINCH chooses the wide projection. */
    input(2, 130, 140, 230, 140);
    input(2, 130, 140, 190, 140);
    expect("nav_map_near_alpha", 0);
    expect("nav_map_wide_alpha", 255);
    input(0, 0, 0, 0, 0);
    input(2, 130, 140, 190, 140);
    input(2, 130, 140, 250, 140);
    expect("nav_map_near_alpha", 255);
    expect("nav_map_wide_alpha", 0);
    input(0, 0, 0, 0, 0);
    /* Captured drag may leave the viewport; its content stays bounded. */
    input(1, 150, 140, 0, 0);
    input(1, width - 1, height - 1, 0, 0);
    expect("nav_map_near_x", 0);
    input(0, 0, 0, 0, 0);
    click(width - 25, 115); /* minus */
    expect("nav_map_near_alpha", 0);
    click(width - 25, 75);  /* plus */
    expect("nav_map_near_alpha", 255);
    click(width - 25, 155); /* recenter */
    expect("navg_car_x", compact ? 126 : 176);
    expect("navg_car_y", compact ? 128 : 160);
    /* Finishing the recenter animation returns to the replay's current point. */
    now_ms += 6000;
    libdmview_update(view, now_ms);
    libdmview_render(view, &surface, NULL);
    expect("nav_map_near_x", compact ? -160 : -110);
    expect("nav_map_near_y", compact ? -364 : -332);
    /* Every page must remain reachable on the compact sidebar too. */
    static const char pages[][8] = { "home", "nav", "media", "climate", "sounds", "games" };
    for (int i = 0; i < 6; i++)
    {
        click(20, compact ? 46 + i * 34 : 48 + i * 40);
        now_ms += 1000;
        libdmview_update(view, now_ms);
        libdmview_render(view, &surface, NULL);
        char alpha[48];
        Dmod_SnPrintf(alpha, sizeof(alpha), "view_%s_alpha", pages[i]);
        expect(alpha, 255);
        if (preview != NULL) save_frame(preview, pages[i]);
    }
    libdmview_display_t display = libdmview_display_register("carhmi_fuel_test");
    uint32_t claim = 0;
    if (display == NULL || libdmview_claim("carhmi_fuel_test", path, &claim) != 0 ||
        libdmview_attach(view, display) != 0)
    {
        Dmod_Printf("FAIL fuel test display/claim\n"); failures++;
    }
    else
    {
        fuel_t tank;
        fuel_init(&tank, 0);
        fuel_event(&tank, "engine");
        fuel_update(&tank, FUEL_RUN_MS * 65u / 100u);
        fuel_event(&tank, "engine");
        libdmview_prop_set_int(claim, "fuel.level", fuel_level(&tank));
        libdmview_update(view, now_ms);
        expect("fuel_liquid_h", 41); /* 35% of the 118px well, from the bottom */
        click(112, 12); /* Fuel is reachable from any page via the top bar. */
        fuel_action(claim, &tank, "fuel.open");
        expect("fuel_panel_alpha", 255);
        expect("fuel_cap_alpha", 255);
        expect("fuel_hose_alpha", 0);
        if (preview != NULL) save_frame(preview, "fuel-capped");
        int filler_x = compact ? 210 : 344;
        int filler_y = compact ? 100 : 112;
        click(filler_x, filler_y);
        fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_stage", FUEL_OPEN);
        expect("fuel_cap_alpha", 0);
        expect("fuel_mouth_alpha", 255);
        expect("fuel_hose_alpha", 0);
        if (preview != NULL) save_frame(preview, "fuel-open");
        click(filler_x, filler_y);
        fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_stage", FUEL_PUMPING);
        expect("fuel_hose_alpha", 255);
        if (preview != NULL) save_frame(preview, "fuel-pumping");
        fuel_update(&tank, tank.updated_ms + 10000);
        libdmview_prop_set_int(claim, "fuel.level", fuel_level(&tank));
        libdmview_update(view, now_ms);
        expect("fuel_liquid_h", 100); /* 85% */
        libdmview_render(view, &surface, NULL);
        if (preview != NULL) save_frame(preview, "fuel-filling");
        fuel_update(&tank, tank.updated_ms + FUEL_FILL_MS);
        libdmview_prop_set_int(claim, "fuel.level", fuel_level(&tank));
        libdmview_update(view, now_ms);
        libdmview_prop_set_int(claim, "fuel.stage", tank.stage);
        libdmview_update(view, now_ms);
        expect("fuel_stage", FUEL_FULL);
        expect("fuel_liquid_h", 118);
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_stage", FUEL_DISCONNECTED);
        expect("fuel_hose_alpha", 0);
        expect("fuel_mouth_alpha", 255);
        expect("fuel_cap_alpha", 0);
        if (preview != NULL) save_frame(preview, "fuel-disconnected");
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_stage", FUEL_CAPPED);
        expect("fuel_mouth_alpha", 0);
        expect("fuel_cap_alpha", 255);
        if (preview != NULL) save_frame(preview, "fuel-closed");
        click(width - 32, 48);
        fuel_action(claim, &tank, "fuel.close");
        expect("fuel_panel_alpha", 0);
        /* The dashboard gauge also opens the same overlay. */
        click(20, compact ? 51 : 54);
        now_ms += 1000; libdmview_update(view, now_ms);
        libdmview_render(view, &surface, NULL);
        expect("view_home_alpha", 255);
        expect("view_sounds_alpha", 0);
        click(85, compact ? 91 : 101);
        fuel_action(claim, &tank, "fuel.open");
        expect("fuel_panel_alpha", 255);
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_stage", FUEL_FULL); /* Already full: pump stays off. */
        click(width - 32, 48); fuel_action(claim, &tank, "fuel.close");
        /* Switching pages remains possible with a connected hose. */
        fuel_event(&tank, "engine");
        fuel_update(&tank, tank.updated_ms + 90000);
        fuel_event(&tank, "engine");
        libdmview_prop_set_int(claim, "fuel.level", fuel_level(&tank));
        libdmview_update(view, now_ms);
        click(112, 12); fuel_action(claim, &tank, "fuel.open");
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_stage", FUEL_PUMPING);
        for (int page = 0; page < 6; page++)
        {
            click(20, compact ? 46 + page * 34 : 48 + page * 40);
            fuel_action(claim, &tank, "fuel.close");
            expect("fuel_panel_alpha", 0);
            expect("fuel_stage", FUEL_CAPPED);
            if (tank.visible) { Dmod_Printf("FAIL fuel still visible after navigation\n"); failures++; }
            now_ms += 1000; libdmview_update(view, now_ms);
            libdmview_render(view, &surface, NULL);
            char alpha[48];
            Dmod_SnPrintf(alpha, sizeof(alpha), "view_%s_alpha", pages[page]);
            expect(alpha, 255);
            if (page < 5) { click(112, 12); fuel_action(claim, &tank, "fuel.open"); }
        }
        /* Return from the new last page (Games) to the engine controls. */
        click(20, compact ? 46 + 4 * 34 : 48 + 4 * 40);
        now_ms += 1000; libdmview_update(view, now_ms);
        libdmview_render(view, &surface, NULL);
        expect("view_sounds_alpha", 255);
        if (preview != NULL) save_frame(preview, "before-engine");
        /* Engine/fuel interlocks, driven by real compiled button clicks. */
        int engine_x = compact ? 110 : 150;
        click(engine_x, 140); fuel_action(claim, &tank, "engine");
        expect("engine", 1);
        if (preview != NULL) save_frame(preview, "engine-on");
        click(engine_x, 140); fuel_action(claim, &tank, "engine");
        expect("engine", 0);
        if (preview != NULL) save_frame(preview, "engine-off");
        uint32_t parked_fuel = tank.remaining_ms;
        fuel_update(&tank, tank.updated_ms + FUEL_RUN_MS * 2u);
        sync_fuel(claim, &tank);
        if (tank.remaining_ms != parked_fuel)
        {
            Dmod_Printf("FAIL fuel consumed with engine off\n"); failures++;
        }
        click(engine_x, 140); fuel_action(claim, &tank, "engine");
        expect("engine", 1);
        fuel_update(&tank, tank.updated_ms + 9000);
        sync_fuel(claim, &tank);
        if (tank.remaining_ms != parked_fuel - 9000u)
        {
            Dmod_Printf("FAIL fuel consumption did not resume after restart\n"); failures++;
        }
        click(112, 12); fuel_action(claim, &tank, "fuel.open");
        expect("fuel_engine_blocked_alpha", 0);
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_engine_blocked_alpha", 255);
        expect("fuel_cap_alpha", 255);
        expect("fuel_stage", FUEL_CAPPED);
        expect("fuel_notice", FUEL_NOTICE_ENGINE_RUNNING);
        expect("fuel_hose_alpha", 0);
        if (preview != NULL) save_frame(preview, "fuel-engine-running");
        for (uint32_t half = 1; half <= FUEL_WARNING_FLASHES * 2u; half++)
        {
            fuel_update(&tank, tank.updated_ms + FUEL_WARNING_HALF_MS);
            sync_fuel(claim, &tank);
            expect("fuel_engine_blocked_alpha", half < FUEL_WARNING_FLASHES * 2u && half % 2u == 0 ? 255 : 0);
            expect("fuel_cap_alpha", 255);
        }
        if (preview != NULL) save_frame(preview, "fuel-warning-finished");
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_engine_blocked_alpha", 255);
        expect("fuel_stage", FUEL_CAPPED);
        click(width - 32, 48); fuel_action(claim, &tank, "fuel.close");
        fuel_update(&tank, tank.updated_ms + FUEL_RUN_MS);
        sync_fuel(claim, &tank);
        expect("engine", 0);
        click(engine_x, 140); fuel_action(claim, &tank, "engine");
        expect("engine", 0);
        expect("fuel_notice", FUEL_NOTICE_EMPTY);
        expect("engine_empty_alpha", 255);
        expect("fuel_top_empty", 1);
        if (preview != NULL) save_frame(preview, "fuel-empty-engine");
        click(112, 12); fuel_action(claim, &tank, "fuel.open");
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        click(filler_x, filler_y); fuel_action(claim, &tank, "fuel.filler");
        expect("fuel_stage", FUEL_PUMPING);
        fuel_update(&tank, tank.updated_ms + 1000);
        sync_fuel(claim, &tank);
        expect("fuel_notice", FUEL_NOTICE_NONE);
        expect("engine_empty_alpha", 0);
        expect("fuel_top_empty", 0);
        click(width - 32, 48); fuel_action(claim, &tank, "fuel.close");
        click(engine_x, 140); fuel_action(claim, &tank, "engine");
        expect("engine", 1);
        libdmview_release(claim);
    }
    libdmview_display_unregister(display);
    libdmview_close(view); Dmod_Free(surface.pixels);
    if (play_pump)
    {
        int before = failures;
        libdmaudio_format_t format = { SFX_SYNTH_RATE_HZ, 1, DMDRVI_AUDIO_FORMAT_S16_LE };
        libdmaudio_stream_options_t options = { 250, 100 };
        libdmaudio_stream_t stream = libdmaudio_open(NULL, &format, &options, &status);
        sfx_synth_t synth = sfx_synth_create();
        int16_t pcm[441];
        if (stream == NULL || synth == NULL)
        {
            Dmod_Printf("FAIL fuel audio open: %d\n", status); failures++;
        }
        else
        {
            sfx_synth_pump(synth, true);
            for (int chunk = 0; chunk < 110; chunk++)
            {
                if (chunk == 100) sfx_synth_pump(synth, false);
                sfx_synth_render(synth, pcm, 441);
                uint32_t sent = 0;
                while (sent < 441)
                {
                    int32_t written = libdmaudio_write(stream, pcm + sent, 441 - sent, 1000);
                    if (written <= 0) { failures++; break; }
                    sent += (uint32_t)written;
                }
                if (failures != before) break;
            }
            if (libdmaudio_drain(stream, 2000) != 0) failures++;
        }
        sfx_synth_destroy(synth); libdmaudio_close(stream);
        Dmod_Printf("fuel pump audio: %s\n", failures == before ? "PASS" : "FAIL");
    }
    Dmod_Printf("navigation_input %ux%u: %s (%d failures)\n", width, height, failures ? "FAIL" : "PASS", failures);
    return failures;
}
