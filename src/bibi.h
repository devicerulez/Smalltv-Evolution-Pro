// Bibigotchi: Bibi, un barboncino nero pixel-art che cambia umore e animazione in base ai dati del firmware.
// Basato sul Tamagotchi di SmallTV Evolution: stessa scena, stessi stati, personaggio nuovo.
// Scena su una griglia di 56x41 celle (4 bit per cella, 16 colori) ingrandita 4x (224x164 pixel).
// Questo file non dipende dallo schermo: i colori e l'invio delle righe arrivano da due funzioni passate da chi chiama.
#pragma once
#include <stdint.h>
#include <string.h>

namespace bibi {

static const int GW = 56, GH = 41, SCALE = 4;
static const int GROUND = 34;  // prima riga del terreno
static const int CX = 28;      // asse del personaggio

enum State : uint8_t {
  ST_HAPPY, ST_SLEEP, ST_STORM, ST_RAIN, ST_SNOW, ST_COLD, ST_HOT, ST_MASK,
  ST_CRASH, ST_PUMP, ST_PARTY, ST_COFFEE, ST_FOOD, ST_SNEEZE, ST_UV, ST_COUNT
};
enum Sky : uint8_t { SKY_DAY, SKY_CLOUD, SKY_RAIN, SKY_NIGHT, SKY_SNOW, SKY_SMOG, SKY_CRASH, SKY_PUMP, SKY_PARTY, SKY_COUNT };

struct Scene {
  State st;
  Sky sky;
  uint8_t variant;  // variante dell'animazione di base (0..3)
};

typedef uint16_t (*ColFn)(uint8_t r, uint8_t g, uint8_t b);
typedef void (*RowFn)(int x, int y, int w, const uint16_t* px);

// ------------------------------------------------------------------
//  Griglia e primitive
// ------------------------------------------------------------------
static uint8_t G[GW * GH / 2];

static inline void P(int x, int y, uint8_t c) {
  if ((unsigned)x >= (unsigned)GW || (unsigned)y >= (unsigned)GH) return;
  uint8_t& b = G[(y * GW + x) >> 1];
  if (x & 1) b = (b & 0xF0) | c;
  else b = (b & 0x0F) | (uint8_t)(c << 4);
}
static inline uint8_t Q(int x, int y) {
  if ((unsigned)x >= (unsigned)GW || (unsigned)y >= (unsigned)GH) return 0;
  const uint8_t b = G[(y * GW + x) >> 1];
  return (x & 1) ? (b & 0x0F) : (b >> 4);
}
static void R(int x, int y, int w, int h, uint8_t c) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) P(x + i, y + j, c);
}
// ellisse piena
static void E(int cx, int cy, int rx, int ry, uint8_t c) {
  const int A = 2 * rx + 1, B = 2 * ry + 1;
  const int lim = A * A * B * B;
  for (int dy = -ry; dy <= ry; dy++)
    for (int dx = -rx; dx <= rx; dx++)
      if ((2 * dx) * (2 * dx) * B * B + (2 * dy) * (2 * dy) * A * A <= lim) P(cx + dx, cy + dy, c);
}
// ellisse con contorno di una cella
static void EO(int cx, int cy, int rx, int ry, uint8_t line, uint8_t fill) {
  E(cx, cy, rx, ry, line);
  if (rx > 1 && ry > 1) E(cx, cy, rx - 1, ry - 1, fill);
}
static void L(int x0, int y0, int x1, int y1, uint8_t c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int e = dx - dy;
  for (;;) {
    P(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * e;
    if (e2 > -dy) { e -= dy; x0 += sx; }
    if (e2 < dx) { e += dx; y0 += sy; }
  }
}
// disegna un piccolo disegno da stringhe ('#' = colore c, 'o' = colore c2, '.' = trasparente)
static void S(int x, int y, const char* const* rows, int n, uint8_t c, uint8_t c2 = 0) {
  for (int j = 0; j < n; j++)
    for (int i = 0; rows[j][i]; i++) {
      const char ch = rows[j][i];
      if (ch == '#') P(x + i, y + j, c);
      else if (ch == 'o') P(x + i, y + j, c2);
    }
}

// ------------------------------------------------------------------
//  Colori (indici di palette)
// ------------------------------------------------------------------
enum : uint8_t {
  C_BG = 0, C_SK1, C_SK2, C_SK3, C_SK4, C_GR, C_GR2,
  C_BODY, C_OUT, C_BELLY, C_CHEEK, C_DARK, C_WHITE, C_A, C_B, C_C
};

static const uint32_t SKY_COL[SKY_COUNT][4] = {
  {0x4FB4F0, 0x6EC6FF, 0x8FD6FF, 0xB5E6FF},   // giorno
  {0x7F93A8, 0x92A5B8, 0xA6B7C7, 0xBAC8D5},   // nuvoloso
  {0x2F3A4C, 0x3A465A, 0x475469, 0x566379},   // pioggia / temporale
  {0x070B22, 0x0E1640, 0x16225A, 0x213070},   // notte
  {0x9DB6CC, 0xB3C7DA, 0xC8D8E6, 0xDCE7F1},   // neve
  {0x8A7A64, 0x9C8B73, 0xAE9D84, 0xC0B096},   // smog
  {0x2A0A14, 0x45101F, 0x611A2B, 0x7D2438},   // crollo
  {0x1A0F40, 0x2A1A66, 0x3F2B8C, 0x5A42B0},   // festa / rialzo
  {0x3A1A5E, 0x5A2A7E, 0x8A3C86, 0xC4568A},   // festa (tramonto)
};
static const uint32_t GROUND_COL[SKY_COUNT][2] = {
  {0x56B85F, 0x3F9A4B}, {0x5CA364, 0x468A50}, {0x3F7A4C, 0x31653D}, {0x1D3B2B, 0x15301F},
  {0xF2F7FF, 0xD3E0EE}, {0x7A7A52, 0x666642}, {0x3A2330, 0x2C1824}, {0x2C2250, 0x211A40}, {0x5A3A6A, 0x452C56},
};

static uint16_t PAL[16];

static void setPalette(const Scene& sc, uint32_t bgRGB, ColFn col) {
  uint32_t p[16];
  p[C_BG] = bgRGB;
  for (int i = 0; i < 4; i++) p[C_SK1 + i] = SKY_COL[sc.sky][i];
  p[C_GR] = GROUND_COL[sc.sky][0];
  p[C_GR2] = GROUND_COL[sc.sky][1];
  p[C_BODY] = 0x2B2B38;    // pelo nero
  p[C_OUT] = 0x08080E;     // contorno
  p[C_BELLY] = 0x6A6A86;   // riflessi sul pelo, muso, contorno occhi
  p[C_CHEEK] = 0xFF7A94;   // lingua
  p[C_DARK] = 0x050508;    // naso, pupille
  p[C_WHITE] = 0xFFFFFF;
  switch (sc.st) {
    case ST_RAIN: p[C_A] = 0xE8384F; p[C_B] = 0xA82039; p[C_C] = 0xBFE3FF; break;
    case ST_STORM: p[C_A] = 0xFFF27A; p[C_B] = 0x8FB4FF; p[C_C] = 0xBFE3FF; break;
    case ST_SNOW: case ST_COLD: p[C_A] = 0xE8384F; p[C_B] = 0xFFFFFF; p[C_C] = 0x9AD7FF; break;
    case ST_HOT: p[C_A] = 0xFFD54A; p[C_B] = 0x6CC7FF; p[C_C] = 0xFF8FB0; break;
    case ST_MASK: p[C_A] = 0xD8F0FA; p[C_B] = 0x8CC8E0; p[C_C] = 0x7A6B55; break;
    case ST_CRASH: p[C_A] = 0xFF4D5E; p[C_B] = 0xA3202F; p[C_C] = 0x6CC7FF; break;
    case ST_PUMP: p[C_A] = 0x3DDC84; p[C_B] = 0x1C8F55; p[C_C] = 0xFFC83D; break;
    case ST_PARTY: p[C_A] = 0xFF4FA0; p[C_B] = 0xFFD54A; p[C_C] = 0x4FC3FF; break;
    case ST_COFFEE: p[C_A] = 0xF4F0E8; p[C_B] = 0x6B3E26; p[C_C] = 0xFFFFFF; break;
    case ST_FOOD: p[C_A] = 0xE9B25A; p[C_B] = 0xE5384F; p[C_C] = 0xFFD54A; break;
    case ST_SNEEZE: p[C_A] = 0xFFD84A; p[C_B] = 0xB8D84A; p[C_C] = 0xBFE3FF; break;
    case ST_UV: p[C_A] = 0xFFD54A; p[C_B] = 0xE6C070; p[C_C] = 0xB5803A; break;
    case ST_SLEEP: p[C_A] = 0xCFE0FF; p[C_B] = 0xFFF2B8; p[C_C] = 0xFFE066; break;
    default: p[C_A] = 0xFF6FA5; p[C_B] = 0xFFC83D; p[C_C] = 0x6CC7FF; break;
  }
  for (int i = 0; i < 16; i++) PAL[i] = col((uint8_t)(p[i] >> 16), (uint8_t)(p[i] >> 8), (uint8_t)p[i]);
}

// ------------------------------------------------------------------
//  Sfondo
// ------------------------------------------------------------------
static void backdrop(const Scene& sc) {
  for (int y = 0; y < GH; y++) {
    uint8_t c;
    if (y >= GROUND) c = (y == GROUND) ? C_GR2 : C_GR;
    else c = (uint8_t)(C_SK1 + (y < 9 ? 0 : y < 18 ? 1 : y < 26 ? 2 : 3));
    for (int x = 0; x < GW; x++) {
      uint8_t cc = c;
      if (y > GROUND && ((x * 7 + y * 13) % 11) == 0) cc = C_GR2;
      P(x, y, cc);
    }
  }
  (void)sc;
}

static void roundCorners() {
  static const uint8_t cut[5] = {4, 2, 1, 1, 0};  // celle da cancellare per ogni riga d'angolo
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < cut[j]; i++) {
      P(i, j, C_BG);
      P(GW - 1 - i, j, C_BG);
      P(i, GH - 1 - j, C_BG);
      P(GW - 1 - i, GH - 1 - j, C_BG);
    }
}

