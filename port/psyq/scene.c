/* Deterministic scene input and post-dispatch state observation. */
#define _GNU_SOURCE
#include "scene.h"
#include "clock.h"
#include "ps1mem.h"
#include "../backend/spu.h"
#include "../backend/xa.h"
#include "../backend/gpu.h"
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <dlfcn.h>
#include <link.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

extern void port_pad_latch(uint16_t buttons);
static const PortCapture *config;
static FILE *input, *trace;
static int have_origin, have_mark, pending;
static uint32_t origin, mark, event_k, previous_k;
static unsigned buttons, held, rand_index;
static int have_previous;
typedef struct { char name[128]; char type[8]; unsigned count, width; void *address; } Variable;
static Variable *variables;
static size_t variable_count;
typedef struct {
    char label[128];
    void *function;
    unsigned occurrence, hits, window, reached, start, after;
} Checkpoint;
static Checkpoint *checkpoints;
static size_t checkpoint_count, next_checkpoint;
static void *mark_function;
typedef struct { unsigned checkpoint, k, mask, applied; } Event;
static Event *events;
static size_t event_count;
static void snapshot(const char *label, unsigned relative);
static void fail(const char *message);
typedef struct {
    char start[128], finish[128], trace[1024], wav[1024];
    unsigned window, origin, stop_count;
    char anchor[32];
    uint64_t cycle;
    char stop_names[32][128];
    void *stops[32];
} AudioSegment;
static AudioSegment audio_segments[2];
static unsigned audio_count, audio_index;
static FILE *audio_trace, *audio_wav, *xa_wav;
static FILE *audio_open(const char *path, const char *mode)
{
    char resolved[PATH_MAX], root[PATH_MAX], parent[PATH_MAX];
    struct stat info;
    if (!lstat(path, &info) && S_ISLNK(info.st_mode)) fail("audio file must not be a symlink");
    if (!realpath(DW1_SOURCE_ROOT, root)) fail("cannot resolve repository root");
    if (!realpath(path, resolved)) {
        if (errno != ENOENT || path[0] != '/') fail("audio path must be absolute and outside repository sources");
        strcpy(parent, path); /* Declaration paths are limited to 1023 bytes. */
        char *slash = strrchr(parent, '/');
        if (!slash || !slash[1]) fail("invalid audio path");
        if (slash == parent) strcpy(parent, "/");
        else *slash = 0;
        if (!realpath(parent, resolved)) fail("cannot resolve audio directory");
    }
    size_t length = strlen(root);
    if (!strncmp(root, resolved, length) && (!resolved[length] || resolved[length] == '/')) {
        /* The owner permits persistent evidence in the ignored build tree.
         * Check the resolved path so aliases into sources remain forbidden. */
        const char *relative = resolved + length;
        if (strncmp(relative, "/build", 6) || (relative[6] && relative[6] != '/'))
            fail("audio evidence must be outside repository sources (build is allowed)");
    }
    return fopen(path, mode);
}
int port_scene_audio_complete(void) { return audio_count && audio_index == audio_count; }
static FILE *movie_trace;
static char movie_directory[1024];
static unsigned movie_frames, movie_origin;
static uint64_t movie_xa_cycle;
void port_scene_movie_timing(const char *event, unsigned value)
{
    if (movie_trace && getenv("DW1_MOVIE_TIMING_TRACE"))
        fprintf(stderr, "movie-timing %s %llu %u %u %u\n", event,
                (unsigned long long)clock_now(), clock_vblanks()-movie_origin,
                movie_frames, value);
}
static void movie_function_timing(void *function, const char *event)
{
    if (!movie_trace || !getenv("DW1_MOVIE_TIMING_TRACE")) return;
    const char *names[] = {"MOV_decodeFrame", "MOV_waitForFlip", "MOV_onSliceDecoded"};
    for (unsigned i=0; i<sizeof(names)/sizeof(names[0]); i++)
        if (function==dlsym(RTLD_DEFAULT,names[i])) {
            char name[80]; snprintf(name,sizeof(name),"%s:%s",names[i],event);
            port_scene_movie_timing(name,0);
        }
}

