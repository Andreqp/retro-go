#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "string.h"

#include "../components/gnuboy/loader.h"
#include "../components/gnuboy/hw.h"
#include "../components/gnuboy/lcd.h"
#include "../components/gnuboy/fb.h"
#include "../components/gnuboy/cpu.h"
#include "../components/gnuboy/mem.h"
#include "../components/gnuboy/sound.h"
#include "../components/gnuboy/pcm.h"
#include "../components/gnuboy/regs.h"
#include "../components/gnuboy/rtc.h"
#include "../components/gnuboy/gnuboy.h"

#include "odroid_system.h"

#define AUDIO_SAMPLE_RATE (32000)
// Note: Magic number obtained by adjusting until audio buffer overflows stop.
#define AUDIO_BUFFER_SAMPLES (AUDIO_SAMPLE_RATE / 10 + 1)
//printf("CHECKPOINT AUDIO: HEAP:0x%x - allocating 0x%x\n", esp_get_free_heap_size(), audioBufferLength * sizeof(int16_t) * 2 * 2);
#define AUDIO_BUFFER_SIZE (AUDIO_BUFFER_SAMPLES * sizeof(int16_t) * 2)

#define GB_WIDTH (160)
#define GB_HEIGHT (144)

extern int debug_trace;

struct fb fb;
struct pcm pcm;

static ODROID_START_ACTION startAction = 0;
static char *romPath = NULL;

uint16_t* framebuffers[2]; //= { fb0, fb0 }; //[160 * 144];
uint8_t currentBuffer;

int frame = 0;
uint elapsedTime = 0;

int16_t* audioBuffers[2];
volatile uint8_t currentAudioBuffer = 0;
volatile uint16_t currentAudioSampleCount;
volatile int16_t* currentAudioBufferPtr;

// --- MAIN
QueueHandle_t videoQueue;
QueueHandle_t audioQueue;

int pcm_submit()
{
    odroid_audio_submit(currentAudioBufferPtr, currentAudioSampleCount >> 1);

    return 1;
}


void run_to_vblank()
{
  /* FRAME BEGIN */

  /* FIXME: djudging by the time specified this was intended
  to emulate through vblank phase which is handled at the
  end of the loop. */
  cpu_emulate(2280);

  /* FIXME: R_LY >= 0; comparsion to zero can also be removed
  altogether, R_LY is always 0 at this point */
  while (R_LY > 0 && R_LY < 144)
  {
    /* Step through visible line scanning phase */
    emu_step();
  }

  /* VBLANK BEGIN */

  //vid_end();
  if ((frame % 2) == 0)
  {
      xQueueSend(videoQueue, &framebuffers[currentBuffer], portMAX_DELAY);

      // swap buffers
      currentBuffer = currentBuffer ? 0 : 1;
      fb.ptr = framebuffers[currentBuffer];
  }

  rtc_tick();

  sound_mix();

  //if (pcm.pos > 100)
  {
        currentAudioBufferPtr = audioBuffers[currentAudioBuffer];
        currentAudioSampleCount = pcm.pos;

        void* tempPtr = 0x1234;
        xQueueSend(audioQueue, &tempPtr, portMAX_DELAY);

        // Swap buffers
        currentAudioBuffer = currentAudioBuffer ? 0 : 1;
        pcm.buf = audioBuffers[currentAudioBuffer];
        pcm.pos = 0;
  }

  if (!(R_LCDC & 0x80)) {
    /* LCDC operation stopped */
    /* FIXME: djudging by the time specified, this is
    intended to emulate through visible line scanning
    phase, even though we are already at vblank here */
    cpu_emulate(32832);
  }

  while (R_LY > 0) {
    /* Step through vblank phase */
    emu_step();
  }
}


int8_t scaling_mode = ODROID_SCALING_FILL;

volatile bool videoTaskIsRunning = false;
void videoTask(void *arg)
{
    videoTaskIsRunning = true;

    int8_t previous_scaling_mode = -1;
    uint16_t* param;

    while(1)
    {
        xQueuePeek(videoQueue, &param, portMAX_DELAY);

        if (param == 1)
            break;

        if (previous_scaling_mode != scaling_mode)
        {
            ili9341_blank_screen();
            previous_scaling_mode = scaling_mode;
        }

        ili9341_write_frame_gb(param, scaling_mode);

        xQueueReceive(videoQueue, &param, portMAX_DELAY);
    }

    videoTaskIsRunning = false;

    vTaskDelete(NULL);

    while (1) {}
}


