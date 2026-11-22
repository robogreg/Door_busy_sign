#include "faces.h"

#include <GxEPD2.h>

#include "mood_bitmaps.h"

static_assert(sizeof(kMoodBitmaps) / sizeof(kMoodBitmaps[0]) == (size_t)door::Mood::Count - 1,
              "mood_bitmaps.h is out of date: rerun tools/gen_moods.py");

void drawMoodFace(Adafruit_GFX& g, door::Mood m, int16_t cx, int16_t cy) {
  if (m == door::Mood::None || m >= door::Mood::Count) return;
  const MoodBitmap& b = kMoodBitmaps[(int)m - 1];
  const int16_t s = MOOD_BMP_SIZE, x = cx - s / 2, y = cy - s / 2;
  g.fillCircle(cx, cy, kMoodDiscRadius, GxEPD_WHITE);
  g.drawBitmap(x, y, b.black, s, s, GxEPD_BLACK);
  g.drawBitmap(x, y, b.red, s, s, GxEPD_RED);
}