/* Observation only: the last sample of a sector is delivered at this CD event. */
void port_scene_movie_xa(void)
{
    if (movie_trace) movie_xa_cycle = clock_now();
}
void port_scene_movie_display(int x, int y, int width, int height)
{
    if (!movie_trace) return;
    if (width != 320 || height != 240) fail("unexpected movie display dimensions");
    char path[1200];
    snprintf(path,sizeof(path),"%s/frame-%04u.ppm",movie_directory,++movie_frames);
    FILE *file=audio_open(path,"wb");
    if (!file) fail("cannot open movie frame");
    fprintf(file,"P6\n%d %d\n255\n",width,height);
    const uint8_t *vram=(const uint8_t *)gpu_vram();
    for(int row=0;row<height;row++) {
        const uint8_t *src=vram+((y+row)&511)*2048;
        for(int col=0;col<width*3;col++)
            if(fputc(src[(x*2+col)&2047],file)==EOF) fail("cannot write movie frame");
    }
    if(fclose(file)) fail("cannot close movie frame");
    fprintf(movie_trace,"{\"event\":\"frame\",\"index\":%u,\"cycle\":%llu,\"vblank\":%u}\n",
            movie_frames,(unsigned long long)clock_now(),clock_vblanks()-movie_origin);
}

static void audio_end(const char *reason)
{
    AudioSegment *segment = &audio_segments[audio_index];
    if (spu_capture_origin(&segment->cycle, &segment->origin)) fail("missing new note-on audio anchor");
    int failed = fprintf(audio_trace, "# end %llu %s\n",
                         (unsigned long long)((clock_now() - segment->cycle) / 768), reason) < 0;
    failed |= spu_capture_end();
    if (xa_wav) { XaDec_CaptureEnd(); failed |= fclose(xa_wav)!=0; xa_wav=NULL; }
    failed |= fclose(audio_trace) != 0;
    failed |= fclose(audio_wav) != 0;
    audio_trace = audio_wav = NULL;
    if (failed) fail("cannot finalize audio segment");
    audio_index++;
}
static void audio_enter(const char *label)
{
    if (audio_index >= audio_count) return;
    AudioSegment *segment = &audio_segments[audio_index];
    if (audio_trace && !strcmp(label, segment->finish)) {
        audio_end("checkpoint");
        if (audio_index >= audio_count) return;
        segment = &audio_segments[audio_index];
    }
    if (!audio_trace && !strcmp(label, segment->start)) {
        audio_trace = audio_open(segment->trace, "w");
        audio_wav = audio_open(segment->wav, "w+b");
        if (!audio_trace || !audio_wav || spu_capture_begin(audio_trace, audio_wav))
            fail("cannot open audio segment");
        const char *xa_path=getenv("DW1_XA_WAV");
        if (xa_path) {
            if (strlen(xa_path)>=1024) fail("XA path too long");
            xa_wav=audio_open(xa_path,"w+b");
            if (!xa_wav) fail("cannot open XA capture");
            XaDec_CaptureBegin(xa_wav);
        }
        segment->origin = clock_vblanks();
        segment->cycle = clock_now();
        if (!strcmp(segment->anchor, "note_on")) spu_capture_note_anchor();
    }
}
int port_scene_complete(void)
{
    if (!checkpoint_count || next_checkpoint != checkpoint_count) return 0;
    for (size_t i = 0; i < checkpoint_count; i++)
        if (clock_vblanks() - checkpoints[i].start < checkpoints[i].window) return 0;
    return 1;
}
void port_scene_enter(void *function)
{
    if (!checkpoint_count) return;
    if (audio_trace) {
        AudioSegment *segment = &audio_segments[audio_index];
        for (unsigned i = 0; i < segment->stop_count; i++) if (function == segment->stops[i]) {
            char reason[160];
            snprintf(reason, sizeof(reason), "event:%s", segment->stop_names[i]);
            audio_end(reason);
            break;
        }
    }
    if (!have_mark && function == mark_function) port_scene_mark(config->mark);
    if (!have_mark) return;
    for (size_t i = 0; i < checkpoint_count; i++) {
        Checkpoint *cp = &checkpoints[i];
        if (cp->after && !checkpoints[cp->after - 1].reached) continue;
        if (cp->function != function || ++cp->hits != cp->occurrence) continue;
        if (i != next_checkpoint) {
            fprintf(stderr, "scene: checkpoint out of order: %s\n", cp->label);
            exit(1);
        }
        cp->reached = 1;
        cp->start = clock_vblanks();
        next_checkpoint++;
        audio_enter(cp->label);
        if (trace) snapshot(cp->label, 0);
    }
}
void __cyg_profile_func_enter(void *function, void *caller)
{
    (void)caller;
    movie_function_timing(function,"enter");
    if (getenv("DW1_MOVIE_CAPTURE") && function==dlsym(RTLD_DEFAULT,"MOV_playMovie")) {
        const char *directory=getenv("DW1_MOVIE_CAPTURE");
        if(strlen(directory)>=sizeof(movie_directory)) fail("movie output path too long");
        strcpy(movie_directory,directory);
        char path[1200]; snprintf(path,sizeof(path),"%s/movie.jsonl",directory);
        movie_trace=audio_open(path,"w"); if(!movie_trace) fail("cannot open movie trace");
        movie_frames=0; movie_xa_cycle=0; movie_origin=clock_vblanks();
        fprintf(movie_trace,"{\"event\":\"start\",\"cycle\":%llu}\n",(unsigned long long)clock_now());
    }
    port_scene_enter(function);
}
void __cyg_profile_func_exit(void *function, void *caller)
{
    (void)caller;
    movie_function_timing(function,"exit");
    if (movie_trace && function==dlsym(RTLD_DEFAULT,"MOV_playMovie")) {
        const int *completed=dlsym(RTLD_DEFAULT,"MOVIE_COMPLETED");
        fprintf(movie_trace,"{\"event\":\"end\",\"cycle\":%llu,\"vblank\":%u,\"return\":%d,\"xa_cycle\":%llu}\n",
            (unsigned long long)clock_now(),clock_vblanks()-movie_origin,
            completed && *completed==1,(unsigned long long)movie_xa_cycle);
        if(fclose(movie_trace)) fail("cannot close movie trace");
        movie_trace=NULL;
    }
    if (audio_trace && audio_index<audio_count) {
        AudioSegment *segment=&audio_segments[audio_index];
        if (!strcmp(segment->start,segment->finish) && function==dlsym(RTLD_DEFAULT,segment->start))
            audio_end("return");
    }
}
static void fail(const char *message)
{
    fprintf(stderr, "scene: %s\n", message);
    exit(1);
}
static unsigned checkpoint_number(const char *text, int base)
{
    char *end;
    errno = 0;
    unsigned long value = strtoul(text, &end, base);
    if (errno || !text[0] || text[0] == '-' || text[0] == '+' || *end || value > UINT32_MAX)
        fail("invalid checkpoint number");
    return (unsigned)value;
}
static void read_event(void)
{
    char line[256], frame[64], mask[64], extra, *end;
    pending = 0;
    if (!input || !fgets(line, sizeof(line), input)) {
        if (input && ferror(input)) fail("cannot read inputs");
        return;
    }
    if (sscanf(line, "%63s %63s %c", frame, mask, &extra) != 2)
        fail("invalid inputs (expected increasing VBlank and hex mask)");
    errno = 0;
    unsigned long value = strtoul(frame, &end, 10);
    if (errno || frame[0] < '0' || frame[0] > '9' || *end || value > UINT32_MAX)
        fail("invalid input VBlank");
    event_k = (uint32_t)value;
    errno = 0;
    value = strtoul(mask, &end, 16);
    if (errno || mask[0] == '-' || mask[0] == '+' || *end || value > 65535 ||
        (have_previous && event_k <= previous_k)) fail("invalid inputs (expected increasing VBlank and hex mask)");
    buttons = (unsigned)value;
    previous_k = event_k;
    have_previous = pending = 1;
}
static void latch(void)
{
    if (!config) return;
    if (checkpoint_count) {
        for (size_t i = 0; i < event_count; i++) {
            Event *event = &events[i];
            Checkpoint *cp = &checkpoints[event->checkpoint];
            if (!event->applied && cp->reached && clock_vblanks() - cp->start >= event->k) {
                held = event->mask;
                event->applied = 1;
            }
        }
        port_pad_latch((uint16_t)held);
        return;
    }
    if (!input) return;
    int from_mark = config->inputs_from && !strcmp(config->inputs_from, "mark");
    if (!(from_mark ? have_mark : have_origin)) return;
    uint32_t k = clock_vblanks() - (from_mark ? mark : origin);
    while (pending && event_k <= k) { held = buttons; read_event(); }
    port_pad_latch((uint16_t)held);
}
void port_scene_prepare(const PortCapture *value)
{
    config = value;
    if (config->audio_segments) {
        FILE *file = fopen(config->audio_segments, "r");
        if (!file) fail("cannot open audio segment declarations");
        char line[7000], extra;
        while (fgets(line, sizeof(line), file)) {
            if (audio_count == 2) fail("too many audio segments");
            AudioSegment *segment = &audio_segments[audio_count++];
            char stop_list[4096] = "-";
            int fields = sscanf(line, "%127s %127s %u %1023s %1023s %4095s %31s %c", segment->start,
                       segment->finish, &segment->window, segment->trace, segment->wav, stop_list, segment->anchor, &extra);
            if (fields < 5 || fields > 7 ||
                (fields == 7 && strcmp(segment->anchor, "-") && strcmp(segment->anchor, "note_on")) ||
                (!strcmp(segment->anchor, "note_on") && !strcmp(stop_list, "-")) ||
                ((!strcmp(segment->finish, "-") && !strcmp(stop_list, "-") ? 1 : 0) != (segment->window > 0)) ||
                (strcmp(stop_list, "-") && (segment->window || strcmp(segment->finish, "-"))))
                fail("invalid audio segment declaration");
            if (strcmp(stop_list, "-")) {
                char *save;
                for (char *name = strtok_r(stop_list, ",", &save); name; name = strtok_r(NULL, ",", &save)) {
                    unsigned n = segment->stop_count++;
                    if (n == 32 || strlen(name) >= 128) fail("too many/invalid audio stop symbols");
                    strcpy(segment->stop_names[n], name);
                    segment->stops[n] = dlsym(RTLD_DEFAULT, name);
                    if (!segment->stops[n]) fail("missing audio stop symbol");
                }
            }
        }
        if (ferror(file) || fclose(file) || (audio_count < 1 || audio_count > 2)) fail("expected one or two audio segments");
    }
    if (config->mark) {
        mark_function = dlsym(RTLD_DEFAULT, config->mark);
        if (!mark_function) {
            fprintf(stderr, "missing native mark symbol: %s\n", config->mark);
            exit(1);
        }
    }
    if (config->checkpoints) {
        FILE *file = fopen(config->checkpoints, "r");
        if (!file) fail("cannot open checkpoints");
        char line[512], name[128], window_text[64], after[128], extra;
        while (fgets(line, sizeof(line), file)) {
            strcpy(after, "-");
            int fields = sscanf(line, "%127s %63s %127s %c", name, window_text, after, &extra);
            if (fields < 2 || fields > 3) fail("invalid checkpoint declaration");
            Checkpoint cp = {.occurrence = 1, .window = checkpoint_number(window_text, 10)};
            if (strcmp(after, "-")) {
                for (size_t i = 0; i < checkpoint_count; i++)
                    if (!strcmp(checkpoints[i].label, after)) cp.after = i + 1;
                if (!cp.after) fail("unknown checkpoint predecessor");
            }
            for (size_t i = 0; i < checkpoint_count; i++)
                if (!strcmp(checkpoints[i].label, name)) fail("duplicate checkpoint");
            strcpy(cp.label, name);
            char *suffix = strchr(name, '#');
            if (suffix) {
                unsigned n = checkpoint_number(suffix + 1, 10);
                if (!n) fail("invalid checkpoint occurrence");
                cp.occurrence = (unsigned)n;
                *suffix = 0;
            }
            cp.function = dlsym(RTLD_DEFAULT, name);
            if (!cp.function) {
                fprintf(stderr, "scene: missing native checkpoint symbol: %s\n", name);
                exit(1);
            }
            Checkpoint *next = realloc(checkpoints, (checkpoint_count + 1) * sizeof(*checkpoints));
            if (!next) fail("cannot allocate checkpoints");
            checkpoints = next;
            checkpoints[checkpoint_count++] = cp;
        }
        if (ferror(file) || fclose(file) || !checkpoint_count) fail("cannot read checkpoints");
    }
    if (config->inputs && checkpoint_count) {
        FILE *file = fopen(config->inputs, "r");
        if (!file) fail("cannot open inputs");
        char line[256], label[128], k_text[64], mask_text[64], extra;
        while (fgets(line, sizeof(line), file)) {
            if (sscanf(line, "%127s %63s %63s %c", label, k_text, mask_text, &extra) != 3)
                fail("invalid checkpoint input");
            unsigned k = checkpoint_number(k_text, 10), mask = checkpoint_number(mask_text, 16);
            if (mask > 65535) fail("invalid checkpoint input mask");
            size_t i;
            for (i = 0; i < checkpoint_count; i++) if (!strcmp(label, checkpoints[i].label)) break;
            if (i == checkpoint_count) fail("unknown input checkpoint");
            if (event_count && (i < events[event_count - 1].checkpoint ||
                (i == events[event_count - 1].checkpoint && k <= events[event_count - 1].k)))
                fail("checkpoint inputs must increase");
            Event *next = realloc(events, (event_count + 1) * sizeof(*events));
            if (!next) fail("cannot allocate inputs");
            events = next;
            events[event_count++] = (Event){.checkpoint = i, .k = k, .mask = mask};
        }
        if (ferror(file) || fclose(file)) fail("cannot read inputs");
    } else if (config->inputs) {
        input = fopen(config->inputs, "r");
        if (!input) fail("cannot open inputs");
        read_event();
    }
    if (!config->state_vars) return;
    FILE *file = fopen(config->state_vars, "r");
    if (!file) fail("cannot open state vars");
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *comment = strchr(line, '#');
        if (comment) *comment = 0;
        Variable var = { .count = 1 };
        char spec[128], extra;
        int fields = sscanf(line, "%127s %127s %c", var.name, spec, &extra);
        if (fields == EOF) continue;
        if (fields != 2) fail("invalid state variable declaration");
        char *bracket = strchr(spec, '[');
        if (bracket) {
            char *end;
            errno = 0;
            unsigned long count = strtoul(bracket + 1, &end, 10);
            if (errno || bracket[1] < '0' || bracket[1] > '9' || !count || count > PS1_RAM_SIZE ||
                strcmp(end, "]")) fail("invalid state variable count");
            var.count = (unsigned)count;
            *bracket = 0;
        }
        if (!strcmp(spec, "ptr") || !strcmp(spec, "u32") || !strcmp(spec, "s32")) var.width = 4;
        else if (!strcmp(spec, "u16") || !strcmp(spec, "s16")) var.width = 2;
        else if (!strcmp(spec, "u8") || !strcmp(spec, "s8")) var.width = 1;
        else fail("invalid state variable type");
        strcpy(var.type, spec);
        var.address = dlsym(RTLD_DEFAULT, var.name);
        if (!var.address) {
            fprintf(stderr, "scene: missing native symbol: %s\n", var.name);
            exit(1);
        }
        Dl_info info;
        const ElfW(Sym) *symbol;
        if (!dladdr1(var.address, &info, (void **)&symbol, RTLD_DL_SYMENT) ||
            !symbol || var.width * var.count > symbol->st_size)
            fail("state variable exceeds native symbol");
        Variable *next = realloc(variables, (variable_count + 1) * sizeof(*variables));
        if (!next) fail("cannot allocate state vars");
        variables = next;
        variables[variable_count++] = var;
    }
    if (ferror(file) || fclose(file)) fail("cannot read state vars");
    if (!variable_count) fail("empty state vars");
    trace = fopen(config->state_trace, "w");
    if (!trace) fail("cannot open state trace");
}
void port_scene_origin(void)
{
    if (!have_origin) { have_origin = 1; origin = clock_vblanks(); latch(); }
}
void port_scene_start(void)
{
    if (!have_mark) { have_mark = 1; mark = clock_vblanks(); rand_index = 0; }
}
static void pointer_value(uint32_t value)
{
    uintptr_t p = value;
    if (!value) { fputs("0", trace); return; }
    if (p >= (uintptr_t)Ps1_Ram && p - (uintptr_t)Ps1_Ram < PS1_RAM_SIZE) {
        fprintf(trace, "0x%08x", 0x80000000u | (unsigned)(p - (uintptr_t)Ps1_Ram));
        return;
    }
    Dl_info info;
    const ElfW(Sym) *symbol;
    if (dladdr1((void *)p, &info, (void **)&symbol, RTLD_DL_SYMENT) && info.dli_sname &&
        symbol && p - (uintptr_t)info.dli_saddr < symbol->st_size) {
        fprintf(trace, "%s+%u", info.dli_sname, (unsigned)(p - (uintptr_t)info.dli_saddr));
        return;
    }
    fail("unresolved native pointer");
}
void port_rand_trace(int value)
{
    if (trace && have_mark && !port_scene_complete())
        if (fprintf(trace, "%u rand %u %d\n", clock_vblanks() - mark, rand_index++, value) < 0 || fflush(trace))
            fail("cannot write rand trace");
}
static void snapshot(const char *label, unsigned relative)
{
    uint32_t k = clock_vblanks() - mark;
    for (size_t i = 0; i < variable_count; i++) {
        Variable *var = &variables[i];
        for (unsigned j = 0; j < var->count; j++) {
            uint32_t value = 0;
            memcpy(&value, (char *)var->address + j * var->width, var->width);
            if (label) fprintf(trace, "%s %u %u state %s[%u] ", label, relative, k, var->name, j);
            else fprintf(trace, "%u state %s[%u] ", k, var->name, j);
            if (!strcmp(var->type, "ptr")) pointer_value(value);
            else if (var->type[0] == 's') {
                int32_t signed_value = var->width == 1 ? (int8_t)value : var->width == 2 ? (int16_t)value : (int32_t)value;
                fprintf(trace, "%d", signed_value);
            } else fprintf(trace, "%u", value);
            fputc('\n', trace);
        }
    }
    if (fflush(trace)) fail("cannot write state trace");
}
void port_scene_tick(void)
{
    latch();
    if (audio_trace && audio_segments[audio_index].window &&
        clock_vblanks() - audio_segments[audio_index].origin >= audio_segments[audio_index].window)
        audio_end("vblanks");
    if (!trace || !have_mark) return;
    if (!checkpoint_count) { snapshot(NULL, 0); return; }
    for (size_t i = 0; i < checkpoint_count; i++) {
        Checkpoint *cp = &checkpoints[i];
        unsigned k = clock_vblanks() - cp->start;
        if (cp->reached && k && k <= cp->window) snapshot(cp->label, k);
    }
}
void port_scene_finish(void)
{
    if (trace && fclose(trace)) fail("cannot close state trace");
    trace = NULL;
}
#else
void port_scene_prepare(const PortCapture *config) { (void)config; }
void port_scene_origin(void) {}
void port_scene_start(void) {}
void port_scene_tick(void) {}
void port_scene_finish(void) {}
int port_scene_complete(void) { return 0; }
int port_scene_audio_complete(void) { return 0; }
void port_scene_enter(void *function) { (void)function; }
void port_scene_movie_timing(const char *event, unsigned value) { (void)event; (void)value; }
#endif
