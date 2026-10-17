/* L1 fixtures derived independently from DW1 asm and the GTE fixed-point rules.
 * No original game data. SHA links are canonical offsets, so ASan and native agree. */
#include "../psyq.h"
#include "../ps1mem.h"
#include "../clock.h"
#include "../libgte.h"
#include "../../backend/gte.h"
#include "../../backend/gpu.h"
#include "../../host/pack/sha256.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures,record;
#define CHECK(c) do {if(!(c)) {fprintf(stderr,"FAIL libgs:%d %s\n",__LINE__,#c);failures++;}} while(0)
static MATRIX id={{{4096,0,0},{0,4096,0},{0,0,4096}},{0,0,0}};
static unsigned long tags[256],other[16],packets[4096],scratch[256];
static unsigned long tmd[128];
static GsOT ot={8,(GsOT_TAG *)tags,0,0,NULL};
static SVECTOR vertices[4]={{-64,-32,0,0},{64,-32,0,0},{-64,32,0,0},{64,32,0,0}};
static SVECTOR normals[4]={{4096,2048,1024,0},{2048,4096,1024,0},{1024,2048,4096,0},{4096,4096,4096,0}};
static uint32_t xy(int x,int y) {return (uint16_t)x|(uint32_t)(uint16_t)y<<16;}
static uint32_t word(const void *p) {uint32_t v;memcpy(&v,p,4);return v;}
static void put(void *p,uint32_t v) {memcpy(p,&v,4);}
static void sh(void *p,unsigned v) {unsigned short h=v;memcpy(p,&h,2);}
static void tables(void)
{
    const char *names[]={"rsin_tbl","SQRT","rcossin_tbl"};
    for(int t=0;t<3;t++) {
        size_t size;libgte_table(names[t],&size);unsigned short *data=calloc(1,size);CHECK(data);
        for(unsigned i=0;i<size/2;i++) {
            int v;
            if(t==0) v=lround(sin(i*6.283185307179586/4096)*4096);
            else if(t==1) v=(int)sqrt((64+i)*262144.0);
            else v=lround((i&1?cos((i/2)*6.283185307179586/4096):sin((i/2)*6.283185307179586/4096))*4096);
            data[i]=v;
        }
        CHECK(libgte_set_table(names[t],data,size)==0);free(data);
    }
}
static void setup(void)
{
    GsInitGraph(320,240,4,0,0);GsSetProjection(256);MATRIX m=id;m.t[2]=1024;GsSetLsMatrix(&m);
    SetLightMatrix(&id);
    for(int i=0;i<5;i++) gte_ctc2(GTE_LCM0+i,word((char *)&id+4*i));
    GsSetAmbient(0,0,0);GsClearOt(4,0,&ot);memset(packets,0,sizeof(packets));memset(scratch,0,sizeof(scratch));GsSetWorkBase((PACKET *)packets);
}
/* The game initializes precisely these 27 dispatch slots. */
static void dispatch(void)
{
    memset(&GsFCALL4,0,sizeof(GsFCALL4));
    GsFCALL4.f3[0][0]=GsTMDfastF3L;GsFCALL4.g3[0][0]=GsTMDfastG3L;
    GsFCALL4.f4[0][0]=GsTMDfastF4L;GsFCALL4.f4[0][2]=GsTMDfastF4NL;GsFCALL4.nf4[0]=GsTMDfastNF4;GsFCALL4.g4[0][0]=GsTMDfastG4L;
    GsFCALL4.tf3[0][0]=GsTMDfastTF3L;GsFCALL4.tf3[0][2]=GsTMDfastTF3NL;GsFCALL4.ntf3[0]=GsTMDfastTNF3;
    GsFCALL4.tf4[0][0]=GsTMDfastTF4L;GsFCALL4.tf4[0][2]=GsTMDfastTF4NL;GsFCALL4.ntf4[0]=GsTMDfastTNF4;
    GsFCALL4.tg3[0][0]=GsTMDfastTG3L;GsFCALL4.tg3[0][2]=GsTMDfastTG3NL;GsFCALL4.ntg3[0]=GsTMDfastTNG3;
    GsFCALL4.tg4[0][0]=GsTMDfastTG4L;GsFCALL4.tg4[0][2]=GsTMDfastTG4NL;GsFCALL4.ntg4[0]=GsTMDfastTNG4;
    GsFCALL4.tf3[1][2]=GsTMDdivTF3NL;GsFCALL4.ntf3[1]=GsTMDdivTNF3;
    GsFCALL4.tg3[1][2]=GsTMDdivTG3NL;GsFCALL4.ntg3[1]=GsTMDdivTNG3;
    GsFCALL4.tf4[1][0]=GsTMDdivTF4L;GsFCALL4.tf4[1][2]=GsTMDdivTF4NL;GsFCALL4.ntf4[1]=GsTMDdivTNF4;
    GsFCALL4.tg4[1][2]=GsTMDdivTG4NL;GsFCALL4.ntg4[1]=GsTMDdivTNG4;
}
static const struct Case {const char *name;unsigned code,mode,divide,size;} cases[]={
    {"F3L",0x20,0,0,16},{"G3L",0x30,0,0,20},{"F4L",0x28,0,0,20},
    {"F4NL",0x28,2,0,20},{"NF4",0x29,1,0,16},{"G4L",0x38,0,0,24},
    {"TF3L",0x24,0,0,24},{"TF3NL",0x24,2,0,24},{"TNF3",0x25,1,0,28},
    {"TF4L",0x2c,0,0,32},{"TF4NL",0x2c,2,0,32},{"TNF4",0x2d,1,0,32},
    {"TG3L",0x34,0,0,28},{"TG3NL",0x34,2,0,28},{"TNG3",0x35,1,0,36},
    {"TG4L",0x3c,0,0,36},{"TG4NL",0x3c,2,0,36},{"TNG4",0x3d,1,0,44},
    {"divTF3NL",0x24,2,1,24},{"divTNF3",0x25,1,1,28},{"divTG3NL",0x34,2,1,28},
    {"divTNG3",0x35,1,1,36},{"divTF4L",0x2c,0,1,32},{"divTF4NL",0x2c,2,1,32},
    {"divTNF4",0x2d,1,1,32},{"divTG4NL",0x3c,2,1,36},{"divTNG4",0x3d,1,1,44}
};
static const char *const hashes[27]={
    "26df65a74b23a029823b6ec35c2d742fe2f67d72af65c8975dfcde11e9163f8d",
    "4d5854435adb56a62edaaa3b70534b4e7e88358777a5a466f5d410c157154261",
    "6d49fc1150de6ccf14cecfaf3384575333fd62119a999a5bac3b3bd19d5404c6",
    "f69fb9fdba13dd69f6b6906e6160c0c5ba7dfdaa4c7d8efb197060b267b55f30",
    "e9ad7fa77defb372cca8b9c34ac8536c200ec80b6f3f8758640b532502e96bd6",
    "61ec48ce5472872538aafc91dceebc851c59bbfb6620c0e5fd271bbda0ddb30e",
    "eb1e503ec6332387eae0c8b9c6b6f90e94c33205541a440ad34ac2b5591ba412",
    "6f53590bb998cf1d9355beb4e501d9f0e27378916886daee565a03703b373245",
    "cb8699564a4bfc591e7b8325088cada55f7b0f94540a814f52798be1806ddb8e",
    "695965e38deca3fa87e397b073581b9271bbfeb2817571b95f1824e2f073925c",
    "2c4fc3ef9a62981bfda653d3ef0a382a4e04ab2325ba0bc4fc35abe0418e915c",
    "5ef7471c7c0fdd43540ddb81f16ddbd6b2071ed6b0cfded729d837f8dc1e1cef",
    "feb13b1abf5cd2755ebf1ec986170f08eae1eebe68be2a29c95a3b223a864b32",
    "576cbb79def05b513eab15b76cf76d9f2a884ce9a9d5f4fea630bfe5b296b955",
    "cf83b6f4cff6dd1a6b8659dd0727b98bdeda01df527f66bccef7dfaea32824f9",
    "65ab5c14ef2b46c1c69186e89b953cce23c42e8cbce816410930e0d7d01c7a05",
    "439c13dbdabc0fc6a5530a65999187a5c5034b46f527924eb54ba704c018283e",
    "124b79c55388945f2f76cb87369bb7712b254eb0619cb907049b5cb0a4c0563d",
    "c2b5c3108c47f61ab174e244022e23649a4200bae1283bae9df99b3af997c993",
    "4764a07dffcd303c7c608bb108ceda49294890af818e9b83f9b852551f3e5ff0",
    "8febbc20136ab6f6df1dba3da4626c40541da2e0b8ed110c935c788dfb25e224",
    "cd3f3ea026b6a7c40519fcbeb2efc877a6d5ec7b9b56aa4b1325aabef7e471c8",
    "43c108d2b1c1680dd1d329a23af82a2cb422f93e57857f3b195c7b5d0da6f3b4",
    "abb98dbbd99633a7b20b1c07a1013670aca3e18eae63a3a2ee07c5d22514e17d",
    "9642b59f2345df309a5951909124720df9aab4f0fff7da780c5802aa13141b79",
    "2beb9f4298f32bfd4fce46fd15d3ec0d6a1e54ca25d6662638de6ddd4f292ab0",
    "c0294d1ad2cd9228badae1a30b54c9f63a4712f8772f8047f833c45d5b9d010d",
};
/* A TMD uses offsets from the first object table, not from the magic or flags. */
static unsigned char *fixture(const struct Case *c,unsigned copies)
{
    memset(tmd,0,sizeof(tmd));tmd[0]=0x41;tmd[2]=1;unsigned long *o=tmd+3;
    o[0]=(uintptr_t)vertices-(uintptr_t)o;o[1]=4;o[2]=(uintptr_t)normals-(uintptr_t)o;o[3]=4;
    o[4]=28;o[5]=copies;unsigned char *p=(void *)(o+7);
    int n=(c->code&8)?4:3,g=!!(c->code&16),tex=c->code&4;
    for(unsigned k=0;k<copies;k++) {
        unsigned char *q=p+k*c->size;q[0]=0x7e;q[1]=0x7f;q[2]=c->mode==1?1:0;q[3]=c->code;
        unsigned index=tex?4+4*n:8;
        if(tex) for(int i=0;i<n;i++) put(q+4+4*i,(16+32*(i&1))|((32+32*(i>>1))<<8)|(i==0?0x1234u<<16:i==1?0x156u<<16:0));
        if(c->mode==1) {
            unsigned colors=tex?4+4*n:4;
            for(int i=0;i<(g?n:1);i++) put(q+colors+4*i,0x00302010u+0x00040404u*i+(!tex?(c->code&0xfeu)<<24:0));
            index=colors+4*(g?n:1);for(int i=0;i<n;i++) sh(q+index+2*i,i);
        } else {
            if(!tex) put(q+4,0x00804020u|(c->code<<24));
            for(int i=0;i<n;i++) {if(g) {sh(q+index+4*i,i);sh(q+index+4*i+2,i);}else sh(q+index+2+2*i,i);}
        }
    }
    GsMapModelingData(tmd+1);CHECK((tmd[1]&1)&&o[0]==(uintptr_t)vertices&&o[2]==(uintptr_t)normals&&o[4]==(uintptr_t)p);
    unsigned long copy[7];memcpy(copy,o,sizeof(copy));GsMapModelingData(tmd+1);CHECK(!memcmp(copy,o,sizeof(copy)));
    return p;
}
/* Canonical links: OT at 0, packet arena at 0x1000, END untouched. */
static uint32_t canonical(uint32_t tag)
{
    unsigned link=tag&0xffffffu;if(link==PS1_LINK_END) return tag;
    uintptr_t p=(uintptr_t)PS1_LINK_PTR(link);unsigned offset;
    if(p>=(uintptr_t)tags&&p<(uintptr_t)(tags+256)) offset=p-(uintptr_t)tags;
    else {CHECK(p>=(uintptr_t)packets&&p<(uintptr_t)(packets+4096));offset=0x1000+p-(uintptr_t)packets;}
    return (tag&0xff000000u)|offset;
}
static void digest(unsigned c,unsigned bytes)
{
    Sha256 s;uint8_t d[32];char hex[65];Sha256_Init(&s);
    for(unsigned off=0;off<bytes;) {
        unsigned long *p=packets+off/4;unsigned size=4+4*(*p>>24);uint32_t tag=canonical(*p);
        Sha256_Update(&s,&tag,4);Sha256_Update(&s,p+1,size-4);off+=size;
    }
    for(unsigned i=0;i<256;i++) {uint32_t tag=canonical(tags[i]);Sha256_Update(&s,&tag,4);}
    Sha256_Final(&s,d);for(int i=0;i<32;i++) sprintf(hex+2*i,"%02x",d[i]);
    if(record) printf("HASH %s %s\n",cases[c].name,hex);
    else CHECK(hashes[c]&&strcmp(hex,hashes[c])==0);
}
/* H=256, Z=1024: projected vertices are (+/-16,+/-8). AVSZ3 is 255,
 * AVSZ4 is 256; (OTZ - offset 4) >> shift 2 is bucket 62 / 63. */