static void cloud(int x, int y, uint8_t c) {
  E(x, y, 5, 2, c);
  E(x - 4, y + 1, 3, 1, c);
  E(x + 4, y + 1, 3, 1, c);
  E(x + 1, y - 2, 3, 2, c);
}

static void starsNight(uint16_t f) {
  static const uint8_t SX[9] = {5, 13, 22, 38, 47, 51, 9, 33, 18};
  static const uint8_t SY[9] = {4, 11, 3, 6, 12, 3, 17, 15, 9};
  for (int i = 0; i < 9; i++) {
    const bool on = ((f / 3 + i * 5) % 7) != 0;
    if (!on) continue;
    P(SX[i], SY[i], C_WHITE);
    if (i % 3 == 0) {
      P(SX[i] - 1, SY[i], C_A);
      P(SX[i] + 1, SY[i], C_A);
      P(SX[i], SY[i] - 1, C_A);
      P(SX[i], SY[i] + 1, C_A);
    }
  }
}
static void moon(int x, int y) {
  E(x, y, 4, 4, C_B);
  E(x + 3, y - 1, 3, 3, C_SK2);  // falce
  P(x - 2, y + 1, C_A);
  P(x - 1, y + 2, C_A);
}
static void sun(int x, int y, uint16_t f, bool big) {
  const int r = big ? 6 : 4;
  E(x, y, r, r, C_A);
  P(x - 2, y - 2, C_WHITE);
  const int ray = r + 2 + ((f / 2) & 1);
  for (int k = 0; k < 8; k++) {
    static const int8_t DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int8_t DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int s = r + 1; s <= ray; s++) P(x + DX[k] * s, y + DY[k] * s, C_A);
  }
}

