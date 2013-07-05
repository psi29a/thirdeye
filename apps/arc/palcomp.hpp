//
//  AESOP palette compiler class
//

#ifndef PALCOMP_H
#define PALCOMP_H

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_RNGBEG 0
#define PAL_RNGEND 1

typedef struct {
	IDR_class *IDR;
	WORD parms[2];
} PAL_class;

PAL_class *PAL_construct(IDR_class *IDR);
void PAL_destroy(PAL_class *PAL);

void PAL_compile(PAL_class *PAL);

#ifdef __cplusplus
}
#endif

#endif
