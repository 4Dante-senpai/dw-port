/* libgs ABI, checked against mw_lib41 by psyq_prototypes.c. */
#ifndef PORT_GS_TYPES_H
#define PORT_GS_TYPES_H
#ifndef _LIBGS_H_
typedef unsigned char PACKET;
typedef struct {
	VECTOR  scale;
	SVECTOR rotate;
	VECTOR  trans;
}       GsCOORD2PARAM;
typedef struct _GsCOORDINATE2 {
	unsigned long flg;
	MATRIX  coord;
	MATRIX  workm;
	GsCOORD2PARAM *param;
	struct _GsCOORDINATE2 *super;
	struct _GsCOORDINATE2 *sub;
}       GsCOORDINATE2;
typedef struct {
	MATRIX  view;
	GsCOORDINATE2 *super;
}       GsVIEW2;
typedef struct {
	long    vpx, vpy, vpz;
	long    vrx, vry, vrz;
	long    rz;
	GsCOORDINATE2 *super;
}       GsRVIEW2;
typedef struct {
	int     vx, vy, vz;
	unsigned char r, g, b;
}       GsF_LIGHT;
typedef struct {
	unsigned p:24;
	unsigned char num:8;
}       GsOT_TAG;
typedef struct {
	unsigned long length;
	GsOT_TAG *org;
	unsigned long offset;
	unsigned long point;
	GsOT_TAG *tag;
}       GsOT;
typedef struct {
	unsigned long attribute;/* pers,trans,rotate,disp */
	GsCOORDINATE2 *coord2;	/* local dmatrix */
	unsigned long *tmd;
	unsigned long id;
}       GsDOBJ2;
typedef struct {
	unsigned long attribute;
	short   x, y;
	unsigned short w, h;
	unsigned short tpage;
	unsigned char u, v;
	short   cx, cy;
	unsigned char r, g, b;
	short   mx, my;
	short   scalex, scaley;
	long    rotate;
}       GsSPRITE;
typedef struct {
	unsigned long attribute;
	short   x, y;
	unsigned short w, h;
	unsigned char r, g, b;
}       GsBOXF;
typedef struct {
	unsigned long pmode;
	short   px, py;
	unsigned short pw, ph;
	unsigned long *pixel;
	short   cx, cy;
	unsigned short cw, ch;
	unsigned long *clut;
}       GsIMAGE;
typedef struct {
	PACKET
	* (*f3[2][3]) ();
	PACKET
	* (*nf3[2]) ();
	PACKET
	* (*g3[2][3]) ();
	PACKET
	* (*ng3[2]) ();
	PACKET
	* (*tf3[2][3]) ();
	PACKET
	* (*ntf3[2]) ();
	PACKET
	* (*tg3[2][3]) ();
	PACKET
	* (*ntg3[2]) ();
	PACKET
	* (*f4[2][3]) ();
	PACKET
	* (*nf4[2]) ();
	PACKET
	* (*g4[2][3]) ();
	PACKET
	* (*ng4[2]) ();
	PACKET
	* (*tf4[2][3]) ();
	PACKET
	* (*ntf4[2]) ();
	PACKET
	* (*tg4[2][3]) ();
	PACKET
	* (*ntg4[2]) ();
	PACKET
	* (*f3g[3])();
	PACKET
	* (*g3g[3])();
	PACKET
	* (*f4g[3])();
	PACKET
	* (*g4g[3])();
}       _GsFCALL;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  r0, g0, b0, code;
	unsigned short n0, v0;
	unsigned short v1, v2;
}       TMD_P_F3;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  r0, g0, b0, code;
	unsigned short n0, v0;
	unsigned short n1, v1;
	unsigned short n2, v2;
}       TMD_P_G3;
typedef struct {
	unsigned char	out, in, dummy, cd;
	unsigned char	r0, g0, b0, code;
	unsigned char	r1, g1, b1, dummy1;
	unsigned char	r2, g2, b2, dummy2;
	unsigned short n0, v0;
	unsigned short v1, v2;
}       TMD_P_F3G;
typedef struct {
	unsigned char	out, in, dummy, cd;
	unsigned char	r0, g0, b0, code;
	unsigned char	r1, g1, b1, dummy1;
	unsigned char	r2, g2, b2, dummy2;
	unsigned short n0, v0;
	unsigned short n1, v1;
	unsigned short n2, v2;
}       TMD_P_G3G;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  r0, g0, b0, code;
	unsigned short v0, v1;
	unsigned short v2, p;
}       TMD_P_NF3;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  r0, g0, b0, code;
	unsigned char  r1, g1, b1, p1;
	unsigned char  r2, g2, b2, p2;
	unsigned short v0, v1;
	unsigned short v2, p;
}       TMD_P_NG3;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  r0, g0, b0, code;
	unsigned short n0, v0;
	unsigned short v1, v2;
	unsigned short v3, p;
}       TMD_P_F4;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  r0, g0, b0, code;
	unsigned short n0, v0;
	unsigned short n1, v1;
	unsigned short n2, v2;
	unsigned short n3, v3;
}       TMD_P_G4;
typedef struct {
	unsigned char	out, in, dummy, cd;
	unsigned char	r0, g0, b0, code;
	unsigned char	r1, g1, b1, dummy1;
	unsigned char	r2, g2, b2, dummy2;
	unsigned char	r3, g3, b3, dummy3;
	unsigned short n0, v0;
	unsigned short v1, v2;
	unsigned short v3, dummy4;
}       TMD_P_F4G;
typedef struct {
	unsigned char	out, in, dummy, cd;
	unsigned char	r0, g0, b0, code;
	unsigned char	r1, g1, b1, dummy1;
	unsigned char	r2, g2, b2, dummy2;
	unsigned char	r3, g3, b3, dummy3;
	unsigned short n0, v0;
	unsigned short n1, v1;
	unsigned short n2, v2;
	unsigned short n3, v3;
}       TMD_P_G4G;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  r0, g0, b0, code;
	unsigned short v0, v1;
	unsigned short v2, v3;
}       TMD_P_NF4;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  r0, g0, b0, code;
	unsigned char  r1, g1, b1, p1;
	unsigned char  r2, g2, b2, p2;
	unsigned char  r3, g3, b3, p3;
	unsigned short v0, v1;
	unsigned short v2, v3;
}       TMD_P_NG4;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  tu0, tv0;
	unsigned short clut;
	unsigned char  tu1, tv1;
	unsigned short tpage;
	unsigned char  tu2, tv2;
	unsigned short p;
	unsigned short n0, v0;
	unsigned short v1, v2;
}       TMD_P_TF3;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  tu0, tv0;
	unsigned short clut;
	unsigned char  tu1, tv1;
	unsigned short tpage;
	unsigned char  tu2, tv2;
	unsigned short p;
	unsigned short n0, v0;
	unsigned short n1, v1;
	unsigned short n2, v2;
}       TMD_P_TG3;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  tu0, tv0;
	unsigned short clut;
	unsigned char  tu1, tv1;
	unsigned short tpage;
	unsigned char  tu2, tv2;
	unsigned short p0;
	unsigned char  r0, g0, b0, p1;
	unsigned short v0, v1;
	unsigned short v2, p2;
}       TMD_P_TNF3;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  tu0, tv0;
	unsigned short clut;
	unsigned char  tu1, tv1;
	unsigned short tpage;
	unsigned char  tu2, tv2;
	unsigned short p0;
	unsigned char  r0, g0, b0, p1;
	unsigned char  r1, g1, b1, p2;
	unsigned char  r2, g2, b2, p3;
	unsigned short v0, v1;
	unsigned short v2, p4;
}       TMD_P_TNG3;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  tu0, tv0;
	unsigned short clut;
	unsigned char  tu1, tv1;
	unsigned short tpage;
	unsigned char  tu2, tv2;
	unsigned short p0;
	unsigned char  tu3, tv3;
	unsigned short p1;
	unsigned short n0, v0;
	unsigned short v1, v2;
	unsigned short v3, p2;
}       TMD_P_TF4;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  tu0, tv0;
	unsigned short clut;
	unsigned char  tu1, tv1;
	unsigned short tpage;
	unsigned char  tu2, tv2;
	unsigned short p0;
	unsigned char  tu3, tv3;
	unsigned short p1;
	unsigned short n0, v0;
	unsigned short n1, v1;
	unsigned short n2, v2;
	unsigned short n3, v3;
}       TMD_P_TG4;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  tu0, tv0;
	unsigned short clut;
	unsigned char  tu1, tv1;
	unsigned short tpage;
	unsigned char  tu2, tv2;
	unsigned short p0;
	unsigned char  tu3, tv3;
	unsigned short p1;
	unsigned char  r0, g0, b0, p2;
	unsigned short v0, v1;
	unsigned short v2, v3;
}       TMD_P_TNF4;
typedef struct {
	unsigned char  out, in, dummy, cd;
	unsigned char  tu0, tv0;
	unsigned short clut;
	unsigned char  tu1, tv1;
	unsigned short tpage;
	unsigned char  tu2, tv2;
	unsigned short p0;
	unsigned char  tu3, tv3;
	unsigned short p1;
	unsigned char  r0, g0, b0, p2;
	unsigned char  r1, g1, b1, p3;
	unsigned char  r2, g2, b2, p4;
	unsigned char  r3, g3, b3, p5;
	unsigned short v0, v1;
	unsigned short v2, v3;
}       TMD_P_TNG4;
#define WORLD NULL
#define SCREEN ((GsCOORDINATE2 *)1)
#define GsDivMODE_NDIV 0
#define GsDivMODE_DIV 1
#define GsLMODE_NORMAL 0
#define GsLMODE_LOFF 2
#endif
extern MATRIX GsIDMATRIX, GsWSMATRIX, GsLIGHTWSMATRIX;
extern _GsFCALL GsFCALL4;
extern PACKET *GsOUT_PACKET_P;