// ------------------------------------------------------------------
//  Particelle e pezzi di scena
// ------------------------------------------------------------------
static void rain(uint16_t f, int canopyL, int canopyR, int canopyY, bool heavy) {
  const int n = heavy ? 26 : 18;
  for (int i = 0; i < n; i++) {
    const int x = (i * 37 + 11) % GW;
    const int speed = 3 + (i % 3);
    const int y = ((f * speed) + i * 17) % (GH + 6) - 4;
    if (canopyR > canopyL && x >= canopyL && x <= canopyR && y > canopyY - 1) {
      if (y == canopyY + 0 && (i & 1)) P(x, canopyY - 1, C_C);  // schizzo sul telo
      continue;
    }
    if (y >= GROUND - 1) {  // anello sul terreno
      P(x - 1, GROUND + 1, C_C);
      P(x + 1, GROUND + 1, C_C);
      continue;
    }
    P(x, y, C_C);
    P(x, y + 1, C_C);
  }
}
static void snowfall(uint16_t f) {
  for (int i = 0; i < 24; i++) {
    const int x0 = (i * 29 + 7) % GW;
    const int sway = ((f / 3 + i) % 4 == 0) ? 1 : (((f / 3 + i) % 4 == 2) ? -1 : 0);
    const int y = ((f * (1 + i % 2)) / 1 + i * 13) % (GH + 4) - 2;
    if (y >= GROUND) continue;
    P(x0 + sway, y, C_WHITE);
    if (i % 5 == 0) {
      P(x0 + sway + 1, y, C_WHITE);
      P(x0 + sway, y + 1, C_WHITE);
    }
  }
}
static void lightning(uint16_t f) {
  const bool flash = (f % 14) == 3 || (f % 14) == 4;
  if (!flash) return;
  const int x = (f / 14) % 2 ? 9 : 46;
  int px = x, py = 0;
  const int8_t step[7] = {2, -2, 3, -3, 2, -1, 1};
  for (int k = 0; k < 7; k++) {
    L(px, py, px + step[k], py + 5, C_A);
    L(px + 1, py, px + step[k] + 1, py + 5, C_A);
    px += step[k];
    py += 5;
  }
}
static void glyphZ(int x, int y, int big) {
  if (big) {
    R(x, y, 5, 1, C_WHITE);
    P(x + 3, y + 1, C_WHITE);
    P(x + 2, y + 2, C_WHITE);
    P(x + 1, y + 3, C_WHITE);
    R(x, y + 4, 5, 1, C_WHITE);
  } else {
    R(x, y, 3, 1, C_A);
    P(x + 1, y + 1, C_A);
    R(x, y + 2, 3, 1, C_A);
  }
}
static void heart(int x, int y, uint8_t c) {
  static const char* const H[] = {".#.#.", "#####", ".###.", "..#.."};
  S(x, y, H, 4, c);
}
static void note(int x, int y, uint8_t c) {
  L(x + 1, y, x + 1, y + 4, c);
  P(x + 2, y, c);
  P(x + 2, y + 1, c);
  E(x, y + 4, 1, 1, c);
}
static void sparkle(int x, int y, uint8_t c) {
  P(x, y, c);
  P(x - 1, y, c);
  P(x + 1, y, c);
  P(x, y - 1, c);
  P(x, y + 1, c);
}
static void coin(int x, int y, uint16_t f) {
  const bool edge = (f % 4) >= 2;
  if (edge) {
    R(x, y - 1, 1, 3, C_C);
    P(x, y - 1, C_WHITE);
  } else {
    R(x - 1, y - 1, 3, 3, C_C);
    P(x, y, C_B);
    P(x - 1, y - 1, C_WHITE);
  }
}
static void confetti(uint16_t f) {
  static const uint8_t cols[5] = {C_A, C_B, C_C, C_WHITE, C_CHEEK};
  for (int i = 0; i < 26; i++) {
    const int x = (i * 31 + 5) % GW;
    const int y = ((f * (1 + i % 3)) + i * 11) % (GH + 4) - 2;
    if (y >= GROUND) continue;
    P(x, y, cols[i % 5]);
    if (i & 1) P(x + 1, y, cols[i % 5]);
  }
}
static void balloon(int x, int y, uint8_t c) {
  EO(x, y, 3, 4, C_OUT, c);
  P(x - 1, y - 2, C_WHITE);
  L(x, y + 5, x - 1, y + 9, C_WHITE);
  L(x - 1, y + 9, x, y + 13, C_WHITE);
}
// grafico a candele stilizzato: andamento (su = true: verde in salita, altrimenti rosso in discesa)
static void chart(bool up, uint16_t f) {
  static const int8_t Y_UP[8] = {26, 24, 25, 20, 21, 15, 16, 9};
  static const int8_t Y_DN[8] = {8, 10, 9, 15, 14, 21, 20, 27};
  const int8_t* Y = up ? Y_UP : Y_DN;
  const uint8_t c = C_A;
  for (int i = 0; i < 7; i++) {
    const int x0 = 4 + i * 7, x1 = 4 + (i + 1) * 7;
    L(x0, Y[i], x1, Y[i + 1], c);
    L(x0, Y[i] + 1, x1, Y[i + 1] + 1, c);
  }
  // freccia in cima alla linea (lampeggia)
  if ((f / 3) % 2 == 0) {
    const int ex = 4 + 7 * 7, ey = Y[7];
    if (up) {
      P(ex, ey - 2, c); P(ex - 1, ey - 1, c); P(ex + 1, ey - 1, c); P(ex - 2, ey, c); P(ex + 2, ey, c);
    } else {
      P(ex, ey + 3, c); P(ex - 1, ey + 2, c); P(ex + 1, ey + 2, c); P(ex - 2, ey + 1, c); P(ex + 2, ey + 1, c);
    }
  }
}
static void haze(uint16_t f) {
  for (int i = 0; i < 18; i++) {
    const int x = ((i * 23 + f * (1 + i % 2)) % (GW + 6)) - 3;
    const int y = 6 + (i * 7) % 27;
    P(x, y, C_C);
    P(x + 1, y, C_C);
    if (i % 3 == 0) P(x + 2, y + 1, C_C);
  }
}

// ------------------------------------------------------------------
//  Bibi: barboncino nero
// ------------------------------------------------------------------
enum Eyes : uint8_t { EY_OPEN, EY_BLINK, EY_HAPPY, EY_SLEEP, EY_SAD, EY_WIDE, EY_STAR, EY_X, EY_WORRY, EY_HALF };
enum Mouth : uint8_t { MO_SMILE, MO_OPEN, MO_SAD, MO_O, MO_FLAT, MO_WAVY, MO_SCREAM, MO_CHEW, MO_NONE, MO_TONGUE };
enum Paw : uint8_t { PW_REST, PW_UP, PW_OUT, PW_HEAD, PW_WAVE0, PW_WAVE1, PW_HIDE };

