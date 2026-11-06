
#include <stdint.h>
#include "sound.h"

extern void sound_outb(uint16_t port, uint8_t value);
extern uint32_t sound_inb(uint16_t port);
extern void sound_wait(uint32_t count);

static uint8_t speaker_state;

void sound_tone(uint32_t frequency)
{
    if (frequency < 20 || frequency > 20000)
        return;

    uint32_t divisor = 1193182u / frequency;

    sound_outb(0x43, 0xB6);
    sound_outb(0x42, (uint8_t)(divisor & 0xFF));
    sound_outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    speaker_state = (uint8_t)sound_inb(0x61);
    sound_outb(0x61, speaker_state | 3);
}

void sound_stop(void)
{
    sound_outb(0x61, (uint8_t)(sound_inb(0x61) & 0xFC));
}

void sound_delay(uint32_t milliseconds)
{
    for (uint32_t i = 0; i < milliseconds; i++) {
        sound_wait(10000);
    }
}

static void play_note(uint32_t frequency, uint32_t duration)
{
    sound_tone(frequency);
    sound_delay(duration);
    sound_stop();
    sound_delay(15);
}

static void pitch_slide(uint32_t start, uint32_t end)
{
    if (start < end) {
        for (uint32_t frequency = start; frequency <= end; frequency += 5) {
            sound_tone(frequency);
            sound_delay(5);
        }
    } else {
        uint32_t frequency = start;

        while (frequency >= end) {
            sound_tone(frequency);
            sound_delay(5);

            if (frequency < end + 5)
                break;

            frequency -= 5;
        }
    }

    sound_stop();
}

void sound_startup(void)
{
    play_note(50, 70);
    play_note(50, 70);
    sound_delay(40);

    pitch_slide(50, 220);
    pitch_slide(220, 110);

    sound_delay(30);

    play_note(147, 90);
    play_note(196, 90);
    play_note(294, 120);
    play_note(392, 220);

    sound_stop();
}