static void pipeline(void)
{
    for(unsigned k=0;k<27;k++) {
        const struct Case *c=cases+k;setup();unsigned char *p=fixture(c,2);
        GsDOBJ2 obj={c->mode==2?64:0,NULL,NULL,0};if(c->divide) obj.attribute|=1<<9;
        GsLinkObject4((uintptr_t)(tmd+3),&obj,0);
        CHECK((p[0]|p[1]<<8)==2);
        uint64_t before=clock_now();GsSortObject4J(&obj,&ot,2,scratch);CHECK(clock_now()==before+CLOCK_PSYQ_ENTRY_CYCLES);
        int n=(c->code&8)?4:3,tex=!!(c->code&4),g=!!(c->code&16),outg=g&&(c->mode!=2||c->divide);
        unsigned len=(outg?2*n:n+1)+(tex?n:0),bytes=4*(len+1),total=bytes*2*(c->divide?4:1);
        CHECK(GsGetWorkBase()==(PACKET *)packets+total);
        unsigned index=n==3?62:63;CHECK(tags[index]==PS1_LINK((PACKET *)packets+total-bytes));
        unsigned command=tex?(c->code&0xfe):c->code&0xfe;
        if(tex&&g&&c->mode==2&&!c->divide) command=n==4?0x2d:0x25;
        unsigned color=tex?(c->mode==1?0x302010:0x808080):(c->mode==1?0x302010:0x804020);
        /* Light/color identity: NCCS multiplies each base channel by normal/4096. */
        if(c->mode==0) color=tex?0x204080:0x202020;
        if(!c->divide) {
            CHECK(packets[0]==((len<<24)|PS1_LINK(tags+index-1)));
            CHECK(packets[1]==(color|(command<<24)));
            unsigned off=2;
            for(int i=0;i<n;i++) {
                if(i&&outg) {
                    unsigned expected;
                    if(c->mode==1) expected=0x302010+0x40404*i;
                    else if(tex) expected=i==1?0x208040:i==2?0x804020:0x808080;
                    else expected=i==1?0x204010:i==2?0x802008:0x804020;
                    CHECK(packets[off++]==(expected|(c->mode==0?command<<24:0)));
                }
                CHECK(packets[off++]==xy(i&1?16:-16,i&2?8:-8));
                if(tex) CHECK(packets[off++]==word(p+4+4*i));
            }
        } else {
            /* First triangle: corner 1, midpoint 1-2, midpoint 0-1.
             * First quad: corner 0, midpoint 0-1, midpoint 0-2, centre. */
            CHECK(packets[2]==xy(n==3?16:-16,-8));
            unsigned stride=outg?3:2;
            CHECK(packets[2+stride]==xy(0,n==3?0:-8));
            CHECK(packets[2+2*stride]==xy(n==3?0:-16,n==3?-8:0));
            if(n==4) CHECK(packets[2+3*stride]==xy(0,0));
            CHECK((packets[3]>>16)==0x1234);
            CHECK((packets[3+stride]>>16)==0x156);
            static const int pos[9][2]={{-16,-8},{16,-8},{-16,8},{16,8},{0,-8},{-16,0},{0,0},{16,0},{0,8}};
            static const int uv[9][2]={{16,32},{48,32},{16,64},{48,64},{32,32},{16,48},{32,48},{48,48},{32,64}};
            static const int tri[4][3]={{1,6,4},{4,6,5},{0,4,5},{2,5,6}};
            static const int quad[4][4]={{0,4,5,6},{1,7,4,6},{2,5,8,6},{3,8,7,6}};
            static const unsigned rgb[9]={0x302010,0x342414,0x382818,0x3c2c1c,0x322212,0x342414,0x362616,0x382818,0x3a2a1a};
            unsigned opcode=c->code; /* div kernels preserve raw-texture bit 0 */
            unsigned long *previous=tags+index-1;
            for(int copy=0;copy<2;copy++) for(int poly=0;poly<4;poly++) {
                unsigned long *out=packets+(copy*4+poly)*bytes/4;
                CHECK(out[0]==((len<<24)|PS1_LINK(previous)));previous=out;
                unsigned off=1;
                for(int vertex=0;vertex<n;vertex++) {
                    int v=n==3?tri[poly][vertex]:quad[poly][vertex];
                    if(!vertex||outg) {
                        unsigned col=c->mode==1?(outg?rgb[v]:0x302010):color;
                        CHECK(out[off++]==(col|((!vertex?opcode:0)<<24)));
                    }
                    CHECK(out[off++]==xy(pos[v][0],pos[v][1]));
                    unsigned uvword=uv[v][0]|uv[v][1]<<8;
                    if(vertex==0) uvword|=0x12340000;
                    if(vertex==1) uvword|=0x01560000;
                    CHECK(out[off++]==uvword);
                }
            }
        }
        digest(k,total);
    }
}
static void clipping(void)
{
    setup();
    uint64_t before=clock_now();
    CHECK(GsTMDfastF3L(NULL,NULL,NULL,(PACKET *)packets,0,2,&ot,scratch)==(PACKET *)packets);
    CHECK(GsTMDdivTF4L(NULL,NULL,NULL,(PACKET *)packets,0,2,&ot,scratch)==(PACKET *)packets);
    CHECK(clock_now()==before);
    GsDOBJ2 obj={0};fixture(cases,1);GsLinkObject4((uintptr_t)(tmd+3),&obj,0);
    SVECTOR swap=vertices[1];vertices[1]=vertices[2];vertices[2]=swap;
    GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets);
    swap=vertices[1];vertices[1]=vertices[2];vertices[2]=swap;
    MATRIX m=id;m.t[2]=-1024;GsSetLsMatrix(&m);GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets);
    m.t[2]=1024;GsSetLsMatrix(&m);obj.attribute=0x80000000u;GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets);
    obj.attribute=0x40000000u;GsSortObject4J(&obj,&ot,2,scratch);CHECK(packets[1]==0x22202020u);
    /* Zero primitives, no speculative read past the TMD arrays. */
    tmd[8]=0;GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets+20);
    /* Two different groups must each dispatch once and use their own stride. */
    setup();fixture(cases,1);unsigned char *p=(void *)(tmd+10);unsigned char second[20]={0};
    second[3]=0x28;put(second+4,0x28804020);sh(second+10,0);sh(second+12,1);sh(second+14,2);sh(second+16,3);
    memcpy(p+16,second,20);tmd[8]=2;GsLinkObject4((uintptr_t)(tmd+3),&obj,0);obj.attribute=0;
    CHECK(word(p)==0x20000001&&word(p+16)==0x28000001);
    GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets+44);
}
/* Two subdivision levels produce 16 children per face. Recursion preserves the
 * initial OT bucket; the outer corner remains at its original projected position. */
