/* Software display -> RGBA texture, at 1x, on the game thread's OffscreenCanvas. The game thread
 * never returns to its event loop, so the browser would not compose that canvas, and an ImageBitmap
 * sent to another thread is never freed (Chromium releases it on the sender's loop): each frame is
 * read back to shared memory and the main thread paints the newest one (showFrames in
 * port/web/dev/index.html), so nothing can queue. */
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "../plat.h"
#include "../../backend/gpu.h"
#include "../../psyq/port.h"
#include "pace.h"
extern int plat_pattern, plat_validation;
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;
static uint32_t display[640 * 480];
static unsigned char rgba[640 * 480 * 4], actual[640 * 480 * 4];
/* Painted by showFrames at each animation frame: seq is odd while the frame is being written. */
static struct {
    _Atomic uint32_t seq;
    uint32_t width, height;
    unsigned char pixels[640 * 480 * 4];
} shown;
static GLuint texture, program;
static GLuint shader(GLenum type, const char *source)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok); PORT_ASSERT(ok);
    return s;
}
static void init(void)
{
    EmscriptenWebGLContextAttributes a;
    emscripten_webgl_init_context_attributes(&a);
    a.majorVersion = 2; a.alpha = 0; a.antialias = 0; a.preserveDrawingBuffer = plat_validation;
    context = emscripten_webgl_create_context("#canvas", &a); PORT_ASSERT(context > 0);
    PORT_ASSERT(emscripten_webgl_make_context_current(context) == EMSCRIPTEN_RESULT_SUCCESS);
    GLuint vs = shader(GL_VERTEX_SHADER, "#version 300 es\n"
        "out vec2 uv; void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
        "uv=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0,1);}");
    GLuint fs = shader(GL_FRAGMENT_SHADER, "#version 300 es\nprecision highp float;"
        "in vec2 uv;uniform sampler2D image;out vec4 color;void main(){color=texture(image,uv);}");
    program = glCreateProgram(); glAttachShader(program, vs); glAttachShader(program, fs);
    glLinkProgram(program); GLint ok; glGetProgramiv(program, GL_LINK_STATUS, &ok); PORT_ASSERT(ok);
    glDeleteShader(vs); glDeleteShader(fs); glUseProgram(program);
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glDisable(GL_DITHER);
}
EMSCRIPTEN_KEEPALIVE void *web_frame_address(void) { return &shown; }
/* actual holds the frame as glReadPixels returns it, bottom row first. */
static void show(int w, int h)
{
    atomic_fetch_add(&shown.seq, 1);
    shown.width = w; shown.height = h;
    for (int y = 0; y < h; y++) memcpy(&shown.pixels[4 * y * w], &actual[4 * (h - 1 - y) * w], 4 * w);
    atomic_fetch_add(&shown.seq, 1);
}
void plat_present(long frame)
{
    double start = emscripten_get_now();
    if (!context) init();
    if (plat_pattern) {
        static uint16_t pattern[320 * 240];
        for (int y = 0; y < 240; y++) for (int x = 0; x < 320; x++)
            pattern[y * 320 + x] = ((x + frame) & 31) | (((y + 2 * frame) & 31) << 5) | (((x + y + 3 * frame) & 31) << 10);
        gpu_load(0, 0, 320, 240, pattern);
        gpu_set_display(0, 0, 320, 240, 0); gpu_set_display_enable(1);
    }
    int w = 320, h = 240;
    if (!gpu_read_display(display, &w, &h)) {
        for (int i = 0; i < w * h; i++) display[i] = 0;
    }
    for (int i = 0; i < w * h; i++) {
        rgba[4*i] = display[i] >> 16; rgba[4*i+1] = display[i] >> 8;
        rgba[4*i+2] = display[i]; rgba[4*i+3] = 255;
    }
    PORT_ASSERT(emscripten_set_canvas_element_size("#canvas", w, h) == EMSCRIPTEN_RESULT_SUCCESS);
    glViewport(0, 0, w, h);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    double elapsed = emscripten_get_now() - start;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    if (!plat_validation) { show(w, h); web_pace(); return; }
    PORT_ASSERT(glGetError() == GL_NO_ERROR);
    int different = 0, first = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int i = y*w+x, j = (h-1-y)*w+x;
        if (actual[4*j] != rgba[4*i] || actual[4*j+1] != rgba[4*i+1] ||
            actual[4*j+2] != rgba[4*i+2] || actual[4*j+3] != rgba[4*i+3]) {
            if (first < 0) first = i;
            different++;
        }
    }
    MAIN_THREAD_EM_ASM({
        Module.presentations.push({frame:$0, width:$1, height:$2, different:$3, first:$4, ms:$5, pixels:Array.from(HEAPU8.subarray($6, $6 + $1 * $2 * 4))});
    }, (int)frame, w, h, different, first, elapsed, actual);
    show(w, h);
    web_pace();
}
