#ifndef _LOADROM_H_
#define _LOADROM_H_

/* Function prototypes */
int load_rom(char *filename);
unsigned char *loadzip(char *archive, char *filename, int *filesize);

/* Global data -- moved to heap state struct (EKA1). */
#ifndef SMS_STATE_DEFINING
#include "sms_state.h"
#endif

#endif /* _LOADROM_H_ */

