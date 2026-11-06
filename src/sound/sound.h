
#ifndef NEUTRA_SOUND_H
#define NEUTRA_SOUND_H

#include <stdint.h>

void sound_startup(void);
void sound_stop(void);
void sound_tone(uint32_t frequency);
void sound_delay(uint32_t milliseconds);

#endif