struct Pose {
  int8_t x = 0;        // spostamento orizzontale (tremore, dondolo)
  int8_t bob = 0;      // sollevamento da terra (salti)
  int8_t squash = 0;   // -1 accucciato, +1 allungato
  Eyes eyes = EY_OPEN;
  Mouth mouth = MO_SMILE;
  Paw pawL = PW_REST, pawR = PW_REST;
  bool lie = false;    // sdraiato (dorme)
};

// occhio: contorno chiaro intorno, pupilla scura con luce
static void eye(int x, int y, Eyes t, uint16_t f, bool left) {
  switch (t) {
    case EY_OPEN:
      R(x - 1, y, 5, 4, C_BELLY);
      P(x - 1, y, C_BODY); P(x + 3, y, C_BODY); P(x - 1, y + 3, C_BODY); P(x + 3, y + 3, C_BODY);
      R(x, y, 3, 4, C_DARK);
      P(x + 1, y, C_WHITE);
      P(x + 1, y + 1, C_WHITE);
      break;
    case EY_BLINK: R(x - 1, y + 2, 5, 1, C_WHITE); break;
    case EY_HAPPY: P(x - 1, y + 3, C_WHITE); P(x, y + 2, C_WHITE); P(x + 1, y + 1, C_WHITE); P(x + 2, y + 2, C_WHITE); P(x + 3, y + 3, C_WHITE); break;
    case EY_SLEEP: P(x - 1, y + 1, C_WHITE); P(x, y + 2, C_WHITE); P(x + 1, y + 2, C_WHITE); P(x + 2, y + 2, C_WHITE); P(x + 3, y + 1, C_WHITE); break;
    case EY_SAD:
      eye(x, y, EY_OPEN, f, left);
      if (left) { P(x - 1, y - 1, C_WHITE); P(x, y - 2, C_WHITE); P(x + 1, y - 2, C_WHITE); }
      else { P(x + 3, y - 1, C_WHITE); P(x + 2, y - 2, C_WHITE); P(x + 1, y - 2, C_WHITE); }
      break;
    case EY_WIDE: {
      R(x - 1, y - 1, 5, 6, C_WHITE);
      P(x - 1, y - 1, C_BODY); P(x + 3, y - 1, C_BODY); P(x - 1, y + 4, C_BODY); P(x + 3, y + 4, C_BODY);
      const int jx = (f % 2) ? 0 : 1;
      R(x + jx, y + 1, 2, 3, C_DARK);
      break;
    }
    case EY_STAR:
      R(x - 1, y, 5, 4, C_BELLY);
      P(x + 1, y, C_C); P(x, y + 1, C_C); P(x + 1, y + 1, C_WHITE); P(x + 2, y + 1, C_C); P(x + 1, y + 2, C_C);
      if ((f / 2) % 2) { P(x - 1, y - 1, C_C); P(x + 3, y + 3, C_C); }
      break;
    case EY_X:   // occhi chiusi dal pianto: ">" "<"
      if (left) { P(x - 1, y, C_WHITE); P(x, y + 1, C_WHITE); P(x + 1, y + 2, C_WHITE); P(x, y + 3, C_WHITE); P(x - 1, y + 4, C_WHITE); }
      else { P(x + 3, y, C_WHITE); P(x + 2, y + 1, C_WHITE); P(x + 1, y + 2, C_WHITE); P(x + 2, y + 3, C_WHITE); P(x + 3, y + 4, C_WHITE); }
      break;
    case EY_WORRY:
      eye(x, y, EY_OPEN, f, left);
      if (left) { P(x - 1, y - 2, C_WHITE); P(x, y - 2, C_WHITE); P(x + 1, y - 3, C_WHITE); }
      else { P(x + 3, y - 2, C_WHITE); P(x + 2, y - 2, C_WHITE); P(x + 1, y - 3, C_WHITE); }
      break;
    case EY_HALF:
      R(x - 1, y + 2, 5, 2, C_BELLY);
      R(x, y + 2, 3, 2, C_DARK);
      R(x - 1, y + 1, 5, 1, C_OUT);
      break;
  }
}

// bocca: centrata sul muso (x), con la riga "y" come prima riga sotto il naso
static void mouth(int x, int y, Mouth t, uint16_t f) {
  switch (t) {
    case MO_SMILE: P(x - 2, y, C_DARK); P(x - 1, y + 1, C_DARK); P(x, y + 1, C_DARK); P(x + 1, y + 1, C_DARK); P(x + 2, y, C_DARK); break;
    case MO_OPEN: R(x - 2, y, 5, 1, C_DARK); R(x - 1, y + 1, 3, 2, C_DARK); P(x, y + 2, C_CHEEK); break;
    case MO_SAD: P(x - 2, y + 1, C_DARK); P(x - 1, y, C_DARK); P(x, y, C_DARK); P(x + 1, y, C_DARK); P(x + 2, y + 1, C_DARK); break;
    case MO_O: R(x - 1, y, 3, 3, C_DARK); P(x, y + 1, C_CHEEK); break;
    case MO_FLAT: R(x - 1, y + 1, 3, 1, C_DARK); break;
    case MO_WAVY: { const int o = f % 2; P(x - 2, y + o, C_DARK); P(x - 1, y + 1 - o, C_DARK); P(x, y + o, C_DARK); P(x + 1, y + 1 - o, C_DARK); P(x + 2, y + o, C_DARK); break; }
    case MO_SCREAM: R(x - 2, y, 5, 4, C_DARK); R(x - 1, y + 3, 3, 1, C_CHEEK); break;
    case MO_CHEW: if (f % 2) R(x - 2, y, 5, 3, C_DARK); else R(x - 1, y + 1, 3, 1, C_DARK); break;
    case MO_TONGUE: R(x - 2, y, 5, 1, C_DARK); R(x - 1, y + 1, 3, 1, C_DARK); R(x, y + 1, 2, 2, C_CHEEK); break;
    case MO_NONE: break;
  }
}

// zampa; se (sx, sy) e' lontano dal corpo disegna anche la gamba che la collega alla spalla
static void paw(int x, int y, int sx = 0, int sy = 0) {
  if (sx || sy) {
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i <= 6; i++) {
        const int px = sx + (x - sx) * i / 6, py = sy + (y - sy) * i / 6;
        if (pass == 0) E(px, py, 2, 2, C_OUT);
        else E(px, py, 1, 1, C_BODY);
      }
  }
  EO(x, y, 2, 2, C_OUT, C_BODY);
  P(x - 1, y - 1, C_BELLY);
}

