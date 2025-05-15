/* ---------------------------------------------------------------------------
 *  Emergency-light controller for an Adafruit MPR121 touch-pad + NeoPixels
 *  --------------------------------------------------------------------------
 *  – Gyro beacon (8 pixels, PIN 2)
 *  – Turn signals  (4 pixels, PIN 0)
 *  – Head + tail   (8 pixels, PIN 4)
 *
 *  A short tap toggles each feature ON / OFF.
 *  A double-tap (< 500 ms) enters a “brightness / colour” setup loop that is
 *  confirmed with CTRL (electrode 5).
 *  ------------------------------------------------------------------------ */

#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_MPR121.h>

#ifndef _BV
#  define _BV(bit)  (1U << (bit))
#endif

/* ---------------------------------------------------------------------------
 *  Pin-map & pixel counts
 * ------------------------------------------------------------------------ */
constexpr uint8_t  PIN_GYRO         = 2;   // Gyro beacon strip
constexpr uint8_t  PIN_TURN         = 0;   // Turn-signal strip
constexpr uint8_t  PIN_HEAD_TAIL    = 4;   // Head-/tail-light strip
constexpr uint8_t  POT_PIN          = 13;  // Analogue pot for brightness / colour

constexpr uint8_t  NUM_GYRO_PIXELS       = 8;
constexpr uint8_t  NUM_TURN_PIXELS       = 4;
constexpr uint8_t  NUM_HEADTAIL_PIXELS   = 8;

/* ---------------------------------------------------------------------------
 *  Timing (half-periods, in ms)
 * ------------------------------------------------------------------------ */
constexpr uint16_t HP_TURN  = 500;
constexpr uint16_t HP_GYRO  = 500;

/* ---------------------------------------------------------------------------
 *  Touch IDs (one bit per electrode)
 * ------------------------------------------------------------------------ */
constexpr uint16_t TK_GYRO          = _BV(0);
constexpr uint16_t TK_TURN_R        = _BV(1);
constexpr uint16_t TK_TURN_L        = _BV(2);
constexpr uint16_t TK_HEAD          = _BV(3);
constexpr uint16_t TK_TAIL          = _BV(4);
constexpr uint16_t TK_CTRL          = _BV(5);
constexpr uint16_t TK_SHOW          = _BV(6);

constexpr uint16_t TK_HAZARD        = TK_TURN_R | TK_TURN_L;
constexpr uint16_t TK_LOW_BEAM      = TK_HEAD    | TK_TAIL;
constexpr uint16_t TK_HEAD_COL      = TK_CTRL    | TK_HEAD;
constexpr uint16_t TK_TAIL_COL      = TK_CTRL    | TK_TAIL;

/* ---------------------------------------------------------------------------
 *  Objects
 * ------------------------------------------------------------------------ */