volatile bool audioTaskIsRunning = false;
void audioTask(void* arg)
{
    audioTaskIsRunning = true;

    uint16_t* param;

    while(1)
    {
        xQueuePeek(audioQueue, &param, portMAX_DELAY);

        if (param == 1)
            break;

        pcm_submit();

        xQueueReceive(audioQueue, &param, portMAX_DELAY);
    }

    odroid_audio_terminate();

    audioTaskIsRunning = false;

    vTaskDelete(NULL);

    while (1) {}
}


void SaveState()
{
    // Save sram
    odroid_input_battery_monitor_enabled_set(0);
    odroid_system_led_set(1);

    char* pathName = odroid_sdcard_get_savefile_path(romPath);
    if (!pathName) abort();

    FILE* f = fopen(pathName, "w");
    if (f == NULL)
    {
        printf("%s: fopen save failed\n", __func__);
        odroid_overlay_alert("Save failed");
    }

    savestate(f);
    rtc_save_internal(f);
    fclose(f);

    printf("%s: savestate OK.\n", __func__);

    free(pathName);

    odroid_system_led_set(0);
    odroid_input_battery_monitor_enabled_set(1);
}

void LoadState(const char* cartName)
{
    char* pathName = odroid_sdcard_get_savefile_path(romPath);
    if (!pathName) abort();

    FILE* f = fopen(pathName, "r");
    if (f == NULL)
    {
        printf("LoadState: fopen load failed\n");
    }
    else
    {
        loadstate(f);
        rtc_load_internal(f);
        fclose(f);

        vram_dirty();
        pal_dirty();
        sound_dirty();
        mem_updatemap();

        printf("LoadState: loadstate OK.\n");
    }

    free(pathName);
}

void QuitEmulator(bool save)
{
    printf("QuitEmulator: stopping tasks.\n");

    uint16_t* param = 1;
    xQueueSend(audioQueue, &param, portMAX_DELAY);
    xQueueSend(videoQueue, &param, portMAX_DELAY);
    while (videoTaskIsRunning || audioTaskIsRunning) vTaskDelay(1);

    odroid_audio_terminate();
    ili9341_blank_screen();

    odroid_display_lock();
    odroid_display_show_hourglass();
    odroid_display_unlock();

    if (save) {
        printf("QuitEmulator: Saving state.\n");
        SaveState();
    }

    // Set menu application
    odroid_system_application_set(0);

    // Reset
    esp_restart();
}

bool palette_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event)
{
    int pal = odroid_settings_Palette_get();
    int max = 7;

    if (event == ODROID_DIALOG_PREV && pal > 0) {
        odroid_settings_Palette_set(--pal);
        pal_set(pal);
    }

    if (event == ODROID_DIALOG_NEXT && pal < max) {
        odroid_settings_Palette_set(++pal);
        pal_set(pal);
    }

    sprintf(option->value, "%d/%d", pal, max);
    return event == ODROID_DIALOG_ENTER;
}


bool rtc_t_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event)
{
    if (option->id == 'd') {
        if (event == ODROID_DIALOG_PREV && --rtc.d < 0) rtc.d = 364;
        if (event == ODROID_DIALOG_NEXT && ++rtc.d > 364) rtc.d = 0;
        sprintf(option->value, "%03d", rtc.d);
    }
    if (option->id == 'h') {
        if (event == ODROID_DIALOG_PREV && --rtc.h < 0) rtc.h = 23;
        if (event == ODROID_DIALOG_NEXT && ++rtc.h > 23) rtc.h = 0;
        sprintf(option->value, "%02d", rtc.h);
    }
    if (option->id == 'm') {
        if (event == ODROID_DIALOG_PREV && --rtc.m < 0) rtc.m = 59;
        if (event == ODROID_DIALOG_NEXT && ++rtc.m > 59) rtc.m = 0;
        sprintf(option->value, "%02d", rtc.m);
    }
    if (option->id == 's') {
        if (event == ODROID_DIALOG_PREV && --rtc.s < 0) rtc.s = 59;
        if (event == ODROID_DIALOG_NEXT && ++rtc.s > 59) rtc.s = 0;
        sprintf(option->value, "%02d", rtc.s);
    }
    return event == ODROID_DIALOG_ENTER;
}

bool rtc_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event)
{
    if (event == ODROID_DIALOG_ENTER) {
        odroid_dialog_choice_t choices[] = {
            {'d', "Day", "000", 1, &rtc_t_update_cb},
            {'h', "Hour", "00", 1, &rtc_t_update_cb},
            {'m', "Min", "00", 1, &rtc_t_update_cb},
            {'s', "Sec", "00", 1, &rtc_t_update_cb},
        };
        odroid_overlay_dialog("Set Clock", choices, 4, 0);
    }
    sprintf(option->value, "%02d:%02d", rtc.h, rtc.m);
    return false;
}

