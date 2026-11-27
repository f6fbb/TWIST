/*
 * twaudio.h - Live audio for the continuous decoder and the audio
 * transmit method (docs/SPECIFICATION.md, sections 2, 3 and 6), over
 * PortAudio (WASAPI / MME / DirectSound on Windows, ALSA / PulseAudio
 * on Linux, CoreAudio on macOS). The public face is twcore.h
 * (twl_audio_*); twistlive uses it directly.
 *
 * Capture: the sound card callback mixes the channels to mono,
 * low-pass filters and decimates to about 12 kHz (48000 -> 12000,
 * 44100 -> 11025: the decoder needs 3 kHz of audio, not 24), and
 * appends to a ring buffer of the last minutes. The decoding thread
 * copies the last 45 s from it at its own pace: neither ever waits
 * for the other, and a slow pass never loses samples.
 *
 * The same capture path can be fed without a sound card
 * (twa_push, device TWL_AUDIO_NONE): replay of a WAV file, tests.
 *
 * Built without PortAudio (TWL_HAVE_PORTAUDIO undefined), the device
 * functions report no device; feeding by twa_push still works.
 */

#ifndef TWAUDIO_H
#define TWAUDIO_H

#include "twltypes.h"
#include "twtx.h"

typedef struct twa twa_t;

int  twa_available(void);
int  twa_device_count(void);
int  twa_device_info(int index, twl_audio_device_t *d);

twa_t *twa_create(void);
void   twa_destroy(twa_t *a);

int  twa_start_input(twa_t *a, int device, int rate, double keep_s);
int  twa_input_rate(const twa_t *a);
int  twa_push(twa_t *a, const float *x, int frames, int channels);
int  twa_last(twa_t *a, float *out, int n);
void twa_input_stats(const twa_t *a, twl_audio_stats_t *st);
void twa_stop_input(twa_t *a);

int  twa_start_output(twa_t *a, int device, twtx_t *tx);
/* Same, the first sample reaching the DAC at UTC time `utc` (s since
 * 1970; 0: at once): silence is played until then, counted in samples
 * from the stream's own DAC timing. */
int  twa_start_output_at(twa_t *a, int device, twtx_t *tx, double utc);
double twa_output_latency(const twa_t *a);   /* s, of the open stream        */
double twa_output_started(const twa_t *a);   /* UTC s of the first sample, 0  */
int  twa_output_active(const twa_t *a);
void twa_stop_output(twa_t *a);

const char *twa_error(const twa_t *a);

#endif /* TWAUDIO_H */