static void deeper_subdivision(void)
{
    for(unsigned k=18;k<27;k++) {
        setup();fixture(cases+k,1);GsDOBJ2 obj={2<<9,NULL,NULL,0};
        if(cases[k].mode==2) obj.attribute|=64;
        GsLinkObject4((uintptr_t)(tmd+3),&obj,0);GsSortObject4J(&obj,&ot,2,scratch);
        unsigned n=cases[k].code&8?4:3,g=!!(cases[k].code&16);
        unsigned size=4*(1+(g?3*n:1+2*n));
        CHECK(GsGetWorkBase()==(PACKET *)packets+size*16);
        CHECK(tags[n==3?62:63]==PS1_LINK((PACKET *)packets+size*15));
        if(n==4) {CHECK(packets[2]==xy(-16,-8));CHECK(packets[2+(g?3:2)]==xy(-8,-8));}
        /* Entirely offscreen: the RCpoly screen bounds reject before emitting children. */
        GsSetWorkBase((PACKET *)packets);MATRIX m=id;m.t[0]=2000;m.t[2]=1024;GsSetLsMatrix(&m);
        GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets);
        /* H/2 test: all points at Z=128, H=256. Exactly H/2 survives. Z=127 is rejected. */
        m=id;m.t[2]=127;GsSetLsMatrix(&m);GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets);
    }
}
static void subdivision_clipping(void)
{
    setup();fixture(cases+18,1);
    SVECTOR crossing[3]={{-8,-8,64,0},{16,-8,160,0},{-8,16,160,0}};
    tmd[3]=(uintptr_t)crossing;GsSetLsMatrix(&id);
    /* RTPT reports divide overflow for the vertex nearer than H/2. */
    for(int i=0;i<3;i++) {
        gte_mtc2(GTE_VXY0+2*i,xy(crossing[i].vx,crossing[i].vy));
        gte_mtc2(GTE_VZ0+2*i,crossing[i].vz);
    }
    gte_command(GTE_CMD_RTPT);CHECK(gte_cfc2(GTE_FLAG)&0x80000000u);
    GsDOBJ2 obj={64|(2<<9),NULL,NULL,0};GsLinkObject4((uintptr_t)(tmd+3),&obj,0);
    GsSortObject4J(&obj,&ot,2,scratch);
    /* The near corner and its edge midpoints have Z=64,112,112: RCpoly
     * rejects that child; each of the other three produces four packets. */
    CHECK(GsGetWorkBase()==(PACKET *)packets+12*32);
    CHECK(tags[6]==PS1_LINK((PACKET *)packets+11*32));
    for(int i=0;i<12;i++) CHECK(packets[i*8+1]==0x24808080u);

    setup();unsigned char *p=fixture(cases+18,1);
    sh(p+20,2);sh(p+22,1); /* Reverse winding while retaining vertex 0. */
    p[2]=2;obj.attribute=64|(1<<9);GsLinkObject4((uintptr_t)(tmd+3),&obj,0);
    GsSortObject4J(&obj,&ot,2,scratch);
    CHECK(GsGetWorkBase()==(PACKET *)packets+4*32);
    GsClearOt(4,0,&ot);GsSetWorkBase((PACKET *)packets);p[2]=0;
    GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets);
}
/* Nodes of the parent OT list, walked from its tag to the end as DrawOTag does. */
static int count_nodes(void)
{
    int n=1;
    for(unsigned long *p=(unsigned long *)ot.tag;(*p&0xffffffu)!=PS1_LINK_END&&n<=4096;n++) p=PS1_LINK_PTR(*p&0xffffffu);
    return n;
}
static int reachable(unsigned long *first,int n)
{
    for(int i=0;i<n;i++) {
        unsigned long *p=(unsigned long *)ot.tag;
        for(int steps=0;p!=first+i&&(*p&0xffffffu)!=PS1_LINK_END&&steps<4096;steps++) p=PS1_LINK_PTR(*p&0xffffffu);
        if(p!=first+i) return 0;
    }
    return 1;
}
static void ordering(void)
{
    setup();GsOT child={4,(GsOT_TAG *)other,0,0,NULL};GsClearOt(2,9,&child);
    CHECK(child.offset==2&&child.point==9&&child.tag==(GsOT_TAG *)(other+15));CHECK(other[0]==PS1_LINK_END);
    for(int i=1;i<16;i++) CHECK(other[i]==PS1_LINK(other+i-1));
    CHECK(GsSortOt(&child,&ot)==&ot);CHECK(tags[5]==PS1_LINK(other+15));CHECK(other[0]==PS1_LINK(tags+4));
    CHECK(reachable(other,16)&&count_nodes()==256+16);
    /* A primitive at bucket 0 of the child stays drawn: it, not other[0], links to the parent. */
    setup();GsClearOt(0,8,&child);packets[0]=0x01000000;packets[1]=0;AddPrim(other,packets);
    CHECK(GsSortOt(&child,&ot)==&ot);CHECK(other[0]==PS1_LINK(packets));
    CHECK(packets[0]==(0x01000000u|PS1_LINK(tags+3)));CHECK(tags[4]==PS1_LINK(other+15));
    CHECK(reachable(other,16)&&reachable(packets,1)&&count_nodes()==256+16+1);
}
static void two_d(void)
{
    setup();GsSetOrign(7,-9);GsDefDispBuff(0,0,0,240); /* applies the new origin in GPU mode */
    GsBOXF box={0x50000000,10,20,30,40,17,34,51};GsSortBoxFill(&box,&ot,12);
    CHECK(packets[0]==(0x04000000u|PS1_LINK(tags+7)));CHECK(packets[1]==0xe1000220u);
    CHECK(packets[2]==0x62332211u&&packets[3]==xy(10,20)&&packets[4]==xy(30,40));
    /* GsOFSGTE carries origin in packets as well as GTE OFX/OFY. */
    GsInitGraph(320,240,0,0,0);GsSetOrign(7,-9);GsDefDispBuff(0,0,0,240);GsClearOt(4,0,&ot);GsSetWorkBase((PACKET *)packets);
    GsSortBoxFill(&box,&ot,12);CHECK(packets[3]==xy(17,251));
    GsSPRITE sprite={0,10,20,32,16,0x11,4,8,32,64,128,64,32,3,5,4096,4096,0};
    GsSetWorkBase((PACKET *)packets);GsSortFastSprite(&sprite,&ot,12);
    CHECK(packets[1]==0xe1000211u&&packets[2]==0x64204080u&&packets[3]==xy(17,251));
    CHECK(packets[4]==0x10020804u&&packets[5]==xy(32,16));
    GsSetWorkBase((PACKET *)packets);GsSortSprite(&sprite,&ot,12);CHECK(packets[3]==xy(14,246));
    /* Rotated and scaled quad: Rz(+90) in PsyQ is (x,y)->(-y,x). */
    setup();sprite.mx=sprite.my=0;sprite.scalex=8192;sprite.scaley=4096;sprite.rotate=1024*360;
    GsSortSprite(&sprite,&ot,12);CHECK((packets[0]>>24)==9);CHECK(packets[1]==0x2c204080u);
    CHECK(packets[2]==xy(10,20));CHECK(packets[4]==xy(10,84));CHECK(packets[6]==xy(-6,20));CHECK(packets[8]==xy(-6,84));
    CHECK(packets[3]==0x10020804u&&packets[5]==0x00110823u&&packets[7]==0x1704&&packets[9]==0x1723);
    GsClearOt(4,0,&ot);GsSetWorkBase((PACKET *)packets);sprite.mx=3;sprite.my=13;
    GsSortSprite(&sprite,&ot,12);
    /* Independently apply pivot, scale (2,1), then Rz(+90) and translation. */
    for(int i=0;i<4;i++) {
        int x=((i&1)?32:0)-3,y=((i&2)?16:0)-13;
        CHECK(packets[2+2*i]==xy(10-y,20+2*x));
    }
    /* Every polygon command layout accepted by GsSortPoly, with RGB/XY/UV sentinels. */
    for(unsigned cmd=0x20;cmd<=0x3c;cmd+=4) {
        unsigned long input[16]={0};input[1]=(cmd<<24)|0x302010;unsigned in=2;int n=cmd&8?4:3;
        for(int i=0;i<n;i++) {if(i&&(cmd&16)) input[in++]=0x12340000+i;input[in++]=xy(10+i,-20-i);if(cmd&4) input[in++]=0x56780000+i;}
        GsClearOt(4,0,&ot);GsSetWorkBase((PACKET *)packets);GsSortPoly(input,&ot,12);
        CHECK((packets[0]>>24)==in-1);CHECK(!memcmp(packets+1,input+1,(in-1)*4));
    }
    PACKET *before=GsGetWorkBase();sprite.attribute=0x80000000u;GsSortSprite(&sprite,&ot,12);GsSortFastSprite(&sprite,&ot,12);box.attribute=0x80000000u;GsSortBoxFill(&box,&ot,12);CHECK(GsGetWorkBase()==before);
}
static void matrices(void)
{
    setup();GsInit3D();CHECK(gte_cfc2(GTE_ZSF3)==341&&gte_cfc2(GTE_ZSF4)==256);
    GsCOORDINATE2 root={0},child={0};GsInitCoordinate2(NULL,&root);GsInitCoordinate2(&root,&child);
    CHECK(root.sub==&child&&child.super==&root&&child.flg==0&&!memcmp(&child.coord,&id,sizeof(id)));
    root.coord.t[0]=100;root.coord.t[1]=200;child.coord.t[0]=5;child.coord.t[2]=30;
    MATRIX w,s;GsGetLw(&child,&w);CHECK(w.t[0]==105&&w.t[1]==200&&w.t[2]==30&&child.flg==1);
    child.coord.t[0]=99;GsGetLw(&child,&w);CHECK(w.t[0]==105);child.flg=0;GsGetLw(&child,&w);CHECK(w.t[0]==199);
    GsVIEW2 view={id,NULL};view.view.t[1]=-50;CHECK(GsSetView2(&view)==0);
    GsGetLws(&child,&w,&s);CHECK(w.t[0]==199&&s.t[1]==150);MATRIX ls;GsGetLs(&child,&ls);CHECK(!memcmp(&ls,&s,sizeof(ls)));
    MATRIX a=id,b=id,c=id;a.t[0]=10;b.t[0]=20;GsMulCoord0(&a,&b,&c);CHECK(c.t[0]==30);GsMulCoord3(&a,&b);CHECK(a.t[0]==30);
    GsRVIEW2 rv={0,0,-1024,0,0,0,0,NULL};CHECK(GsSetRefView2(&rv)==0);CHECK(GsWSMATRIX.t[2]==1024);
    CHECK(GsWSMATRIX.m[0][0]==4096&&GsWSMATRIX.m[1][1]==4096&&GsWSMATRIX.m[2][2]==4096);
    rv.vpz=0;CHECK(GsSetRefView2(&rv)==1);
    /* Super coordinate inverse: world eye -1024 and parent translation +100 X. */
    rv.vpz=-1024;rv.super=&root;CHECK(GsSetRefView2(&rv)==0);CHECK(GsWSMATRIX.t[0]==-100&&GsWSMATRIX.t[1]==-200);
    view.super=&root;view.view=id;CHECK(GsSetView2(&view)==0);CHECK(GsWSMATRIX.t[0]==-100);
    /* Vertical look and large coordinates exercise zero horizontal length and scaling. */
    rv=(GsRVIEW2){0,0,0,0,1024,0,0,NULL};CHECK(GsSetRefView2(&rv)==0);CHECK(GsWSMATRIX.m[1][2]==-4096&&GsWSMATRIX.m[2][1]==4096);
    rv=(GsRVIEW2){0,0,-1048576,0,0,0,0,NULL};CHECK(GsSetRefView2(&rv)==0&&GsWSMATRIX.t[2]==1048576);
    GsSetProjection(777);CHECK(gte_cfc2(GTE_H)==777);GsSetLsMatrix(&c);CHECK(gte_cfc2(GTE_TRX)==30);
    GsF_LIGHT light={0,0,-4096,255,128,0};CHECK(GsSetFlatLight(0,&light)==0);CHECK(GsLIGHTWSMATRIX.m[0][2]==4096);
    CHECK(gte_cfc2(GTE_LCM0)==4096);CHECK((gte_cfc2(GTE_LCM0+1)>>16)==2056);
    GsSetLightMatrix(&id);CHECK((gte_cfc2(GTE_LLM0+1)&0xffff)==4096);
    light.vz=0;CHECK(GsSetFlatLight(1,&light)==-1);GsSetLightMode(0);GsSetLightMode(3);GsSetLightMode(0);
    GsSetAmbient(4095,2049,-17);CHECK(gte_cfc2(GTE_RBK)==4080&&gte_cfc2(GTE_GBK)==2048&&gte_cfc2(GTE_BBK)==0xffffffe0u);
    uint64_t before=clock_now();GsSetNearClip(12);CHECK(clock_now()==before+64);
}
static void frame_cache(void)
{
    setup();GsCOORDINATE2 root={0},parent={0},child={0};
    GsInitCoordinate2(NULL,&root);GsInitCoordinate2(&root,&parent);GsInitCoordinate2(&parent,&child);
    root.coord.t[0]=100;root.coord.t[1]=200;parent.coord.t[0]=5;child.coord.t[2]=30;
    GsVIEW2 view={id,NULL};view.view.t[1]=-50;CHECK(GsSetView2(&view)==0);
    MATRIX w,s,expected=id;GsGetLws(&child,&w,&s);
    CHECK(w.t[0]==105&&w.t[1]==200&&w.t[2]==30&&s.t[1]==150);
    for(int frame=2;frame<=3;frame++) {
        GsSwapDispBuff();root.coord.t[0]=100*frame;root.coord.t[1]=-40*frame;root.flg=0;
        expected.t[0]=100*frame+5;expected.t[1]=-40*frame;expected.t[2]=30;
        if(frame==2) GsGetLw(&child,&w);else GsGetLws(&child,&w,&s);
        CHECK(!memcmp(&w,&expected,sizeof(w))&&!memcmp(&child.workm,&expected,sizeof(w)));
        CHECK(root.flg==(unsigned)frame&&parent.flg==(unsigned)frame&&child.flg==(unsigned)frame);
        GsGetLws(&child,&w,&s);expected.t[1]-=50;
        CHECK(!memcmp(&s,&expected,sizeof(s)));
    }
}
static void rotated_light(void)
{
    setup();GsF_LIGHT light={-3,-4,0,255,128,64};CHECK(GsSetFlatLight(0,&light)==0);
    MATRIX bone={{{0,-4096,0},{4096,0,0},{0,0,4096}},{0,0,0}};
    GsSetLightMatrix(&bone);
    /* World row (2457,3276,0) times Rz(+90) is (3276,-2457,0).
     * All other light rows are zero. These are calculated without MulMatrix. */
    uint32_t llm[5]={xy(3276,-2457),0,0,0,0};
    for(int i=0;i<5;i++) CHECK(gte_cfc2(GTE_LLM0+i)==llm[i]);
    fixture(cases+6,1);GsDOBJ2 obj={0};GsLinkObject4((uintptr_t)(tmd+3),&obj,0);
    GsSortObject4J(&obj,&ot,2,scratch);CHECK(GsGetWorkBase()==(PACKET *)packets+32);
    /* Normal (4096,2048,1024), fixed-point light dot, colour matrix,
     * then modulation by texture neutral RGB (128,128,128). */
    int intensity=(3276*4096-2457*2048)>>12;
    unsigned rgb=0,channels[3]={255,128,64};
    for(int i=0;i<3;i++) {
        int lit=(((channels[i]*4096)/255)*intensity)>>12;
        rgb|=((128*lit)>>12)<<(8*i);
    }
    CHECK(rgb==0x10203f);CHECK(packets[1]==(0x24000000u|rgb));
}
static void tim(void)
{
    unsigned long input[]={8,16,0x00c80010,0x00010002,0x12345678,16,0x00000140,0x00010002,0x76543210};
    GsIMAGE image={0};GsGetTimInfo(input,&image);CHECK(image.pmode==8&&image.cx==16&&image.cy==200&&image.cw==2&&image.ch==1&&image.clut==input+4);
    CHECK(image.px==320&&image.py==0&&image.pw==2&&image.ph==1&&image.pixel==input+8);
    unsigned long no_clut[]={2,16,0x00030140,0x00050004,0};image.clut=input+4;GsGetTimInfo(no_clut,&image);
    CHECK(image.pmode==2&&image.pixel==no_clut+4&&image.px==320&&image.py==3&&image.pw==4&&image.ph==5);
    CHECK(image.clut==input+4); /* asm leaves absent CLUT fields untouched */
}
static void buffers(void)
{
    GsInitGraph(320,240,4,0,0);GsDefDispBuff(0,0,0,240);GsInit3D();CHECK(GsGetActiveBuff()==0);
    for(int i=0;i<2;i++) {
        GsClearOt(0,0,&ot);GsSortClear(i?0:255,0,i?255:0,&ot);GsDrawOt(&ot);DrawSync(0);
        CHECK(gpu_vram()[(i?240:0)*1024]==(i?0x7c00:0x001f));GsSwapDispBuff();CHECK(GsGetActiveBuff()==!i);
        static uint32_t display[320*240];int w,h;CHECK(gpu_read_display(display,&w,&h)&&w==320&&h==240);
        CHECK(display[0]==(i?0x000000ffu:0x00ff0000u));
    }
}
int main(int argc,char **argv)
{
    record=argc==2&&!strcmp(argv[1],"--record");clock_reset();tables();dispatch();pipeline();clipping();deeper_subdivision();subdivision_clipping();ordering();two_d();matrices();frame_cache();rotated_light();tim();buffers();
    printf("libgs: 27 TMD kernels, OT, 2D, matrices, utilities, double buffer; failures=%d\n",failures);return failures?1:0;
}
