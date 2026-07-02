
#ifndef __TYPES_H_
#define __TYPES_H_

/* If Frodo's sysdeps.h already defined the fixed-width types (C++ side),
   do NOT redefine them here -- sysdeps.h uses uint32 = unsigned int while
   this file historically used unsigned long, and redefining would be a
   conflicting typedef in C++.  On the pure-C engine side _FRODO_TYPES_DONE
   is not set, so the engine keeps its original types. */
#ifndef _FRODO_TYPES_DONE

typedef unsigned char uint8;
typedef unsigned short int uint16;
typedef unsigned long int uint32;

typedef signed char int8;
typedef signed short int int16;
typedef signed long int int32;

#endif /* _FRODO_TYPES_DONE */

#endif /* __TYPES_H_ */