// punti di riccio: piccoli riflessi sul pelo
static void curls(int x0, int y0, int x1, int y1) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      if (Q(x, y) == C_BODY && ((x * 5 + y * 3 + (x / 3) * 2) % 12) == 0) P(x, y, C_BELLY);
}

// disegna Bibi; ritorna il centro verticale della testa
static int dog(const Pose& p, uint16_t f, int* outEyeY = nullptr) {
  const int cx = CX + p.x;
  const int b = p.bob;
  int hy;                       // centro della testa
  if (p.lie) {
    hy = 22 + ((f / 4) % 2) * 0;
    E(CX, GROUND + 1, 14, 1, C_GR2);
    // coda a pon-pon
    L(cx + 12, 30, cx + 15, 27, C_OUT);
    EO(cx + 16, 26, 2, 2, C_OUT, C_BODY);
    // corpo sdraiato
    EO(cx - 9, 30, 4, 4, C_OUT, C_BODY);
    EO(cx + 9, 30, 4, 4, C_OUT, C_BODY);
    EO(cx, 29, 10, 5, C_OUT, C_BODY);
    // orecchie, ciuffo, testa
    EO(cx - 10, hy + 3, 4, 6, C_OUT, C_BODY);
    EO(cx + 10, hy + 3, 4, 6, C_OUT, C_BODY);
    EO(cx, hy - 8, 4, 3, C_OUT, C_BODY);
    EO(cx, hy, 8, 7, C_OUT, C_BODY);
    curls(cx - 14, hy - 10, cx + 14, hy + 10);
    curls(cx - 12, 24, cx + 12, 33);
    // zampe davanti, sotto il muso
    EO(cx - 4, 32, 3, 2, C_OUT, C_BODY);
    EO(cx + 4, 32, 3, 2, C_OUT, C_BODY);
  } else {
    hy = 14 - b + (p.squash < 0 ? 1 : 0);
    E(CX, GROUND + 1, 10 - (b > 2 ? 2 : b / 2), 1, C_GR2);
    // coda a pon-pon che scodinzola
    const int wag = (int)((f / 2) % 3) - 1;
    L(cx + 7, 28 - b, cx + 12 + wag, 23 - b, C_OUT);
    EO(cx + 13 + wag, 21 - b, 3, 3, C_OUT, C_BODY);
    // cosce e corpo
    EO(cx - 8, 29 - b, 4, 4, C_OUT, C_BODY);
    EO(cx + 8, 29 - b, 4, 4, C_OUT, C_BODY);
    EO(cx, 27 - b, 7, 6, C_OUT, C_BODY);
    // orecchie a pon-pon, ciuffo, testa
    const int ed = (b > 1) ? -1 : 0;
    EO(cx - 10, hy + 2 + ed, 4, 6, C_OUT, C_BODY);
    EO(cx + 10, hy + 2 + ed, 4, 6, C_OUT, C_BODY);
    EO(cx, hy - 8, 4, 3, C_OUT, C_BODY);
    EO(cx, hy, 8, 7, C_OUT, C_BODY);
    // zampe davanti (se non sono alzate)
    if (p.pawL == PW_REST) { EO(cx - 4, 30 - b, 2, 3, C_OUT, C_BODY); EO(cx - 4, 33 - b, 3, 1, C_OUT, C_BODY); }
    if (p.pawR == PW_REST) { EO(cx + 4, 30 - b, 2, 3, C_OUT, C_BODY); EO(cx + 4, 33 - b, 3, 1, C_OUT, C_BODY); }
    curls(cx - 14, hy - 10, cx + 14, 34 - b);
  }
  // muso chiaro e naso
  EO(cx, hy + 4, 4, 3, C_OUT, C_BELLY);
  R(cx - 1, hy + 2, 3, 2, C_DARK);
  P(cx - 1, hy + 2, C_WHITE);
  // collare con medaglietta
  R(cx - 5, hy + 8, 11, 2, C_A);
  R(cx - 1, hy + 10, 3, 2, C_B);
  // occhi
  const int ey = hy - 3;
  eye(cx - 6, ey, p.eyes, f, true);
  eye(cx + 4, ey, p.eyes, f, false);
  mouth(cx, hy + 5, p.mouth, f);
  if (outEyeY) *outEyeY = ey;
  // zampe alzate
  auto drawPaw = [&](Paw a, int side) {
    switch (a) {
      case PW_UP: paw(cx + side * 18, hy - 3, cx + side * 6, hy + 11); break;
      case PW_OUT: paw(cx + side * 17, hy + 12 - b, cx + side * 6, hy + 11); break;
      case PW_HEAD: paw(cx + side * 6, hy - 9); break;
      case PW_WAVE0: paw(cx + side * 18, hy - 5, cx + side * 6, hy + 11); break;
      case PW_WAVE1: paw(cx + side * 19, hy - 1, cx + side * 6, hy + 11); break;
      default: break;
    }
  };
  drawPaw(p.pawL, -1);
  drawPaw(p.pawR, 1);
  return hy;
}

