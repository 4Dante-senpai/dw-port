/* Exact pixel expectations from psx-spx GPU rules (2026-10-16):
 * https://psx-spx.consoledev.net/graphicsprocessingunitgpu/
 * Constants below are independent of the adapted renderer. Full-VRAM SHA-256
 * baselines were frozen only after these pixel assertions passed. */
#include "../gpu.h"
#include "../../host/pack/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *scenario;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %s:%d [%s] %s\n",__FILE__,__LINE__,scenario,#c); failures++; } } while (0)
static uint32_t xy(int x,int y) { return (uint16_t)x | ((uint32_t)(uint16_t)y<<16); }
static uint16_t pixel(int x,int y) { return gpu_vram()[y*1024+x]; }
static void cmd(uint32_t v) { gpu_gp0(&v,1); }
static void reset(const char *name)
{
    scenario=name; gpu_reset(1); gpu_fill(0,0,1024,512,0);
}
static void hash(void)
{
    static const struct { const char *name, *digest; } baselines[] = {
        {"GT3-interpolation", "8906980db9a995808ab1d7a0770fc9bf4c19d18b6bc231250835b7e267c5379d"},
        {"GT4-interpolation", "82dfca3ebbe48cf69fb0742fec478a53fbf59901954a81d8ee24d43cc0d4480a"},
        {"interpolation", "f935048fb8ffe907d5302d2e169941ed1cea4fb746922ad06855d10acc1a32fd"},
        {"F3", "e276f828841d894c6534f474ee29e1191e44d2f5869a5628bcab1f993ea9f438"},
        {"F4", "1d0de764d4708c094ae4cdaa1defb6af584134274830e715f2e00605cd1b5c2a"},
        {"G3", "e276f828841d894c6534f474ee29e1191e44d2f5869a5628bcab1f993ea9f438"},
        {"G4", "1d0de764d4708c094ae4cdaa1defb6af584134274830e715f2e00605cd1b5c2a"},
        {"FT3", "4ccde4f0ba1357529b32254f69d838b1b36277cf0820f8c2cc33731faf344ea9"},
        {"FT4", "5d1d0931db4f7c0fe721b245e0eb6c49f29f52dd8140bcf9515e29b4e46617ac"},
        {"GT3", "4ccde4f0ba1357529b32254f69d838b1b36277cf0820f8c2cc33731faf344ea9"},
        {"GT4", "5d1d0931db4f7c0fe721b245e0eb6c49f29f52dd8140bcf9515e29b4e46617ac"},
        {"tile", "ab40744895b26195ab58bd8243b04d45a35f2249a5bffc0e7467704b16f3639b"},
        {"sprite", "4d95a5c1a04f03ee1d63f92bf0215e8ff75a74d2144fd758f49a566274b05db3"},
        {"F2-line", "bd64aa45fa47fbb14b5e8558c6bda3c9cfd5a624d9494205591c81fdb2772102"},
        {"F3-line", "009bc5f0dabdbbb47b7a6cb093b8c602b0b8c461cad936521bf3dd8886417b19"},
        {"F4-line", "c26f51d30d05be29e33100c1f39260cc325df8ed417e775d56aa20af2ea8e604"},
        {"fill", "87b6addf24083cae4f815c647f68b434a9c7fa488a5cb68f7ff2959f25b7a654"},
        {"blend0", "e52718763d0e293f94f04cefc97cad09d0184b2e2a9b89f4f09778e16f7a53bf"},
        {"blend1", "153eca0119e92bf60c921ee8c5b15bdeacd772a315cc65180cb99818811401cc"},
        {"blend2", "78962d013d3b40edc4a08edbd9ba6b7442141b8a88ecca966ed0a13452ec464d"},
        {"blend3", "709f4ddbe9282f1c25f737b1baf91de8a5072deb4a6dd0f11d0a927c5717a454"},
        {"dither-off", "162dec402c822cf61926ec614decc231d7de8f8b3e74bc23569ecaa9e951f97e"},
        {"dither-on", "43c9090e5839800417f3a498024715549e89164d6ceb76cb071fc5dd46826419"},
        {"clut4", "8a8b69f1095fc350848d846c459e9553deda8487e535e8e16cb25d4df33ee630"},
        {"clut8", "46abbd95b3419c7789c31768a9ad643a981335f85aa247f96f2027faa159d15d"},
        {"texture-window", "67d4dea7887c0023c71341e52916b1e8deee59a9a80e829cf87cf5d9859f57b8"},
        {"mask", "9ce4ffb668886dab953cfdabb4e01069ecab0681885884fccdad4d4f32033257"},
        {"transfers", "11cd8e8986c9ac169a9f91f5d2e1dab912164e30e6b39e635739792fed6f8999"},
        {"gp0-transfer", "e7ab4197d15c1f7e6251e7f5ecdf1e39e2537a957986bbe0d6f361d4f7b5d098"},
        {"clip-offset", "2f3fe8e5c1a12da6e4e7877af762a033d494a6cc2491abce2ad2bf5b25709977"},
    };
    const char *expected = NULL;
    for (unsigned i=0;i<sizeof(baselines)/sizeof(baselines[0]);i++)
        if (!strcmp(scenario,baselines[i].name)) expected=baselines[i].digest;
    Sha256 sha; uint8_t digest[32]; char hex[65];
    Sha256_Init(&sha); Sha256_Update(&sha,gpu_vram(),1024*512*2); Sha256_Final(&sha,digest);
    for (int i=0;i<32;i++) sprintf(hex+2*i,"%02x",digest[i]);
    if (getenv("DW1_GPU_RECORD_HASHES")) printf("HASH %s %s\n",scenario,hex);
    else CHECK(expected && strcmp(hex,expected)==0);
}
static void tile(int x,int y,int w,int h,uint32_t color,int semi)
{
    uint32_t p[]={ (semi?0x62000000u:0x60000000u)|color,xy(x,y),xy(w,h)}; gpu_gp0(p,3);
}
static void primitives(void)
{
    const int ops[]={0x20,0x28,0x30,0x38,0x24,0x2c,0x34,0x3c};
    const char *names[]={"F3","F4","G3","G4","FT3","FT4","GT3","GT4"};
    for (int j=0;j<8;j++) {
        reset(names[j]); int op=ops[j], n=(op&8)?4:3, idx=1;
        uint32_t p[12]={((uint32_t)op<<24)|0x808080};
        int x[]={8,16,8,16},y[]={8,8,16,16};
        if(op&4) { uint16_t tex[64]; for(int i=0;i<64;i++) tex[i]=0x4210; gpu_load(256,0,8,8,tex); }
        for(int k=0;k<n;k++) {
            if(k && (op&16)) p[idx++]=0x808080;
            p[idx++]=xy(x[k],y[k]);
            if(op&4) p[idx++]=xy(x[k]-8,y[k]-8)| (k==1?0x1040000u:0);
        }
        GpuWork work=gpu_gp0(p,idx); CHECK(work.words==(unsigned)idx); CHECK(work.pixels>0);
        CHECK(pixel(9,9)==0x4210);
        CHECK(pixel(7,9)==0); CHECK(pixel(16,16)==0);
        CHECK(pixel(15,15)==((op&8)?0x4210:0)); hash();
    }
    reset("tile"); tile(7,9,3,2,0xff,0); CHECK(pixel(7,9)==31); CHECK(pixel(9,10)==31); CHECK(pixel(10,10)==0); hash();
    reset("sprite"); uint16_t tex[]={31,0,0x83e0,0x7c00}; gpu_load(256,0,4,1,tex); cmd(0xe1000104);
    uint32_t spr[]={0x65808080,xy(8,8),0,xy(4,1)}; gpu_gp0(spr,4);
    CHECK(pixel(8,8)==31); CHECK(pixel(9,8)==0); CHECK(pixel(10,8)==0x83e0); CHECK(pixel(11,8)==0x7c00); hash();
    for(int n=2;n<=4;n++) {
        const char *names2[]={"F2-line","F3-line","F4-line"}; reset(names2[n-2]);
        uint32_t p[]={n==2?0x400000ff:0x480000ff,xy(4,4),xy(8,4),xy(8,8),xy(4,8),0x55555555};
        if(n>2) p[n+1]=0x55555555;
        gpu_gp0(p,n==2?3:n+2); CHECK(pixel(4,4)==31); CHECK(pixel(8,4)==31);
        CHECK(pixel(8,8)==(n>2?31:0)); CHECK(pixel(4,8)==(n>3?31:0)); hash();
    }
    reset("fill"); uint32_t fill[]={0x020000ff,xy(31,511),xy(17,2)}; gpu_gp0(fill,3);
    CHECK(pixel(16,511)==31); CHECK(pixel(47,0)==31); CHECK(pixel(48,0)==0); hash();
}
static void interpolation(void)
{
    reset("interpolation");
    /* At (10,10), triangle barycentrics are 1/2,1/4,1/4.
     * (248,0,0),(0,248,0),(0,0,248) -> RGB5 (15,7,7). */
    uint32_t g3[]={0x300000f8,xy(8,8),0x0000f800,xy(16,8),0x00f80000,xy(8,16)};
    gpu_gp0(g3,6); CHECK(pixel(10,10)==(15|(7<<5)|(7<<10)));
    /* UV varies along X/Y; neutral modulation leaves the fetched RGB5 intact. */
    uint16_t tex[64]; for(int i=0;i<64;i++) tex[i]=(uint16_t)(i+1);
    gpu_load(256,0,8,8,tex);
    uint32_t ft3[]={0x24808080,xy(24,8),0,xy(32,8),0x01040008,xy(24,16),0x00000800};
    gpu_gp0(ft3,7); CHECK(pixel(26,10)==19); CHECK(pixel(27,9)==12);
    hash();
}
static void joint_interpolation(void)
{
    for(int quad=0;quad<=1;quad++) {
        reset(quad?"GT4-interpolation":"GT3-interpolation");
        uint16_t tex[64];
        for(int v=0;v<8;v++) for(int u=0;u<8;u++)
            tex[v*8+u]=(uint16_t)((8+u)|((8+v)<<5)|((8+u+v)<<10));
        gpu_load(256,0,8,8,tex);
        uint32_t p[]={quad?0x3c404040:0x34404040,xy(8,8),0,
                      0x008040c0,xy(16,8),0x01040008,
                      0x0080c040,xy(8,16),0x00000800,
                      0x00e0a080,xy(16,16),0x00000808};
        gpu_gp0(p,quad?12:9);
        /* Vertices RGB8: (64,64,64), (192,64,128), (64,192,128),
         * (128,160,224). UVs: (0,0), (8,0), (0,8), (8,8).
         * At (10,10), weights (1/2,1/4,1/4) give RGB8 (96,96,96),
         * UV (2,2), texel RGB5 (10,10,12). floor(texel*colour/128)
         * gives (7,7,9), with dithering disabled.
         * At (11,9), weights (1/2,3/8,1/8) give RGB8 (112,80,96),
         * UV (3,1), texel (11,9,12), hence RGB5 (9,5,9). */
        CHECK(pixel(10,10)==(7|(7<<5)|(9<<10)));
        CHECK(pixel(11,9)==(9|(5<<5)|(9<<10)));
        if(quad) {
            /* Second triangle at (14,12): vertex 1/2/3 weights (1/2,1/4,1/4)
             * give RGB8 (144,120,152), UV (6,4), texel (14,12,18).
             * floor((2016,1440,2736)/128) = RGB5 (15,11,21). */
            CHECK(pixel(14,12)==(15|(11<<5)|(21<<10)));
        }
        hash();
    }
}
static void blending(void)
{
    /* Background channels (10,20,30), foreground (6,12,24). */
    const uint16_t expected[]={8|(16<<5)|(27<<10),16|(31<<5)|(31<<10),4|(8<<5)|(6<<10),11|(23<<5)|(31<<10)};
    const char *names[]={"blend0","blend1","blend2","blend3"};
    for(int mode=0;mode<4;mode++) {
        reset(names[mode]); gpu_fill(0,0,32,32,0xf0a050); cmd(0xe1000000u|((unsigned)mode<<5));
        tile(4,4,1,1,0xc06030,1); CHECK(pixel(4,4)==expected[mode]);
        uint16_t tex[]={6|(12<<5)|(24<<10),0x8000|6|(12<<5)|(24<<10)};
        gpu_load(256,0,2,1,tex); cmd(0xe1000104u|((unsigned)mode<<5));
        uint32_t p[]={0x67808080,xy(5,4),0,xy(2,1)}; gpu_gp0(p,4);
        CHECK(pixel(5,4)==tex[0]); CHECK(pixel(6,4)==(expected[mode]|0x8000)); hash();
    }
}
static void dithering(void)
{
    static const int d[4][4]={{-4,0,-3,1},{2,-2,3,-1},{-3,1,-4,0},{3,-1,2,-2}};
    for(int on=0;on<=1;on++) {
        reset(on?"dither-on":"dither-off"); cmd(0xe1000000u|(on?512:0));
        uint32_t p[]={0x38808080,xy(8,8),0x808080,xy(12,8),0x808080,xy(8,12),0x808080,xy(12,12)};
        gpu_gp0(p,8);
        for(int y=0;y<4;y++) for(int x=0;x<4;x++) {
            int c=(128+(on?d[y][x]:0))/8; CHECK(pixel(8+x,8+y)==(c|(c<<5)|(c<<10)));
        }
        hash();
    }
}
static void textures(void)
{
    for(int mode=0;mode<2;mode++) {
        reset(mode?"clut8":"clut4"); uint16_t packed=mode?0x0201:0x0021;
        uint16_t colors[]={0,31,0x3e0}; gpu_load(256,0,1,1,&packed); gpu_load(0,256,3,1,colors);
        cmd(0xe1000004u|((unsigned)mode<<7));
        uint32_t p[]={0x65808080,xy(8,8),0x40000000,xy(2,1)}; gpu_gp0(p,4);
        CHECK(pixel(8,8)==31); CHECK(pixel(9,8)==0x3e0); hash();
    }
    reset("texture-window"); uint16_t tex[16]; for(int i=0;i<16;i++)tex[i]=(uint16_t)(i+1);
    gpu_load(256,0,16,1,tex); cmd(0xe1000104); cmd(0xe2000401); /* mask X=8, offset X=8 */
    uint32_t p[]={0x65808080,xy(8,8),0,xy(16,1)}; gpu_gp0(p,4);
    for(int i=0;i<16;i++) CHECK(pixel(8+i,8)==(9+(i&7))); hash();
}
static void masks_and_transfers(void)
{
    reset("mask"); cmd(0xe6000001); tile(1,1,1,1,0xff,0); CHECK(pixel(1,1)==0x801f);
    cmd(0xe6000002); tile(1,1,1,1,0xff00,0); CHECK(pixel(1,1)==0x801f);
    uint16_t v=0x3e0; CHECK(gpu_load(1,1,1,1,&v).pixels==0); CHECK(pixel(1,1)==0x801f);
    gpu_load(2,1,1,1,&v); CHECK(gpu_move(2,1,1,1,1,1).pixels==0); CHECK(pixel(1,1)==0x801f);
    gpu_fill(1,1,1,1,0xff00); CHECK(pixel(1,1)==0x3e0); hash();
    reset("transfers"); uint16_t src[12],dst[12]; for(int i=0;i<12;i++) src[i]=(uint16_t)(i+1);
    GpuWork w=gpu_load(1022,511,4,3,src); CHECK(w.words==6 && w.pixels==12);
    w=gpu_store(1022,511,4,3,dst); CHECK(w.words==6 && w.pixels==0); CHECK(!memcmp(src,dst,sizeof(src)));
    CHECK(pixel(0,0)==7); gpu_load(10,10,4,3,src); gpu_move(10,10,11,11,4,3); gpu_store(11,11,4,3,dst);
    CHECK(!memcmp(src,dst,sizeof(src))); hash();
    reset("gp0-transfer"); uint32_t p[]={0xa0000000,xy(1023,511),xy(3,1),0x00020001,0x00000003};
    w=gpu_gp0(p,5); CHECK(w.words==5 && w.pixels==3); CHECK(pixel(1023,511)==1);CHECK(pixel(0,511)==2);CHECK(pixel(1,511)==3);
    uint32_t move[]={0x80000000,xy(1023,511),xy(5,5),xy(3,1)}; gpu_gp0(move,4);CHECK(pixel(7,5)==3); hash();
}
static void clipping_and_display(void)
{
    reset("clip-offset");cmd(0xe3000000|4|(4<<10));cmd(0xe4000000|7|(7<<10));cmd(0xe5000000|4|(4<<11));
    tile(0,0,8,8,0xff,0);CHECK(pixel(4,4)==31);CHECK(pixel(7,7)==31);CHECK(pixel(8,7)==0);hash();
    uint32_t out[4]; int w=0,h=0; gpu_set_display(4,4,2,2,0); CHECK(!gpu_read_display(out,&w,&h));
    gpu_set_display_enable(1); CHECK(gpu_read_display(out,&w,&h));CHECK(w==2&&h==2&&out[0]==0xff0000);
    gpu_reset(0);CHECK(gpu_read_display(out,&w,&h));CHECK(pixel(4,4)==31);
    uint16_t packed[]={0x2211,0x4433,0x6655};gpu_load(0,0,3,1,packed);gpu_set_display(0,0,2,1,1);
    CHECK(gpu_read_display(out,&w,&h));CHECK(out[0]==0x112233&&out[1]==0x445566);
    uint32_t truncated[]={0x20808080}; CHECK(gpu_gp0(truncated,1).pixels==0);
    uint32_t ignored[]={0xc0000000,0,xy(1,1)};CHECK(gpu_gp0(ignored,3).pixels==0);
}
int main(void)
{
    primitives(); interpolation(); joint_interpolation(); blending(); dithering(); textures(); masks_and_transfers(); clipping_and_display();
    if(failures) fprintf(stderr,"gpu: %d failures\n",failures); else puts("gpu: exact pixels and VRAM hashes passed");
    return failures?1:0;
}
