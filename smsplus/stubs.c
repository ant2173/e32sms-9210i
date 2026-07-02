#include <string.h>
#include <ctype.h>

int stricmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        int ca = tolower(*a++);
        int cb = tolower(*b++);
        if (ca != cb) return ca - cb;
    }
    return tolower(*a) - tolower(*b);
}

unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len)
{
    return 0;
}

int check_zip(char *filename)
{
    return 0;
}

int loadFromZipByName(char *zipfile, char *filename, unsigned char *buffer)
{
    return 0;
}

/* sound stubs */
void psg_write(int data) {}
void psg_stereo_w(int data) {}

void fmunit_write(int offset, int data) {}
void fmunit_detect_w(int data) {}
int fmunit_detect_r(void) { return 0; }

void *SN76489_GetContextPtr(void) { return 0; }
int SN76489_GetContextSize(void) { return 0; }

void *FM_GetContextPtr(void) { return 0; }
int FM_GetContextSize(void) { return 0; }

void sound_update(void) {}
void sound_init(void) {}
void sound_shutdown(void) {}
void sound_reset(void) {}

void error_init(void) {}
void error_shutdown(void) {}

void system_manage_sram(unsigned char *sram, int slot) {}