// ------------------------------------------------------------------
//  Accessori
// ------------------------------------------------------------------
static void umbrella(int cx, int hy) {
  const int topY = hy - 13;
  const int ucx = cx + 3;
  for (int dy = 0; dy <= 6; dy++)
    for (int dx = -17; dx <= 17; dx++) {
      if ((2 * dx) * (2 * dx) * 169 + (2 * dy) * (2 * dy) * 1225 > 35 * 35 * 169) continue;
      const uint8_t c = ((dx + 17) / 6) % 2 ? C_B : C_A;
      P(ucx + dx, topY + 6 - dy, c);
    }
  for (int dx = -17; dx <= 17; dx += 1)
    if (((dx + 17) % 6) < 3) P(ucx + dx, topY + 7, ((dx + 17) / 6) % 2 ? C_B : C_A);
  P(ucx, topY - 1, C_OUT);
  const int hx = cx + 17;
  L(hx, topY + 6, hx, hy + 8, C_DARK);
  L(hx, hy + 8, hx - 1, hy + 9, C_DARK);
}
static void scarf(int cx, int hy) {
  const int y = hy + 8;
  R(cx - 8, y, 17, 3, C_A);
  R(cx - 8, y + 3, 17, 1, C_B);
  for (int i = -8; i <= 8; i += 3) P(cx + i, y, C_B);
  R(cx + 4, y + 3, 4, 7, C_A);
  R(cx + 4, y + 5, 4, 1, C_B);
  R(cx + 4, y + 8, 4, 1, C_B);
}
static void beanie(int cx, int hy) {
  const int y = hy - 7;
  for (int dy = 0; dy < 6; dy++) {
    const int w = 4 + dy;
    R(cx - w + 1, y - 5 + dy, 2 * w - 1, 1, C_A);
  }
  R(cx - 8, y + 1, 17, 2, C_B);
  E(cx, y - 7, 2, 2, C_WHITE);
}
static void mask(int cx, int hy) {
  R(cx - 6, hy + 3, 13, 6, C_A);
  R(cx - 6, hy + 3, 13, 1, C_B);
  R(cx - 6, hy + 5, 13, 1, C_B);
  R(cx - 6, hy + 8, 13, 1, C_B);
  L(cx - 6, hy + 4, cx - 10, hy + 1, C_B);
  L(cx + 6, hy + 4, cx + 10, hy + 1, C_B);
}
static void sunglasses(int cx, int ey) {
  const int y = ey - 1;
  R(cx - 8, y, 7, 5, C_A);
  R(cx + 3, y, 7, 5, C_A);
  R(cx - 7, y + 1, 5, 3, C_DARK);
  R(cx + 4, y + 1, 5, 3, C_DARK);
  R(cx - 1, y, 4, 1, C_A);
  P(cx - 6, y + 1, C_WHITE);
  P(cx + 5, y + 1, C_WHITE);
}
static void partyHat(int cx, int hy) {
  const int y = hy - 6;
  for (int dy = 0; dy < 8; dy++) {
    const int w = 1 + dy / 2;
    R(cx - w + 1, y - 8 + dy, 2 * w, 1, (dy % 3 == 1) ? C_B : C_A);
  }
  P(cx, y - 9, C_C);
  P(cx + 1, y - 9, C_C);
  R(cx - 4, y - 1, 9, 1, C_B);
}
static void sweat(int cx, int hy, uint16_t f) {
  for (int k = 0; k < 2; k++) {
    const int y = hy - 6 + (int)((f * 2 + k * 5) % 12);
    const int x = cx + (k ? 17 : -17);
    P(x, y, C_B);
    P(x, y + 1, C_B);
    P(x, y + 2, C_WHITE);
  }
}
static void tears(int cx, int ey, uint16_t f) {
  for (int k = 0; k < 2; k++) {
    const int x = cx + (k ? 5 : -6);
    for (int j = 0; j < 9; j++) {
      const bool on = ((f + j) % 3) != 0;
      if (on) P(x + (k ? 1 : -1) * (j > 5 ? 1 : 0), ey + 4 + j, C_C);
    }
  }
}
static void mug(int x, int y, uint16_t f, bool steam) {
  R(x, y, 5, 5, C_A);
  R(x + 5, y + 1, 1, 3, C_A);
  P(x + 6, y + 2, C_A);
  R(x + 1, y, 3, 1, C_B);
  if (steam)
    for (int k = 0; k < 3; k++) {
      const int sy = y - 2 - ((f + k * 2) % 6);
      P(x + 1 + k, sy, C_WHITE);
    }
}
static void bone(int x, int y) {
  static const char* const BN[] = {".##...##.", "#########", ".##...##."};
  S(x, y, BN, 3, C_WHITE);
  P(x + 2, y + 1, C_A);
  P(x + 6, y + 1, C_A);
}
static void popsicle(int x, int y) {
  R(x + 1, y + 6, 1, 3, C_A);
  R(x, y, 3, 6, C_C);
  P(x, y, C_WHITE);
  P(x, y + 1, C_WHITE);
  R(x, y + 5, 3, 1, C_B);
}
static void pollen(uint16_t f) {
  for (int i = 0; i < 22; i++) {
    const int x = ((i * 17 + f * (1 + i % 3)) % (GW + 4)) - 2;
    const int y = 3 + ((i * 11 + (int)(f / 2) * (1 + i % 2)) % (GROUND - 4));
    const uint8_t c = (i % 3 == 0) ? C_B : C_A;
    P(x, y, c);
    if (i % 4 == 0) P(x + 1, y, c);
  }
}
static void strawHat(int cx, int hy) {
  const int y = hy - 6;
  R(cx - 6, y - 4, 13, 4, C_B);
  R(cx - 6, y - 2, 13, 1, C_C);
  R(cx - 13, y, 27, 2, C_B);
  R(cx - 13, y + 1, 27, 1, C_C);
}
static void tissue(int x, int y) {
  R(x, y, 6, 5, C_WHITE);
  R(x + 1, y + 1, 4, 1, C_C);
  P(x, y, C_C);
}