Adafruit_NeoPixel pxGyro (NUM_GYRO_PIXELS,     PIN_GYRO,      NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pxTurn (NUM_TURN_PIXELS,     PIN_TURN,      NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pxMain (NUM_HEADTAIL_PIXELS, PIN_HEAD_TAIL, NEO_GRB + NEO_KHZ800);

Adafruit_MPR121   cap;

/* ---------------------------------------------------------------------------
 *  State flags
 * ------------------------------------------------------------------------ */
bool gyroEnabled         = false;
bool turnR_Enabled       = false;
bool turnL_Enabled       = false;
bool hazardEnabled       = false;
bool headEnabled         = false;
bool tailEnabled         = false;
bool lowBeamEnabled      = false;

bool cfgGyroBrightness   = false;
bool cfgTurnBrightness   = false;
bool cfgMainBrightness   = false;
bool cfgColour           = false;

/* Runtime helpers */
unsigned long tLastGyro      = 0;
unsigned long tLastTurnR     = 0;
unsigned long tLastTurnL     = 0;
unsigned long tLastHazard    = 0;

/* Double-tap bookkeeping */
struct TapTimer {
  uint8_t       count  = 0;
  unsigned long first  = 0;
};
TapTimer tapGyro, tapTurnR, tapTurnL, tapMain;

/* ---------------------------------------------------------------------------
 *  User presets (will be updated from setup loops)
 * ------------------------------------------------------------------------ */
uint8_t  brGyroInit   = 100;                                // 0-255
uint8_t  brTurnInit   = 100;
uint8_t  brMainInit   = 100;

uint32_t colHeadInit  = pxMain.Color(230, 240, 255);        // bluish-white
uint32_t colTailInit  = pxMain.Color(255,   0,   0);        // red
uint32_t colTurnInit  = pxMain.Color(255, 165,   0);        // amber
uint32_t colGyroA     = pxMain.Color(255,   0,   0);        // red
uint32_t colGyroB     = pxMain.Color(  0,   0, 255);        // blue

/* Demo / show-mode colours */
const uint32_t colShowGyroA = pxMain.Color( 38, 196, 236);
const uint32_t colShowGyroB = pxMain.Color( 20, 148,  20);
const uint32_t colShowTurn  = pxMain.Color(187, 210, 225);
const uint32_t colShowMain  = pxMain.Color(255,   0, 127);

/* ---------------------------------------------------------------------------
 *  Forward declarations
 * ------------------------------------------------------------------------ */
void clearStrip(Adafruit_NeoPixel &strip);
void toggleGyro  (bool state, uint32_t c1, uint32_t c2);
void toggleTurnR (bool state, uint32_t c);
void toggleTurnL (bool state, uint32_t c);
void toggleHazard(bool state, uint32_t c);
void toggleHead  (bool state, uint32_t c);
void toggleTail  (bool state, uint32_t c);
void toggleLowBeam(bool state, uint32_t c);

uint8_t  potToBrightness(int raw);
uint32_t potToWhiteShade (int raw);
uint32_t potToRedShade   (int raw);
uint32_t potToAmberShade (int raw);

void waitRelease(uint8_t electrode);

/* ---------------------------------------------------------------------------
 *  SETUP
 * ------------------------------------------------------------------------ */
void setup()
{
  Serial.begin(9600);

  pxGyro.begin();
  pxTurn.begin();
  pxMain.begin();

  pxGyro.setBrightness(brGyroInit);
  pxTurn.setBrightness(brTurnInit);
  pxMain.setBrightness(brMainInit);

  clearStrip(pxGyro);
  clearStrip(pxTurn);
  clearStrip(pxMain);

  if (!cap.begin(0x5A))
  {
    Serial.println(F("MPR121 not found – check wiring!"));
    while (true) ;
  }
}

/* ---------------------------------------------------------------------------
 *  LOOP
 * ------------------------------------------------------------------------ */
void loop()
{
  uint16_t touchNow = cap.touched();
  if (touchNow) {
    Serial.print(F("Touch 0x"));
    Serial.print(touchNow, HEX);
    Serial.println(F(" detected"));
  }

  /* -----------------------------------------------------------------------
   *  GYRO BEACON  ──────────────────────────────────────────────────────────
   * -------------------------------------------------------------------- */
  handleTap(touchNow, TK_GYRO, tapGyro, cfgGyroBrightness, gyroEnabled,
            tLastGyro, HP_GYRO,
            [](){                 // on-toggle
              gyroEnabled = !gyroEnabled;
              tLastGyro   = millis() - HP_GYRO;   // sync phase
              if (gyroEnabled)
                toggleGyro(true, colGyroA, colGyroB);
              else
                clearStrip(pxGyro);
            },
            [](){                 // brightness-config loop
              cfgGyroBrightness = true;
              while (cfgGyroBrightness) {
                int raw  = analogRead(POT_PIN);
                uint8_t br = potToBrightness(raw);
                pxGyro.setBrightness(br);
                toggleGyro(true, colGyroA, colGyroB);

                if (cap.touched() == TK_CTRL) {
                  brGyroInit = br;
                  waitRelease(5);
                  cfgGyroBrightness = false;
                  gyroEnabled = false;
                  clearStrip(pxGyro);
                }
                if (cap.touched() == TK_GYRO) {
                  pxGyro.setBrightness(brGyroInit);
                  waitRelease(0);
                  cfgGyroBrightness = false;
                  gyroEnabled = false;
                  clearStrip(pxGyro);
                }
              }
            });

  /* Half-period blinking while gyro is active */
  if (gyroEnabled && millis() - tLastGyro >= HP_GYRO) {
    tLastGyro += HP_GYRO;
    static bool phase = false;
    phase = !phase;
    toggleGyro(phase, colGyroA, colGyroB);
  }

  /* Colour swap: CTRL + GYRO (white ↔ red for the first 4 pixels) */
  if (touchNow == TK_CTRL | TK_GYRO) {
    while (cap.touched() == (TK_CTRL | TK_GYRO)) delay(10);
    colGyroA = (colGyroA == pxGyro.Color(255,255,255)) ?
               pxGyro.Color(255,0,0) : pxGyro.Color(255,255,255);
  }

  /* -----------------------------------------------------------------------
   *  TURN SIGNALS  (RIGHT / LEFT / HAZARD)
   * -------------------------------------------------------------------- */
  handleTap(touchNow, TK_TURN_R, tapTurnR, cfgTurnBrightness, turnR_Enabled,
            tLastTurnR, HP_TURN,
            [](){                                  // toggle right
              turnR_Enabled = !turnR_Enabled;
              tLastTurnR    = millis() - HP_TURN;
              if (turnR_Enabled)  toggleTurnR(true, colTurnInit);
              else                clearStrip(pxTurn);
              turnL_Enabled   = false;
              hazardEnabled   = false;
            },
            [](){                                  // brightness setup
              cfgTurnBrightness = true;
              while (cfgTurnBrightness) {
                int raw  = analogRead(POT_PIN);
                uint8_t br = potToBrightness(raw);
                pxTurn.setBrightness(br);
                toggleHazard(true, colTurnInit);

                if (cap.touched() == TK_CTRL) {
                  brTurnInit = br;
                  waitRelease(5);
                  cfgTurnBrightness = false;
                  clearStrip(pxTurn);
                }
                if (cap.touched() & TK_HAZARD) {
                  pxTurn.setBrightness(brTurnInit);
                  waitRelease(1);  // either electrode 1 or 2 is fine
                  cfgTurnBrightness = false;
                  clearStrip(pxTurn);
                }
              }
            });

  /* Blink right */
  if (turnR_Enabled && millis() - tLastTurnR >= HP_TURN && !turnL_Enabled) {
    tLastTurnR += HP_TURN;
    static bool phaseR = false;
    phaseR = !phaseR;
    toggleTurnR(phaseR, colTurnInit);
  }

  /* ─────────────────────────────────────────────────────────────────---- */
  handleTap(touchNow, TK_TURN_L, tapTurnL, cfgTurnBrightness, turnL_Enabled,
            tLastTurnL, HP_TURN,
            [](){                                  // toggle left
              turnL_Enabled = !turnL_Enabled;
              tLastTurnL    = millis() - HP_TURN;
              if (turnL_Enabled)  toggleTurnL(true, colTurnInit);
              else                clearStrip(pxTurn);
              turnR_Enabled   = false;
              hazardEnabled   = false;
            },
            [](){});   // brightness config handled above – skip here

  /* Blink left */
  if (turnL_Enabled && millis() - tLastTurnL >= HP_TURN) {
    tLastTurnL += HP_TURN;
    static bool phaseL = false;
    phaseL = !phaseL;
    toggleTurnL(phaseL, colTurnInit);
  }

  /* ─────────────────────────────────────────────────────────────────---- */
  /* Hazard (both turn buttons together) */
  if (touchNow == TK_HAZARD) {
    hazardEnabled = !hazardEnabled;
    tLastHazard   = millis() - HP_TURN;
    if (hazardEnabled)  toggleHazard(true, colTurnInit);
    else                clearStrip(pxTurn);
    turnL_Enabled = turnR_Enabled = false;
    waitRelease(1);   // wait until both electrodes are clear
  }
  if (hazardEnabled && millis() - tLastHazard >= HP_TURN) {
    tLastHazard += HP_TURN;
    static bool phaseH = false;
    phaseH = !phaseH;
    toggleHazard(phaseH, colTurnInit);
  }

  /* -----------------------------------------------------------------------
   *  HEAD- / TAIL-LIGHTS
   * -------------------------------------------------------------------- */
  handleTap(touchNow, TK_HEAD, tapMain, cfgMainBrightness, headEnabled,
            /* timer not needed */ tLastGyro, HP_TURN,
            [](){                                   // toggle headlights
              lowBeamEnabled = false;
              headEnabled = !headEnabled;
              if (headEnabled) toggleHead(true,  colHeadInit);
              else             clearStrip(pxMain);
            },
            [](){                                   // brightness setup
              cfgMainBrightness = true;
              toggleHead(true, colHeadInit);
              toggleTail(true, colTailInit);
              while (cfgMainBrightness) {
                int raw  = analogRead(POT_PIN);
                uint8_t br = potToBrightness(raw);
                pxMain.setBrightness(br);

                if (cap.touched() == TK_CTRL) {
                  brMainInit = br;
                  waitRelease(5);
                  cfgMainBrightness = false;
                  clearStrip(pxMain);
                  headEnabled = tailEnabled = false;
                }
                if (cap.touched() == TK_HEAD) {
                  pxMain.setBrightness(brMainInit);
                  waitRelease(3);
                  cfgMainBrightness = false;
                  clearStrip(pxMain);
                  headEnabled = tailEnabled = false;
                }
              }
            });

  /* Tail lights (simple ON/OFF) */
  if (touchNow == TK_TAIL) {
    tailEnabled = !tailEnabled;
    toggleTail(tailEnabled, colTailInit);
    waitRelease(4);
  }

  /* Low beam (head + tail together) */
  if (touchNow == TK_LOW_BEAM) {
    lowBeamEnabled = !lowBeamEnabled;
    toggleLowBeam(lowBeamEnabled, colHeadInit);
    waitRelease(3);   // release both 3 & 4
  }

  /* -----------------------------------------------------------------------
   *  COLOUR CONFIGURATION LOOPS
   * -------------------------------------------------------------------- */
  if (touchNow == TK_HEAD_COL) {
    cfgColour = true;
    while (cfgColour) {
      int raw  = analogRead(POT_PIN);
      uint32_t c = potToWhiteShade(raw);
      toggleHead(true, c);

      if (cap.touched() == TK_CTRL) {
        colHeadInit = c;
        waitRelease(5);
        cfgColour = false;
        clearStrip(pxMain);
      }
      if (cap.touched() == TK_HEAD) {
        waitRelease(3);
        cfgColour = false;
        clearStrip(pxMain);
      }
    }
  }

  if (touchNow == TK_TAIL_COL) {
    cfgColour = true;
    while (cfgColour) {
      int raw  = analogRead(POT_PIN);
      uint32_t c = potToRedShade(raw);
      toggleTail(true, c);

      if (cap.touched() == TK_CTRL) {
        colTailInit = c;
        waitRelease(5);
        cfgColour = false;
        clearStrip(pxMain);
      }
      if (cap.touched() == TK_TAIL) {
        waitRelease(4);
        cfgColour = false;
        clearStrip(pxMain);
      }
    }
  }

  /* -----------------------------------------------------------------------
   *  DEMO “SHOW MODE”
   * -------------------------------------------------------------------- */
  if (touchNow == TK_SHOW) {
    waitRelease(6);                 // debouncing
    demoShowMode();
  }
}

/* ===========================================================================
 *  HELPER FUNCTIONS
 * ======================================================================== */
/* ---------------------------------------------------------------------------
 *  handleTap()
 *  ----------
 *  Generic double-tap detector + dispatcher.
 * ------------------------------------------------------------------------ */
template<typename ToggleFn, typename CfgFn>
void handleTap(uint16_t touchNow,
               uint16_t keyMask,
               TapTimer &tap,
               bool &cfgFlag,
               bool &featureFlag,
               unsigned long &tLast,
               uint16_t halfPeriod,
               ToggleFn onToggle,
               CfgFn    onConfig)
{
  /* flush stale taps (>1 s) */
  if (tap.count && millis() - tap.first > 1000) tap.count = 0;

  if (touchNow == keyMask) {            // electrode touched
    tap.count++;
    if (tap.count == 1) tap.first = millis();

    /* Double-tap → config loop */
    if (tap.count == 2 && millis() - tap.first < 500) {
      tap.count = 0;
      cfgFlag   = true;
      onConfig();
      return;
    }

    /* Single tap → ON / OFF */
    if (tap.count == 1) {
      onToggle();
    }
    waitRelease(__builtin_ctz(keyMask));   // wait for release of that electrode
  }

  /* Blinking handled outside */
}

/* ---------------------------------------------------------------------------
 *  Strip helpers
 * ------------------------------------------------------------------------ */
void clearStrip(Adafruit_NeoPixel &strip)
{
  for (uint16_t i = 0; i < strip.numPixels(); ++i)
    strip.setPixelColor(i, 0);
  strip.show();
}

void toggleGyro(bool phase, uint32_t c1, uint32_t c2)
{
  /* Two interleaved groups of four pixels */
  for (uint8_t i = 0; i < 8; ++i) {
    bool groupA = (i < 2) || (i > 5);
    pxGyro.setPixelColor(i, phase ^ groupA ? c1 : c2);
  }
  pxGyro.show();
}

void toggleTurnR(bool phase, uint32_t c)
{
  pxTurn.setPixelColor(2, phase ? c : 0);
  pxTurn.setPixelColor(3, phase ? c : 0);
  pxTurn.show();
}
void toggleTurnL(bool phase, uint32_t c)
{
  pxTurn.setPixelColor(0, phase ? c : 0);
  pxTurn.setPixelColor(1, phase ? c : 0);
  pxTurn.show();
}
void toggleHazard(bool phase, uint32_t c)
{
  for (uint8_t i = 0; i < NUM_TURN_PIXELS; ++i)
    pxTurn.setPixelColor(i, phase ? c : 0);
  pxTurn.show();
}

void toggleHead(bool on, uint32_t c)
{
  static const uint8_t idx[] = {0,2,3,4,5,7};
  for (uint8_t i : idx) pxMain.setPixelColor(i, on ? c : 0);
  pxMain.show();
}
void toggleTail(bool on, uint32_t c)
{
  pxMain.setPixelColor(1, on ? c : 0);
  pxMain.setPixelColor(6, on ? c : 0);
  pxMain.show();
}
void toggleLowBeam(bool on, uint32_t c)
{
  static const uint8_t idx[] = {0,2,5,7};
  for (uint8_t i : idx) pxMain.setPixelColor(i, on ? c : 0);
  pxMain.show();
}

/* ---------------------------------------------------------------------------
 *  Analogue helpers
 * ------------------------------------------------------------------------ */
uint8_t potToBrightness(int raw)
{
  raw = constrain(raw, 0, 4095);
  return map(raw, 0, 4095, 0, 255);            // full 8-bit range
}
uint32_t potToWhiteShade(int raw)
{
  raw = constrain(raw, 0, 4095);
  byte pos = map(raw, 0, 4095, 0, 120);
  return pxMain.Color(230 - pos, 240 - pos, 255 - pos / 3);
}
uint32_t potToRedShade(int raw)
{
  raw = constrain(raw, 0, 4095);
  byte pos = map(raw, 0, 4095, 0, 100);
  return pxMain.Color(255 - pos / 3, pos / 8, pos);
}
uint32_t potToAmberShade(int raw)
{
  raw = constrain(raw, 0, 4095);
  byte pos = map(raw, 0, 4095, 0, 120);
  return pxMain.Color(255 - pos / 5, 165 - pos / 2, 0);
}

/* Wait until a given electrode is released */
void waitRelease(uint8_t electrode)
{
  while (cap.touched() & _BV(electrode)) delay(10);
}

/* ---------------------------------------------------------------------------
 *  SHOW MODE – fancy demo lights (press electrode 6 to start)
 * ------------------------------------------------------------------------ */
void demoShowMode()
{
  /* Reset strips and brightness */
  pxGyro.setBrightness(brGyroInit);
  pxTurn.setBrightness(brTurnInit);
  pxMain.setBrightness(brMainInit);
  clearStrip(pxGyro);
  clearStrip(pxTurn);
  clearStrip(pxMain);

  /* Sequencer */
  bool running = true;
  uint32_t tStart = millis();
  uint8_t  order  = 0;     // 0-3 for the four centre pixels
  while (running)
  {
    /* --- Gyro flash ---------------------------------------------------- */
    bool phase = ((millis() - tStart) / 500) & 1;
    toggleGyro(phase, colShowGyroA, colShowGyroB);

    /* --- Turn & head chaser ------------------------------------------- */
    toggleHazard(phase, colShowTurn);

    /* middle pixel “bouncing” */
    static const uint8_t centre[] = {1,2,5,6};
    if (((millis() - tStart) / 500) % 2 == 0) {   // every second period
      pxMain.setPixelColor(centre[order], colShowMain);
      pxMain.setPixelColor(centre[(order + 3) % 4], 0);
      pxMain.show();
      order = (order + 1) & 3;
    }

    if (cap.touched() == TK_SHOW) {
      waitRelease(6);
      running = false;
    }
    delay(20);
  }
  /* Clean-up */
  clearStrip(pxGyro);
  clearStrip(pxTurn);
  clearStrip(pxMain);
}
