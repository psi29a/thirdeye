//
//  AESOP Cartesian map compiler class
//

#ifndef MAPCOMP_H
#define MAPCOMP_H

#ifdef __cplusplus
extern "C" {
#endif

#define MSP_XSIZE 0
#define MSP_YSIZE 1
#define MSP_ORGX  2
#define MSP_ORGY  3
#define MSP_CELLX 4
#define MSP_CELLY 5

typedef struct
{
   IDR_class *IDR;
   WORD parms[6];
}
MAP_class;

MAP_class *MAP_construct(IDR_class *IDR);
void MAP_destroy(MAP_class *MAP);

void MAP_compile(MAP_class *MAP);

#ifdef __cplusplus
}
#endif

#endif