// ------------------------------------------------------------------
//  Disegno di un fotogramma
// ------------------------------------------------------------------
static void compose(const Scene& sc, uint16_t f) {
  backdrop(sc);
  Pose po;
  int hy = 0, ey = 0;

  switch (sc.sky) {
    case SKY_DAY: cloud(14 + (f / 6) % 40, 8, C_SK4); break;
    case SKY_CLOUD: cloud(12 + (f / 8) % 40, 7, C_SK4); cloud(40 - (f / 10) % 30, 13, C_SK4); break;
    case SKY_RAIN: cloud(10, 5, C_SK4); cloud(30, 4, C_SK4); cloud(48, 6, C_SK4); break;
    case SKY_SNOW: cloud(14, 6, C_SK4); cloud(44, 8, C_SK4); break;
    case SKY_NIGHT: starsNight(f); break;
    default: break;
  }

  switch (sc.st) {
    case ST_SLEEP: {
      moon(46, 8);
      po.lie = true;
      po.eyes = EY_SLEEP;
      po.mouth = MO_NONE;
      hy = dog(po, f, &ey);
      glyphZ(40 + ((f / 3) % 6), 18 - (int)((f / 3) % 6), 0);
      glyphZ(43 + ((f / 3 + 2) % 6), 12 - (int)((f / 3 + 2) % 6), 1);
      break;
    }
    case ST_STORM: {
      lightning(f);
      po.x = (f % 2) ? 1 : -1;
      po.eyes = EY_WIDE;
      po.mouth = MO_WAVY;
      po.pawL = PW_HEAD;
      po.pawR = PW_HEAD;
      hy = dog(po, f, &ey);
      rain(f, 0, -1, 0, true);
      break;
    }
    case ST_RAIN: {
      po.eyes = ((f % 16) == 9) ? EY_BLINK : EY_HAPPY;
      po.mouth = MO_SMILE;
      po.pawR = PW_HIDE;
      po.pawL = (f / 4) % 2 ? PW_WAVE0 : PW_REST;
      po.bob = ((f / 4) % 4 == 1) ? 1 : 0;
      hy = dog(po, f, &ey);
      paw(CX + 17, hy + 8, CX + 5, hy + 11);
      umbrella(CX, hy);
      rain(f, CX + 3 - 17, CX + 3 + 17, hy - 13 + 4, false);
      break;
    }
    case ST_SNOW:
    case ST_COLD: {
      po.x = ((f / 2) % 2) ? 1 : 0;
      po.eyes = ((f % 14) == 5) ? EY_BLINK : EY_OPEN;
      po.mouth = MO_WAVY;
      hy = dog(po, f, &ey);
      scarf(CX + po.x, hy);
      beanie(CX + po.x, hy);
      if (sc.st == ST_SNOW) snowfall(f);
      break;
    }
    case ST_HOT: {
      sun(46, 8, f, true);
      po.eyes = EY_OPEN;
      po.mouth = (f / 3) % 2 ? MO_TONGUE : MO_OPEN;
      po.pawR = PW_HIDE;
      hy = dog(po, f, &ey);
      paw(CX + 11, hy + 12, CX + 5, hy + 12);
      popsicle(CX + 10, hy + 6);
      sunglasses(CX, ey);
      sweat(CX, hy, f);
      break;
    }
    case ST_MASK: {
      haze(f);
      E(8, 8, 4, 4, C_A);
      po.eyes = EY_WORRY;
      po.mouth = MO_NONE;
      po.bob = ((f % 10) == 7) ? 1 : 0;
      hy = dog(po, f, &ey);
      mask(CX, hy);
      haze(f + 3);
      break;
    }
    case ST_CRASH: {
      chart(false, f);
      po.x = (f % 2) ? 1 : 0;
      po.eyes = EY_X;
      po.mouth = MO_SCREAM;
      po.pawL = PW_HEAD;
      po.pawR = PW_HEAD;
      hy = dog(po, f, &ey);
      tears(CX + po.x, ey, f);
      break;
    }
    case ST_PUMP: {
      starsNight(f);
      chart(true, f);
      static const int8_t HOP[8] = {0, 2, 4, 5, 4, 2, 0, 0};
      po.bob = HOP[f % 8];
      po.squash = (po.bob == 0 && (f % 8) == 7) ? -1 : 0;
      po.eyes = EY_STAR;
      po.mouth = MO_TONGUE;
      po.pawL = PW_OUT;
      po.pawR = PW_OUT;
      hy = dog(po, f, &ey);
      for (int k = 0; k < 6; k++) {
        const int x = 5 + k * 9 + (k % 2) * 2;
        const int y = (int)(((f * 2) + k * 9) % (GROUND + 3)) - 3;
        if (y < GROUND - 1) coin(x, y, f + k);
      }
      break;
    }
    case ST_PARTY: {
      balloon(8, 14 - (int)((f / 4) % 3), C_A);
      balloon(49, 12 + (int)((f / 4 + 1) % 3), C_C);
      static const int8_t HOP[8] = {0, 1, 3, 4, 3, 1, 0, 0};
      po.bob = HOP[f % 8];
      po.eyes = EY_HAPPY;
      po.mouth = MO_TONGUE;
      po.pawL = (f / 2) % 2 ? PW_UP : PW_OUT;
      po.pawR = (f / 2) % 2 ? PW_OUT : PW_UP;
      hy = dog(po, f, &ey);
      partyHat(CX, hy);
      confetti(f);
      break;
    }
    case ST_SNEEZE: {
      pollen(f);
      const int ph = (int)(f % 20);
      if (ph < 10) {
        po.eyes = (ph % 6 < 3) ? EY_WORRY : EY_HALF;
        po.mouth = (ph % 4 < 2) ? MO_O : MO_OPEN;
        po.bob = (int8_t)(ph / 4);
        hy = dog(po, f, &ey);
      } else if (ph < 13) {
        po.x = (ph % 2) ? 1 : -1;
        po.eyes = EY_X;
        po.mouth = MO_SCREAM;
        po.squash = -1;
        po.pawL = PW_HEAD;
        hy = dog(po, f, &ey);
        const int k = ph - 10 + 1;
        for (int i = 0; i < 9; i++) {
          const int x = CX + 6 + k * 4 + (i % 3) * 2;
          const int y = hy + 6 + (i - 4) * k / 2;
          if (x < GW - 1 && y > 1 && y < GROUND) P(x, y, (i % 2) ? C_C : C_WHITE);
        }
      } else {
        po.eyes = ((f % 6) == 2) ? EY_BLINK : EY_HALF;
        po.mouth = MO_FLAT;
        po.pawR = PW_HIDE;
        hy = dog(po, f, &ey);
        paw(CX + 9, hy + 9, CX + 5, hy + 12);
        tissue(CX + 4, hy + 5);
      }
      break;
    }
    case ST_UV: {
      sun(46, 8, f, true);
      po.eyes = EY_OPEN;
      po.mouth = MO_SMILE;
      po.bob = ((f / 4) % 4 == 1) ? 1 : 0;
      hy = dog(po, f, &ey);
      sunglasses(CX, ey);
      R(CX - 1, hy + 2, 3, 2, C_WHITE);       // crema solare sul naso
      strawHat(CX, hy);
      break;
    }
    case ST_COFFEE: {
      const bool sip = (f % 12) >= 8;
      po.eyes = sip ? EY_HAPPY : (((f % 10) == 4) ? EY_BLINK : EY_HALF);
      po.mouth = sip ? MO_O : MO_SMILE;
      po.pawR = PW_HIDE;
      hy = dog(po, f, &ey);
      paw(CX + (sip ? 8 : 13), hy + (sip ? 10 : 12), CX + 5, hy + 12);
      mug(CX + (sip ? 8 : 12), hy + (sip ? 6 : 8), f, true);
      break;
    }
    case ST_FOOD: {
      const bool bite = (f % 8) >= 4;
      po.eyes = EY_HAPPY;
      po.mouth = bite ? MO_CHEW : MO_TONGUE;
      po.pawR = PW_HIDE;
      hy = dog(po, f, &ey);
      paw(CX + (bite ? 8 : 12), hy + (bite ? 11 : 13), CX + 5, hy + 13);
      bone(CX + (bite ? 5 : 9), hy + (bite ? 6 : 9));
      break;
    }
    default: {  // ST_HAPPY
      switch (sc.variant & 3) {
        case 0: {  // saltello
          static const int8_t HOP[8] = {0, 2, 3, 4, 3, 2, 0, 0};
          po.bob = HOP[f % 8];
          po.squash = ((f % 8) == 7) ? -1 : 0;
          po.eyes = EY_HAPPY;
          po.mouth = MO_TONGUE;
          po.pawL = po.bob > 1 ? PW_UP : PW_REST;
          po.pawR = po.bob > 1 ? PW_UP : PW_REST;
          hy = dog(po, f, &ey);
          break;
        }
        case 1: {  // saluta con la zampa
          po.eyes = ((f % 14) == 6) ? EY_BLINK : EY_OPEN;
          po.mouth = MO_SMILE;
          po.pawR = (f / 2) % 2 ? PW_WAVE0 : PW_WAVE1;
          hy = dog(po, f, &ey);
          break;
        }
        case 2: {  // balla
          po.x = ((f / 2) % 2) ? 2 : -2;
          po.bob = ((f / 2) % 2) ? 1 : 0;
          po.eyes = EY_HAPPY;
          po.mouth = MO_TONGUE;
          po.pawL = (f / 2) % 2 ? PW_UP : PW_OUT;
          po.pawR = (f / 2) % 2 ? PW_OUT : PW_UP;
          hy = dog(po, f, &ey);
          note(6 + (int)((f / 3) % 4), 22 - (int)((f / 3) % 5) * 2, C_WHITE);
          note(46 - (int)((f / 3 + 1) % 4), 20 - (int)((f / 3 + 2) % 5) * 2, C_C);
          break;
        }
        default: {  // innamorato
          po.bob = ((f / 3) % 2) ? 1 : 0;
          po.eyes = EY_HAPPY;
          po.mouth = MO_SMILE;
          hy = dog(po, f, &ey);
          heart(CX - 2 + (int)((f / 2) % 3) - 1, 6 - (int)((f / 4) % 2), C_CHEEK);
          if ((f / 4) % 2) heart(CX + 14, 12, C_CHEEK);
          break;
        }
      }
      break;
    }
  }
  (void)hy;
  (void)ey;
  roundCorners();
}