_Static_assert(sizeof(GsCOORD2PARAM)==40 && sizeof(GsCOORDINATE2)==80 &&
               __builtin_offsetof(GsCOORDINATE2,coord)==4 && __builtin_offsetof(GsCOORDINATE2,workm)==36 &&
               __builtin_offsetof(GsCOORDINATE2,super)==72, "libgs coordinate ABI");
_Static_assert(sizeof(GsVIEW2)==36 && sizeof(GsRVIEW2)==32 && sizeof(GsF_LIGHT)==16,
               "libgs view and light ABI");
_Static_assert(sizeof(GsOT_TAG)==4 && sizeof(GsOT)==20 && __builtin_offsetof(GsOT,tag)==16 &&
               sizeof(GsDOBJ2)==16 && __builtin_offsetof(GsDOBJ2,tmd)==8, "libgs OT and object ABI");
_Static_assert(sizeof(GsSPRITE)==36 && __builtin_offsetof(GsSPRITE,mx)==24 &&
               __builtin_offsetof(GsSPRITE,rotate)==32 && sizeof(GsBOXF)==16 && sizeof(GsIMAGE)==28 &&
               __builtin_offsetof(GsIMAGE,pixel)==12 && __builtin_offsetof(GsIMAGE,clut)==24, "libgs 2D ABI");
