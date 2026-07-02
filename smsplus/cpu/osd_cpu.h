/*******************************************************************************
*																			   *
*	Define size independent data types and operations.						   *
*																			   *
*   The following types must be supported by all platforms:					   *
*																			   *
*	UINT8  - Unsigned 8-bit Integer		INT8  - Signed 8-bit integer           *
*	UINT16 - Unsigned 16-bit Integer	INT16 - Signed 16-bit integer          *
*	UINT32 - Unsigned 32-bit Integer	INT32 - Signed 32-bit integer          *
*	UINT64 - Unsigned 64-bit Integer	INT64 - Signed 64-bit integer          *
*																			   *
*******************************************************************************/


#ifndef OSD_CPU_H
#define OSD_CPU_H

#ifndef DOS
// #include "basetsd.h"

#ifndef __BASETSD_STUB__
#define __BASETSD_STUB__

typedef signed char    INT8;
typedef unsigned char  UINT8;
typedef signed short   INT16;
typedef unsigned short UINT16;
typedef signed int     INT32;
typedef unsigned int   UINT32;

#endif /* __BASETSD_STUB__ */
#endif /* !DOS */

#ifdef DOS
typedef unsigned char						UINT8;
typedef unsigned short						UINT16;
typedef unsigned int                        UINT32;
__extension__ typedef unsigned long long    UINT64;
typedef signed char 						INT8;
typedef signed short						INT16;
typedef signed int                          INT32;
__extension__ typedef signed long long      INT64;
#endif /* DOS */

/* Combine two 32-bit integers into a 64-bit integer */
#define COMBINE_64_32_32(A,B)     ((((UINT64)(A))<<32) | (UINT32)(B))
#define COMBINE_U64_U32_U32(A,B)  COMBINE_64_32_32(A,B)

/* Return upper 32 bits of a 64-bit integer */
#define HI32_32_64(A)		  (((UINT64)(A)) >> 32)
#define HI32_U32_U64(A)		  HI32_32_64(A)

/* Return lower 32 bits of a 64-bit integer */
#define LO32_32_64(A)		  ((A) & 0xffffffff)
#define LO32_U32_U64(A)		  LO32_32_64(A)

#define DIV_64_64_32(A,B)	  ((A)/(B))
#define DIV_U64_U64_U32(A,B)  ((A)/(UINT32)(B))

#define MOD_32_64_32(A,B)	  ((A)%(B))
#define MOD_U32_U64_U32(A,B)  ((A)%(UINT32)(B))

#define MUL_64_32_32(A,B)	  ((A)*(INT64)(B))
#define MUL_U64_U32_U32(A,B)  ((A)*(UINT64)(UINT32)(B))


/******************************************************************************
 * Union of UINT8, UINT16 and UINT32 in native endianess of the target
 ******************************************************************************/
#ifndef LSB_FIRST
#define LSB_FIRST 1
#endif

typedef union {
#ifdef LSB_FIRST
	struct { UINT8 l,h,h2,h3; } b;
	struct { UINT16 l,h; } w;
#else
	struct { UINT8 h3,h2,h,l; } b;
	struct { UINT16 h,l; } w;
#endif
	UINT32 d;
}	PAIR;

#endif	/* OSD_CPU_H */