// ------------------------------------------------------------------
//  Ritratto (icona del menu): il viso di Bibi, disegnato sul fondo trasparente
// ------------------------------------------------------------------
typedef void (*RectFn)(int x, int y, int w, int h, uint16_t c);

static void portrait(uint32_t bgRGB, ColFn col, RectFn rect, int cx, int cy) {
  Scene sc = {ST_HAPPY, SKY_DAY, 0};
  setPalette(sc, bgRGB, col);
  PAL[C_A] = col(0xFF, 0x4F, 0x6E);   // collare
  PAL[C_B] = col(0xFF, 0xC8, 0x3D);   // medaglietta
  memset(G, 0, sizeof(G));
  const int pcx = 28, hy = 17;
  EO(pcx - 10, hy + 2, 4, 6, C_OUT, C_BODY);
  EO(pcx + 10, hy + 2, 4, 6, C_OUT, C_BODY);
  EO(pcx, hy - 8, 4, 3, C_OUT, C_BODY);
  EO(pcx, hy, 8, 7, C_OUT, C_BODY);
  EO(pcx, 29, 7, 5, C_OUT, C_BODY);
  curls(pcx - 14, hy - 10, pcx + 14, 33);
  EO(pcx, hy + 4, 4, 3, C_OUT, C_BELLY);
  R(pcx - 1, hy + 2, 3, 2, C_DARK);
  P(pcx - 1, hy + 2, C_WHITE);
  R(pcx - 5, hy + 8, 11, 2, C_A);
  R(pcx - 1, hy + 10, 3, 2, C_B);
  eye(pcx - 6, hy - 3, EY_OPEN, 0, true);
  eye(pcx + 4, hy - 3, EY_OPEN, 0, false);
  mouth(pcx, hy + 5, MO_TONGUE, 0);
  // ogni cella diventa un quadrato 4x4, centrato su (cx, cy); le celle di sfondo restano trasparenti
  const int ox = cx - (pcx * SCALE), oy = cy - (hy + 4) * SCALE;
  for (int y = 0; y < GH; y++) {
    int x = 0;
    while (x < GW) {
      const uint8_t c = Q(x, y);
      if (c == C_BG) { x++; continue; }
      int x2 = x + 1;
      while (x2 < GW && Q(x2, y) == c) x2++;
      rect(ox + x * SCALE, oy + y * SCALE, (x2 - x) * SCALE, SCALE, PAL[c]);
      x = x2;
    }
  }
}

// ------------------------------------------------------------------
//  Invio allo schermo
// ------------------------------------------------------------------
static void blit(int x0, int y0, RowFn rowFn) {
  static uint16_t line[GW * SCALE];
  for (int r = 0; r < GH; r++) {
    for (int c = 0; c < GW; c++) {
      const uint16_t v = PAL[Q(c, r)];
      uint16_t* d = &line[c * SCALE];
      d[0] = v;
      d[1] = v;
      d[2] = v;
      d[3] = v;
    }
    for (int k = 0; k < SCALE; k++) rowFn(x0, y0 + r * SCALE + k, GW * SCALE, line);
  }
}

inline void drawFrame(const Scene& sc, uint16_t frame, uint32_t bgRGB, ColFn col, RowFn rowFn, int x0, int y0) {
  setPalette(sc, bgRGB, col);
  compose(sc, frame);
  blit(x0, y0, rowFn);
}

}  // namespace bibi