void app_main(void)
{
    printf("gnuboy (%s-%s).\n", COMPILEDATE, GITREV);

    // Do before odroid_system_init to make sure we get the caps requested
    framebuffers[0] = heap_caps_calloc(GB_WIDTH * GB_HEIGHT, 2, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    framebuffers[1] = heap_caps_calloc(GB_WIDTH * GB_HEIGHT, 2, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    audioBuffers[0] = heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    audioBuffers[1] = heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_DMA);

    // Init all the console hardware
	odroid_system_init(1, AUDIO_SAMPLE_RATE, &romPath, &startAction);

    assert(framebuffers[0] && framebuffers[1]);
    assert(audioBuffers[0] && audioBuffers[1]);

    scaling_mode = odroid_settings_Scaling_get();

    videoQueue = xQueueCreate(1, sizeof(uint16_t*));
    audioQueue = xQueueCreate(1, sizeof(uint16_t*));

    xTaskCreatePinnedToCore(&videoTask, "videoTask", 1024, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(&audioTask, "audioTask", 2048, NULL, 5, NULL, 1); //768


    // Load ROM
    loader_init(romPath);

    // RTC
    memset(&rtc, 0, sizeof(rtc));

    // Video
    memset(&fb, 0, sizeof(fb));
    fb.w = 160;
  	fb.h = 144;
  	fb.pelsize = 2;
  	fb.pitch = fb.w * fb.pelsize;
  	fb.indexed = 0;
  	fb.ptr = framebuffers[currentBuffer];
  	fb.enabled = 1;
  	fb.dirty = 0;

    // Audio
    memset(&pcm, 0, sizeof(pcm));
    pcm.hz = AUDIO_SAMPLE_RATE;
  	pcm.stereo = 1;
  	pcm.len = AUDIO_BUFFER_SAMPLES; // count of 16bit samples (x2 for stereo)
  	pcm.buf = audioBuffers[0];
  	pcm.pos = 0;


    emu_reset();

    lcd_begin();

    pal_set(odroid_settings_Palette_get());

    // Load state
    if (startAction == ODROID_START_ACTION_RESUME)
    {
        LoadState(romPath);
    }


    uint startTime;
    uint stopTime;
    uint totalElapsedTime = 0;
    uint actualFrameCount = 0;

    while (true)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);

        if (joystick.values[ODROID_INPUT_MENU]) {
            odroid_overlay_game_menu();
        }
        else if (joystick.values[ODROID_INPUT_VOLUME]) {
            odroid_dialog_choice_t options[] = {
                {10, "Palette", "7/7",  1, &palette_update_cb},
                {20, "Set clock", "00:00", 1, &rtc_update_cb},
            };
            odroid_overlay_settings_menu(options, 2);
        }

        pad_set(PAD_UP, joystick.values[ODROID_INPUT_UP]);
        pad_set(PAD_RIGHT, joystick.values[ODROID_INPUT_RIGHT]);
        pad_set(PAD_DOWN, joystick.values[ODROID_INPUT_DOWN]);
        pad_set(PAD_LEFT, joystick.values[ODROID_INPUT_LEFT]);

        pad_set(PAD_SELECT, joystick.values[ODROID_INPUT_SELECT]);
        pad_set(PAD_START, joystick.values[ODROID_INPUT_START]);

        pad_set(PAD_A, joystick.values[ODROID_INPUT_A]);
        pad_set(PAD_B, joystick.values[ODROID_INPUT_B]);


        startTime = xthal_get_ccount();
        run_to_vblank();
        stopTime = xthal_get_ccount();


        if (stopTime > startTime)
          elapsedTime = (stopTime - startTime);
        else
          elapsedTime = ((uint64_t)stopTime + (uint64_t)0xffffffff) - (startTime);

        totalElapsedTime += elapsedTime;
        ++frame;
        ++actualFrameCount;

        if (actualFrameCount == 60)
        {
            float seconds = totalElapsedTime / (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000.0f); // 240000000.0f; // (240Mhz)
            float fps = actualFrameCount / seconds;

            odroid_battery_state battery;
            odroid_input_battery_level_read(&battery);

            printf("HEAP:%d, FPS:%f, BATTERY:%d [%d]\n",
                esp_get_free_heap_size() / 1024, fps,
                battery.millivolts, battery.percentage);

            actualFrameCount = 0;
            totalElapsedTime = 0;
        }
    }
}
