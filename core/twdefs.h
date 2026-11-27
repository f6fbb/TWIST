/*
 * twdefs.h - Constants shared by every protocol of the engine: the
 * version, the chip timing of the OOK frames (0.256 s x OpN) and the
 * size of the largest frame the receiver handles. The constants of
 * the classic frame are in twcdefs.h, those of the TWIST frames in
 * twist.h, those of PI4 in pi4.h.
 */

#ifndef TWDEFS_H
#define TWDEFS_H

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* TWIST version - see CHANGELOG.md. */
#define TW_VERSION    "1.0.0-rc.1"   /* TWIST Console: the same as app/Twist.App/Twist.App.csproj */
#define TW_BUILD_INFO "F6FBB 2026"

/* Largest frame handled by the receiver (TWIST long frame: 160 coded
 * bits). The receive structures are sized for it; the actual frame
 * geometry is chosen at run time (tw_spec_t.ncoded / .nch). */
#define TW_MAXCODED  160
#define TW_MAXCHIPS  (2 * TW_MAXCODED + 1)

/* Chips of the shorter OOK frames (the classic frame and TWIST S): the
 * window of the keying detector (twscan.h). */
#define TW_SHORT_CHIPS 239

/* -------------------------------------------------------------- */
/* Timing: one chip lasts 0.256 * n seconds for sub-mode OpN.       */
/* Op05 (n=0.5) is the mode recommended for 2 m and above.          */
/* -------------------------------------------------------------- */
#define TW_TCHIP_BASE  0.256

static inline double tw_tchip(double n) { return TW_TCHIP_BASE * n; }

#endif /* TWDEFS_H */