_Static_assert(sizeof(_GsFCALL)==0x130 && __builtin_offsetof(_GsFCALL,tf3)==0x40 &&
               __builtin_offsetof(_GsFCALL,nf4)==0x98 && __builtin_offsetof(_GsFCALL,f3g)==0x100,
               "libgs function table ABI");
_Static_assert(sizeof(TMD_P_F3)==16, "TMD F3 ABI");
_Static_assert(sizeof(TMD_P_F4)==20, "TMD F4 ABI");
_Static_assert(sizeof(TMD_P_G3)==20, "TMD G3 ABI");
_Static_assert(sizeof(TMD_P_G4)==24, "TMD G4 ABI");
_Static_assert(sizeof(TMD_P_NF3)==16, "TMD NF3 ABI");
_Static_assert(sizeof(TMD_P_NF4)==16, "TMD NF4 ABI");
_Static_assert(sizeof(TMD_P_NG3)==24, "TMD NG3 ABI");
_Static_assert(sizeof(TMD_P_NG4)==28, "TMD NG4 ABI");
_Static_assert(sizeof(TMD_P_TF3)==24, "TMD TF3 ABI");
_Static_assert(sizeof(TMD_P_TF4)==32, "TMD TF4 ABI");
_Static_assert(sizeof(TMD_P_TG3)==28, "TMD TG3 ABI");
_Static_assert(sizeof(TMD_P_TG4)==36, "TMD TG4 ABI");
_Static_assert(sizeof(TMD_P_TNF3)==28, "TMD TNF3 ABI");
_Static_assert(sizeof(TMD_P_TNF4)==32, "TMD TNF4 ABI");
_Static_assert(sizeof(TMD_P_TNG3)==36, "TMD TNG3 ABI");
_Static_assert(sizeof(TMD_P_TNG4)==44, "TMD TNG4 ABI");


_Static_assert(sizeof(TMD_P_F3G)==24 && sizeof(TMD_P_G3G)==28 &&
               sizeof(TMD_P_F4G)==32 && sizeof(TMD_P_G4G)==36, "TMD gradation ABI");
_Static_assert(__builtin_offsetof(TMD_P_F3,n0)==8 && __builtin_offsetof(TMD_P_F4,v3)==16 &&
               __builtin_offsetof(TMD_P_TF4,n0)==20 && __builtin_offsetof(TMD_P_TG4,v3)==34 &&
               __builtin_offsetof(TMD_P_TNF3,v0)==20 && __builtin_offsetof(TMD_P_TNG4,v0)==36,
               "TMD vertex and normal offsets");

#endif
