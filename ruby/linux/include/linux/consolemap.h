/*
 * consolemap.h
 *
 * Interface between console.c, selection.c  and consolemap.c
 */
#define LAT1_MAP 0
#define GRAF_MAP 1
#define IBMPC_MAP 2
#define USER_MAP 3

struct vc_data;

extern unsigned short *get_acm(int m);
extern unsigned char inverse_translate(int m, int ucs);
extern u16 inverse_convert(struct vc_data *conp, int glyph);
extern void set_translate(struct vc_data *conp, int m);
extern int conv_uni_to_pc(struct vc_data *vc, long ucs);
