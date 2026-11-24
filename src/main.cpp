// ============================================================================
//  Small TV Evolution Pro  -  firmware per NM-TV-154 (ESP32-WROOM-32E)
//
//  Menu di schermate comandato dal pulsante touch superiore (GPIO32 / T9)
//  oppure dai tre tasti dell'interfaccia web:
//    MENU   : 1 tocco = avanti, 2 tocchi = indietro, tieni premuto = entra
//    DENTRO : 1 tocco = funzione successiva, 2 tocchi = funzione precedente,
//             tieni premuto = torna al menu
//    Tieni premuto 8 secondi = portale di configurazione WiFi/citta'
//
//  Meteo : principale | prossimi 3 giorni | grafico temperatura
//  Aria  : indice EAQI | salute (UV e pollini)
//  Cripto : candele in tempo reale delle criptovalute scelte dal web
//  Fasi Lunari : fase della luna di oggi, illuminazione e prossima luna piena
//  Timer : conto alla rovescia a ore, minuti e secondi (impostabile dal web)
//  Salvaschermo : dopo un po' di inattivita' mostra a giro le funzioni scelte dal web
//  Frattali, Countdown, Citazione, Numeri, Post-it : schermate nuove della 2.0.1
//  Radar ISS : posizione della stazione spaziale, traccia dell'orbita e prossimo passaggio
//  Bibigotchi : Bibi, il barboncino nero che cambia umore
//  Bibi AI : assistente AI su servizio esterno (chat dal web, espressioni animate, memoria)
//  Radar Voli : aerei in volo vicino a te (adsb.fi, rotte da adsbdb)
//  Foto : galleria delle foto caricate dal web
//  Quadranti : 5 orologi (semplice, analogico, flip, Matrix, citta' del mondo)
//  Pomodoro : timer di concentrazione | livelli, XP, badge e statistiche
//  Sistema : pioggia Matrix con statistiche del dispositivo
//  Dati: Open-Meteo (gratuito, nessuna chiave API)
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include "jpegdec.h"
#include "bibi.h"
#include "iss.h"
#include "moon.h"
#include "moondraw.h"
#include "logo.h"
#include "fractal.h"
#include "lucky.h"
#include "quotes.h"
#include "qrender.h"
#include <time.h>
#include <math.h>

#define FW_VERSION "2.0.4a"

// ---------------------------------------------------------------- Pin -------
#define PIN_BL        19   // retroilluminazione, PWM, attiva bassa
#define PIN_PANEL_PWR 21   // alimentazione pannello, va portato a LOW
#define PIN_TOUCH     32   // pulsante touch (canale T9)
#define AP_NAME       "NM-TV-Setup"

// ------------------------------------------------------------- Colori -------
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

static inline uint16_t c565(uint32_t rgb) {
  return RGB565((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

// t = 0 -> a, t = 255 -> b
uint32_t mixRGB(uint32_t a, uint32_t b, uint8_t t) {
  uint32_t r  = (((a >> 16) & 255) * (255 - t) + ((b >> 16) & 255) * t) / 255;
  uint32_t g  = (((a >> 8) & 255) * (255 - t) + ((b >> 8) & 255) * t) / 255;
  uint32_t bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
  return (r << 16) | (g << 8) | bl;
}

// colori fissi (icone meteo e segnali): non dipendono dal tema
static const uint16_t C_SUN    = RGB565(255, 196, 40);
static const uint16_t C_CLOUD  = RGB565(206, 215, 232);
static const uint16_t C_CLOUDD = RGB565(120, 132, 156);
static const uint16_t C_RAIN   = RGB565(70, 150, 255);
static const uint16_t C_SNOW   = RGB565(235, 243, 255);
static const uint16_t C_BOLT   = RGB565(255, 225, 60);
static const uint16_t C_MAX    = RGB565(255, 150, 60);
static const uint16_t C_MIN    = RGB565(110, 190, 255);
static const uint16_t C_MOON   = RGB565(236, 236, 205);
static const uint16_t C_ERR    = RGB565(255, 110, 110);
static const uint16_t C_AIR    = RGB565(80, 220, 200);

// Temi (0xRRGGBB), gli stessi di SmallTV Evolution. Il primo (Neon) e' quello di default.
struct Theme {
  uint32_t bg, card, a1, a2, text, dim, band;
};
const Theme THEMES[] = {
  // bg        card      a1        a2        text      dim       band
  {0x05060F, 0x0E1230, 0x00E5FF, 0xFF2E93, 0xFFFFFF, 0x6B7399, 0x2A1470},  // Neon
  {0x030F0E, 0x0A2422, 0x2DFFB2, 0x7C5CFF, 0xEFFFFA, 0x5F8F86, 0x0B4A48},  // Aurora
  {0x120610, 0x2A0F26, 0xFF9F1C, 0xFF3D81, 0xFFF4E6, 0x9C6B7E, 0x6A1A4A},  // Tramonto
  {0x06101C, 0x0F2338, 0x7FD1FF, 0xA78BFA, 0xF2F8FF, 0x6F8CAB, 0x14487A},  // Ghiaccio
  {0x000000, 0x14100A, 0xFFB000, 0xFF6A00, 0xFFE9B8, 0x7A5C1E, 0x4A3400},  // Ambra
};
const uint8_t THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
static const char* const THEME_NAME[5] = {"Neon (cyberpunk)", "Aurora", "Tramonto", "Ghiaccio", "Ambra"};
const Theme* th = &THEMES[0];

// colori del tema in uso (si impostano con applyTheme)
uint16_t C_BG = 0, C_CARD = 0, C_TRACK = 0, C_TEXT = 0xFFFF, C_DIM = 0, C_ACC = 0, C_ACC2 = 0;

void applyTheme(uint8_t i) {
  if (i >= THEME_COUNT) i = 0;
  th = &THEMES[i];
  C_BG    = c565(th->bg);
  C_CARD  = c565(th->card);
  C_TRACK = c565(mixRGB(th->card, th->dim, 90));
  C_TEXT  = c565(th->text);
  C_DIM   = c565(th->dim);
  C_ACC   = c565(th->a1);
  C_ACC2  = c565(th->a2);
}

// Fasce EAQI (indice europeo): 0-20, 20-40, 40-60, 60-80, 80-100, >100
static const uint16_t BANDCOL[6] = {
  RGB565(80, 240, 230), RGB565(80, 204, 170), RGB565(240, 230, 65),
  RGB565(255, 80, 80),  RGB565(160, 0, 60),   RGB565(135, 40, 140)
};
static const char* BANDTXT[6] = {"Ottima", "Buona", "Discreta", "Scadente", "Molto scadente", "Pessima"};

static const char* GIORNI[7] = {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"};
static const char* MESI[12]  = {"Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno",
                                "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre"};

static const char* TZ_ROME = "CET-1CEST,M3.5.0,M10.5.0/3";   // Europe/Rome, ora legale automatica

static const uint32_t WEATHER_MS = 10UL * 60UL * 1000UL;     // meteo ogni 10 minuti
static const uint32_t AIR_MS     = 30UL * 60UL * 1000UL;     // aria ogni 30 minuti

// ------------------------------------------------------- Configurazione -----
struct CdEvent {
  char name[17];   // nome dell'evento (ASCII)
  uint16_t y;      // anno
  uint8_t m, d;    // mese 1..12 e giorno 1..31 (m = 0: evento vuoto)
  bool yearly;     // si ripete ogni anno
};
struct NoteItem {
  char text[41];   // testo del post-it (ASCII, con emoticon come :) ;) :D <3)
  bool on;
};

struct Config {
  String ssid, pass, cityq, city;
  float lat = 0, lon = 0;
  bool geo = false, fmt12 = false, inv = true, rot = false;
  uint8_t theme = 0;
  uint8_t bright = 100;                        // luminosita' di giorno 5..100
  bool nightOn = false;                        // modalita' notturna
  uint16_t nStart = 22 * 60, nEnd = 7 * 60;    // fascia notturna (minuti dalla mezzanotte)
  uint8_t nBright = 10;                        // luminosita' notturna 0..100 (0 = spento)
  uint8_t startSel = 0;                        // 0 = menu, 1..N = apri subito quella funzione
  uint32_t cMask = 0x13;                       // criptovalute scelte (bit per bit): BTC, ETH, SOL
  uint8_t cSec = 5;                            // secondi per ogni cripto (1..60)
  uint8_t cInt = 1;                            // intervallo candele: 0=5m 1=15m 2=1h 3=4h
  uint8_t mig = 0;                             // versione di migrazione delle impostazioni
  uint8_t pSec = 5;                            // secondi per ogni foto (1..60)
  uint8_t pFit = 0;                            // foto caricate: 0 = adattate (riempiono), 1 = intere con bordi neri
  uint8_t pomW = 25, pomB = 5, pomL = 15;      // Pomodoro: lavoro, pausa, pausa lunga (minuti)
  uint8_t tmH = 0, tmM = 10, tmS = 0;          // Timer: ore, minuti, secondi iniziali
  // Bibi AI (servizio esterno): 0 = Claude (Anthropic), 1 = compatibile OpenAI (indirizzo, modello e chiave a scelta)
  uint8_t aiProv = 1;
  String aiUrl = "https://text.pollinations.ai/openai", aiModel = "openai", aiKey;
  bool aiDaily = true;                         // una frase nuova ogni giorno
  uint8_t flKm = 50;                           // Radar voli: raggio in km
  uint8_t wc[4] = {0, 0, 0, 0};                // Quadrante citta: indici nell'elenco WC_CITY (riempiti al caricamento)
  uint8_t xpGoal = 4;                          // obiettivo giornaliero di pomodori
  bool ssOn = false;                           // salvaschermo attivo
  uint8_t ssMin = 5;                           // minuti di inattivita' prima che parta (1..60)
  uint8_t ssSec = 5;                           // secondi per ogni funzione (1..60)
  uint32_t ssMask = 1;                         // funzioni scelte: bit n = n-esima funzione dell'elenco (la 1a = bit 0)
  CdEvent cd[3] = {};                          // eventi del Countdown
  NoteItem nt[3] = {};                         // note del Post-it
};
Config cfg;
Preferences prefs;

// testo ASCII: le lettere accentate italiane (UTF-8) diventano lettera + apostrofo (e' a' o'), il resto non ASCII e' scartato
static void noteCopy(char* dst, size_t n, const char* src) {
  size_t j = 0;
  for (size_t i = 0; src && src[i] && j < n - 1; i++) {
    const uint8_t c = (uint8_t)src[i];
    if (c >= 32 && c < 127) {
      dst[j++] = (char)c;
    } else if (c == 0xC3 && src[i + 1]) {
      const uint8_t d = (uint8_t)src[++i];
      char base = 0;
      switch (d) {
        case 0xA0: case 0xA1: base = 'a'; break;
        case 0xA8: case 0xA9: base = 'e'; break;
        case 0xAC: case 0xAD: base = 'i'; break;
        case 0xB2: case 0xB3: base = 'o'; break;
        case 0xB9: case 0xBA: base = 'u'; break;
        case 0x80: case 0x81: base = 'A'; break;
        case 0x88: case 0x89: base = 'E'; break;
        case 0x8C: case 0x8D: base = 'I'; break;
        case 0x92: case 0x93: base = 'O'; break;
        case 0x99: case 0x9A: base = 'U'; break;
        default: break;
      }
      if (base && j + 2 < n) {
        dst[j++] = base;
        dst[j++] = '\'';
      }
    }
  }
  dst[j] = 0;
  while (j > 0 && dst[j - 1] == ' ') dst[--j] = 0;
}

static void cdSetDate(CdEvent& e, const String& ds) {          // "AAAA-MM-GG"; data non valida = evento vuoto
  int y = 0, m = 0, d = 0;
  e.y = 0;
  e.m = 0;
  e.d = 0;
  if (sscanf(ds.c_str(), "%d-%d-%d", &y, &m, &d) == 3 && y >= 2000 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
    e.y = (uint16_t)y;
    e.m = (uint8_t)m;
    e.d = (uint8_t)d;
  }
}

void loadCfg() {
  prefs.begin("nmtv", true);
  cfg.ssid  = prefs.getString("ssid", "");
  cfg.pass  = prefs.getString("pass", "");
  cfg.cityq = prefs.getString("cityq", "Roma");
  cfg.city  = prefs.getString("city", "");
  cfg.lat   = prefs.getFloat("lat", 0);
  cfg.lon   = prefs.getFloat("lon", 0);
  cfg.geo   = prefs.getBool("geo", false);
  cfg.fmt12 = prefs.getBool("fmt12", false);
  cfg.inv   = prefs.getBool("inv", true);
  cfg.rot   = prefs.getBool("rot", false);
  cfg.theme = prefs.getUChar("theme", 0);
  cfg.bright  = prefs.getUChar("bri", 100);
  cfg.nightOn = prefs.getBool("nOn", false);
  cfg.nStart  = prefs.getUShort("nS", 22 * 60);
  cfg.nEnd    = prefs.getUShort("nE", 7 * 60);
  cfg.nBright = prefs.getUChar("nB", 10);
  cfg.startSel = prefs.getUChar("st", 0);
  cfg.cMask = prefs.getUInt("cm", 0x13);
  cfg.cSec = prefs.getUChar("cs", 5);
  cfg.cInt = prefs.getUChar("ci", 1);
  cfg.mig = prefs.getUChar("mv", 0);
  cfg.pSec = prefs.getUChar("ps", 5);
  cfg.pFit = prefs.getUChar("pf", 0);
  cfg.pomW = prefs.getUChar("pw", 25);
  cfg.pomB = prefs.getUChar("pb", 5);
  cfg.pomL = prefs.getUChar("pl", 15);
  cfg.tmH = prefs.getUChar("tmh", 0);
  cfg.tmM = prefs.getUChar("tmm", 10);
  cfg.tmS = prefs.getUChar("tms", 0);
  if (cfg.tmH > 23 || cfg.tmM > 59 || cfg.tmS > 59 || (cfg.tmH + cfg.tmM + cfg.tmS) == 0) { cfg.tmH = 0; cfg.tmM = 10; cfg.tmS = 0; }
  cfg.aiProv = prefs.getUChar("aip", 1);
  cfg.aiUrl = prefs.getString("aiu", "https://text.pollinations.ai/openai");
  cfg.aiModel = prefs.getString("aim", "openai");
  cfg.aiKey = prefs.getString("aik", "");
  cfg.aiDaily = prefs.getBool("aid", true);
  if (cfg.aiProv > 1) cfg.aiProv = 1;
  cfg.flKm = prefs.getUChar("flk", 50);
  if (cfg.flKm < 10 || cfg.flKm > 250) cfg.flKm = 50;
  cfg.wc[0] = prefs.getUChar("wc0", 0);
  cfg.wc[1] = prefs.getUChar("wc1", 255);
  cfg.wc[2] = prefs.getUChar("wc2", 255);
  cfg.wc[3] = prefs.getUChar("wc3", 255);
  cfg.xpGoal = prefs.getUChar("xpg", 4);
  if (cfg.xpGoal < 1 || cfg.xpGoal > 12) cfg.xpGoal = 4;
  cfg.ssOn = prefs.getBool("sso", false);
  cfg.ssMin = prefs.getUChar("ssm", 5);
  cfg.ssSec = prefs.getUChar("sst", 5);
  cfg.ssMask = prefs.getUInt("ssk", 1);
  if (cfg.ssMin < 1 || cfg.ssMin > 60) cfg.ssMin = 5;
  if (cfg.ssSec < 1 || cfg.ssSec > 60) cfg.ssSec = 5;
  if (!cfg.ssMask) cfg.ssMask = 1;
  for (int i = 0; i < 3; i++) {
    char k[8];
    snprintf(k, sizeof k, "c%dn", i);
    noteCopy(cfg.cd[i].name, sizeof cfg.cd[i].name, prefs.getString(k, i == 0 ? "Natale" : "").c_str());
    snprintf(k, sizeof k, "c%dd", i);
    cdSetDate(cfg.cd[i], prefs.getString(k, i == 0 ? "2026-12-25" : ""));
    snprintf(k, sizeof k, "c%dy", i);
    cfg.cd[i].yearly = prefs.getBool(k, i == 0);
    snprintf(k, sizeof k, "t%d", i);
    noteCopy(cfg.nt[i].text, sizeof cfg.nt[i].text, prefs.getString(k, i == 0 ? "Ciao! :)" : "").c_str());
    snprintf(k, sizeof k, "t%do", i);
    cfg.nt[i].on = prefs.getBool(k, i == 0);
  }
  prefs.end();
  if (cfg.theme >= THEME_COUNT) cfg.theme = 0;
  if (cfg.bright < 5 || cfg.bright > 100) cfg.bright = 100;
  if (cfg.nBright > 100) cfg.nBright = 10;
  if (cfg.startSel > 26) cfg.startSel = 0;
  if (cfg.pSec < 1 || cfg.pSec > 60) cfg.pSec = 5;
  if (cfg.pFit > 1) cfg.pFit = 0;
  if (cfg.pomW < 1 || cfg.pomW > 99) cfg.pomW = 25;
  if (cfg.pomB < 1 || cfg.pomB > 99) cfg.pomB = 5;
  if (cfg.pomL < 1 || cfg.pomL > 99) cfg.pomL = 15;
  cfg.cMask &= 0xFFFFFu;
  if (cfg.cMask == 0) cfg.cMask = 0x13;
  if (cfg.cSec < 1 || cfg.cSec > 60) cfg.cSec = 5;
  if (cfg.cInt > 3) cfg.cInt = 1;
  if (cfg.nStart > 1439) cfg.nStart = 22 * 60;
  if (cfg.nEnd > 1439) cfg.nEnd = 7 * 60;
}

void saveCfg() {
  prefs.begin("nmtv", false);
  prefs.putString("ssid", cfg.ssid);
  prefs.putString("pass", cfg.pass);
  prefs.putString("cityq", cfg.cityq);
  prefs.putString("city", cfg.city);
  prefs.putFloat("lat", cfg.lat);
  prefs.putFloat("lon", cfg.lon);
  prefs.putBool("geo", cfg.geo);
  prefs.putBool("fmt12", cfg.fmt12);
  prefs.putBool("inv", cfg.inv);
  prefs.putBool("rot", cfg.rot);
  prefs.putUChar("theme", cfg.theme);
  prefs.putUChar("bri", cfg.bright);
  prefs.putBool("nOn", cfg.nightOn);
  prefs.putUShort("nS", cfg.nStart);
  prefs.putUShort("nE", cfg.nEnd);
  prefs.putUChar("nB", cfg.nBright);
  prefs.putUChar("st", cfg.startSel);
  prefs.putUInt("cm", cfg.cMask);
  prefs.putUChar("cs", cfg.cSec);
  prefs.putUChar("ci", cfg.cInt);
  prefs.putUChar("mv", cfg.mig);
  prefs.putUChar("ps", cfg.pSec);
  prefs.putUChar("pf", cfg.pFit);
  prefs.putUChar("pw", cfg.pomW);
  prefs.putUChar("pb", cfg.pomB);
  prefs.putUChar("pl", cfg.pomL);
  prefs.putUChar("tmh", cfg.tmH);
  prefs.putUChar("tmm", cfg.tmM);
  prefs.putUChar("tms", cfg.tmS);
  prefs.putUChar("aip", cfg.aiProv);
  prefs.putString("aiu", cfg.aiUrl);
  prefs.putString("aim", cfg.aiModel);
  prefs.putString("aik", cfg.aiKey);
  prefs.putBool("aid", cfg.aiDaily);
  prefs.putUChar("flk", cfg.flKm);
  for (int i = 0; i < 4; i++) {
    char k[6];
    snprintf(k, sizeof k, "wc%d", i);
    prefs.putUChar(k, cfg.wc[i]);
  }
  prefs.putUChar("xpg", cfg.xpGoal);
  prefs.putBool("sso", cfg.ssOn);
  prefs.putUChar("ssm", cfg.ssMin);
  prefs.putUChar("sst", cfg.ssSec);
  prefs.putUInt("ssk", cfg.ssMask);
  for (int i = 0; i < 3; i++) {
    char k[8], ds[12] = "";
    snprintf(k, sizeof k, "c%dn", i);
    prefs.putString(k, cfg.cd[i].name);
    snprintf(k, sizeof k, "c%dd", i);
    if (cfg.cd[i].m) snprintf(ds, sizeof ds, "%04u-%02u-%02u", (unsigned)cfg.cd[i].y, (unsigned)cfg.cd[i].m, (unsigned)cfg.cd[i].d);
    prefs.putString(k, ds);
    snprintf(k, sizeof k, "c%dy", i);
    prefs.putBool(k, cfg.cd[i].yearly);
    snprintf(k, sizeof k, "t%d", i);
    prefs.putString(k, cfg.nt[i].text);
    snprintf(k, sizeof k, "t%do", i);
    prefs.putBool(k, cfg.nt[i].on);
  }
  prefs.end();
}

// ----------------------------------------------------------- Globali --------
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
DNSServer dns;
String netOpts;   // elenco reti WiFi (solo in modalita' portale)

// ------------------------------------------------------- Dati condivisi -----
struct Weather {
  bool valid = false;
  float t = 0, feels = 0, hum = 0, press = 0, wind = 0;
  int code = 0;
  bool day = true;
  float tmin[4] = {0, 0, 0, 0};
  float tmax[4] = {0, 0, 0, 0};
  int dcode[4] = {0, 0, 0, 0};
  String dday[4];
  int srH = 0, srM = 0, ssH = 0, ssM = 0;
  int8_t trend = 0;        // tendenza: 1 in aumento, -1 in calo, 0 stabile
  float hh[24];            // temperatura oraria, prime 24 ore a partire da adesso
  bool hvalid = false;
  int hstart = 0;          // ora del giorno del primo valore
  time_t upd = 0;
};
struct Air {
  bool valid = false;
  float aqi = 0, pm25 = 0, pm10 = 0;
  float uv = 0, uvMax = 0;               // indice UV attuale e massimo previsto oggi
  float pGrass = 0, pOlive = 0, pBirch = 0;   // pollini in granuli/m3
  bool hasPollen = false;
  time_t upd = 0;
};

enum { ST_WIFI, ST_GEO, ST_FETCH, ST_OK, ST_ERR_GEO, ST_ERR_NET };

SemaphoreHandle_t mtx;
Weather Wsh, W;          // Wsh = scritto dal task dati, W = copia per il display
Air Ash, A;
volatile uint32_t gVer = 0;
volatile int gStatus = ST_WIFI;
volatile bool gPortal = false;   // vero durante il portale o un aggiornamento: il task dati si ferma
uint32_t seenVer = 0;

void setStatus(int s) {
  if (gStatus != s) {
    gStatus = s;
    gVer = gVer + 1;
  }
}

const char* statusText() {
  switch (gStatus) {
    case ST_WIFI:    return "Connessione WiFi...";
    case ST_GEO:     return "Ricerca citta...";
    case ST_FETCH:   return "Aggiorno i dati...";
    case ST_ERR_GEO: return "Citta non trovata";
    case ST_ERR_NET: return "Errore di rete";
    default:         return "";
  }
}

bool syncData() {
  if (gVer == seenVer) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  W = Wsh;
  A = Ash;
  seenVer = gVer;
  xSemaphoreGive(mtx);
  return true;
}

// ----------------------------------------------------------- Utilita' -------
String urlEnc(const String& s) {
  String o;
  char b[4];
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += (char)c;
    } else {
      snprintf(b, sizeof b, "%%%02X", c);
      o += b;
    }
  }
  return o;
}

String htmlEsc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

bool getTm(struct tm& t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &t);
  return true;
}

String fmtHM(int h, int m) {
  char b[16];
  if (cfg.fmt12) {
    int hh = h % 12;
    if (hh == 0) hh = 12;
    snprintf(b, sizeof b, "%d:%02d %s", hh, m, h >= 12 ? "PM" : "AM");
  } else {
    snprintf(b, sizeof b, "%02d:%02d", h, m);
  }
  return String(b);
}

String hourLabel(int h) {
  char b[8];
  if (cfg.fmt12) {
    int hh = h % 12;
    if (hh == 0) hh = 12;
    snprintf(b, sizeof b, "%d%s", hh, h >= 12 ? "PM" : "AM");
  } else {
    snprintf(b, sizeof b, "%02d", h);
  }
  return String(b);
}

int dowOf(int y, int m, int d) {   // 0 = domenica
  static const int tt[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + tt[m - 1] + d) % 7;
}

// ------------------------------------------------------ Luminosita' ---------
int curBright = -1;           // luminosita' applicata (-1 = ancora nessuna)

void setBacklight(uint8_t level) { ledcWrite(0, 255 - level); }   // retroilluminazione attiva bassa

// vero se l'ora attuale cade nella fascia notturna impostata
bool nightActive() {
  if (!cfg.nightOn || cfg.nStart == cfg.nEnd) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;        // ora non ancora sincronizzata
  struct tm ti;
  localtime_r(&now, &ti);
  int m = ti.tm_hour * 60 + ti.tm_min;
  if (cfg.nStart < cfg.nEnd) return m >= cfg.nStart && m < cfg.nEnd;
  return m >= cfg.nStart || m < cfg.nEnd;    // fascia a cavallo della mezzanotte
}

// applica la luminosita' di giorno o di notte (solo se cambiata, oppure con force)
void applyBrightness(bool force) {
  int target = nightActive() ? cfg.nBright : cfg.bright;
  if (force || target != curBright) {
    curBright = target;
    setBacklight((uint8_t)((target * 255 + 50) / 100));
  }
}

// ------------------------------------------------------------- Rete ---------
bool getJson(const String& url, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  return deserializeJson(doc, body) == DeserializationError::Ok;
}

// 0 = ok, 1 = errore di rete, 2 = citta' non trovata
int geocode() {
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEnc(cfg.cityq) +
               "&count=1&language=it&format=json";
  JsonDocument doc;
  if (!getJson(url, doc)) return 1;
  JsonArray r = doc["results"];
  if (r.isNull() || r.size() == 0) return 2;
  cfg.lat = r[0]["latitude"] | 0.0f;
  cfg.lon = r[0]["longitude"] | 0.0f;
  String nm = r[0]["name"].as<String>();
  cfg.city = nm.length() ? nm : cfg.cityq;
  cfg.geo = true;
  saveCfg();
  return 0;
}

bool fetchWeather() {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.lat, 4) +
               "&longitude=" + String(cfg.lon, 4) +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
               "weather_code,pressure_msl,wind_speed_10m"
               "&hourly=temperature_2m"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
               "&timezone=Europe%2FRome&forecast_days=4&wind_speed_unit=kmh";
  JsonDocument doc;
  if (!getJson(url, doc)) return false;
  JsonObject cur = doc["current"];
  JsonObject d = doc["daily"];
  if (cur.isNull() || d.isNull()) return false;

  Weather w;
  w.valid = true;
  w.t     = cur["temperature_2m"] | 0.0f;
  w.feels = cur["apparent_temperature"] | 0.0f;
  w.hum   = cur["relative_humidity_2m"] | 0.0f;
  w.press = cur["pressure_msl"] | 0.0f;
  w.wind  = cur["wind_speed_10m"] | 0.0f;
  w.code  = cur["weather_code"] | 0;
  w.day   = (cur["is_day"] | 1) == 1;
  for (int i = 0; i < 4; i++) {
    w.dcode[i] = d["weather_code"][i] | 0;
    w.tmax[i]  = d["temperature_2m_max"][i] | 0.0f;
    w.tmin[i]  = d["temperature_2m_min"][i] | 0.0f;
    w.dday[i]  = d["time"][i].as<String>();
  }
  String sr = d["sunrise"][0].as<String>();   // "2026-11-15T06:55"
  String ss = d["sunset"][0].as<String>();
  if (sr.length() >= 16) { w.srH = sr.substring(11, 13).toInt(); w.srM = sr.substring(14, 16).toInt(); }
  if (ss.length() >= 16) { w.ssH = ss.substring(11, 13).toInt(); w.ssM = ss.substring(14, 16).toInt(); }

  // temperatura oraria: l'array parte da oggi alle 00:00, la prima ora utile e' quella attuale
  for (int i = 0; i < 24; i++) w.hh[i] = 0;
  String ct = cur["time"].as<String>();       // "2026-11-16T15:30"
  int h0 = (ct.length() >= 13) ? ct.substring(11, 13).toInt() : 0;
  if (h0 < 0 || h0 > 23) h0 = 0;
  JsonArray ha = doc["hourly"]["temperature_2m"];
  if (!ha.isNull() && (int)ha.size() >= h0 + 24) {
    for (int i = 0; i < 24; i++) w.hh[i] = ha[h0 + i] | 0.0f;
    w.hvalid = true;
    w.hstart = h0;
    const float dT = w.hh[3] - w.t;          // previsione tra 3 ore rispetto a adesso
    w.trend = dT >= 0.7f ? 1 : (dT <= -0.7f ? -1 : 0);
  }

  time_t n = time(nullptr);
  w.upd = (n > 1700000000) ? n : 0;

  xSemaphoreTake(mtx, portMAX_DELAY);
  Wsh = w;
  gVer = gVer + 1;
  xSemaphoreGive(mtx);
  return true;
}

bool fetchAir() {
  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + String(cfg.lat, 4) +
               "&longitude=" + String(cfg.lon, 4) +
               "&current=european_aqi,pm10,pm2_5,uv_index,grass_pollen,olive_pollen,birch_pollen"
               "&daily=uv_index_max&forecast_days=1&timezone=Europe%2FRome";
  JsonDocument doc;
  if (!getJson(url, doc)) return false;
  JsonObject cur = doc["current"];
  if (cur.isNull()) return false;

  Air a;
  a.valid = true;
  a.aqi  = cur["european_aqi"] | 0.0f;
  a.pm25 = cur["pm2_5"] | 0.0f;
  a.pm10 = cur["pm10"] | 0.0f;
  a.uv    = cur["uv_index"] | 0.0f;
  a.uvMax = doc["daily"]["uv_index_max"][0] | 0.0f;
  a.hasPollen = !(cur["grass_pollen"].isNull() && cur["olive_pollen"].isNull() && cur["birch_pollen"].isNull());
  a.pGrass = cur["grass_pollen"] | 0.0f;
  a.pOlive = cur["olive_pollen"] | 0.0f;
  a.pBirch = cur["birch_pollen"] | 0.0f;
  time_t n = time(nullptr);
  a.upd = (n > 1700000000) ? n : 0;

  xSemaphoreTake(mtx, portMAX_DELAY);
  Ash = a;
  gVer = gVer + 1;
  xSemaphoreGive(mtx);
  return true;
}

// ------------------------------------------------------------ Cripto (dati) --
// Candele da Binance (mirror pubblico dei dati di mercato, nessuna chiave API).
static const int CR_COUNT = 20;   // criptovalute disponibili
static const int CR_N = 44;       // candele per grafico

struct CoinInfo { const char* sym; const char* name; uint32_t col; };
static const CoinInfo COINS[CR_COUNT] = {
  {"BTC", "Bitcoin", 0xF7931A}, {"ETH", "Ethereum", 0x627EEA}, {"BNB", "BNB", 0xF3BA2F},
  {"XRP", "XRP", 0xC8D2E1},     {"SOL", "Solana", 0x9945FF},   {"DOGE", "Dogecoin", 0xC2A633},
  {"ADA", "Cardano", 0x3C6EE6}, {"TRX", "TRON", 0xEB0029},     {"AVAX", "Avalanche", 0xE84142},
  {"LINK", "Chainlink", 0x2A5ADA}, {"DOT", "Polkadot", 0xE6007A}, {"TON", "Toncoin", 0x0098EA},
  {"SHIB", "Shiba Inu", 0xFFA400}, {"LTC", "Litecoin", 0x78A0DC}, {"BCH", "Bitcoin Cash", 0x8DC447},
  {"XLM", "Stellar", 0xC8C8DC}, {"UNI", "Uniswap", 0xFF007A}, {"ATOM", "Cosmos", 0x6E78BE},
  {"NEAR", "NEAR", 0x00EC97},   {"POL", "Polygon", 0x8247E5}};
static const char* const CR_INTV[4] = {"5m", "15m", "1h", "4h"};
static const uint16_t CR_INTMIN[4] = {5, 15, 60, 240};

struct CoinData {
  bool valid = false, err = false, tried = false;
  uint8_t n = 0, itv = 0;
  float o[CR_N], h[CR_N], l[CR_N], c[CR_N];
  time_t upd = 0;
  uint32_t tryAt = 0;
};
CoinData CD[CR_COUNT];
volatile uint32_t gCVer = 0;
volatile int gCryptoShow = -1;   // cripto mostrata adesso sul display (-1 = nessuna)

bool fetchCrypto(int id) {
  String url = String("https://data-api.binance.vision/api/v3/klines?symbol=") + COINS[id].sym +
               "USDT&interval=" + CR_INTV[cfg.cInt] + "&limit=" + String(CR_N);
  JsonDocument doc;
  bool ok = getJson(url, doc);
  CoinData c;
  if (ok) {
    JsonArray a = doc.as<JsonArray>();
    int n = (int)a.size();
    if (n > CR_N) n = CR_N;
    if (n < 2) ok = false;
    else {
      for (int i = 0; i < n; i++) {
        JsonArray k = a[i];
        c.o[i] = (float)atof(k[1] | "0");
        c.h[i] = (float)atof(k[2] | "0");
        c.l[i] = (float)atof(k[3] | "0");
        c.c[i] = (float)atof(k[4] | "0");
      }
      c.n = (uint8_t)n;
      c.valid = true;
      c.itv = cfg.cInt;
      time_t now = time(nullptr);
      c.upd = (now > 1700000000) ? now : 0;
    }
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (ok) {
    c.tried = true;
    c.tryAt = millis();
    CD[id] = c;
  } else {
    CD[id].tried = true;
    CD[id].err = true;
    CD[id].tryAt = millis();
  }
  gCVer = gCVer + 1;
  xSemaphoreGive(mtx);
  return ok;
}

// quale cripto aggiornare: quella mostrata ogni 10 s, le altre selezionate ogni 90 s
int pickCrypto() {
  const int want = gCryptoShow;
  if (want < 0 || want >= CR_COUNT) return -1;
  const uint32_t now = millis();
  for (int k = 0; k < CR_COUNT; k++) {
    const int i = (want + k) % CR_COUNT;
    if (k > 0 && !((cfg.cMask >> i) & 1u)) continue;
    const CoinData& d = CD[i];
    const bool good = d.valid && d.itv == cfg.cInt;
    const uint32_t lim = good ? (k == 0 ? 10000UL : 90000UL) : 8000UL;
    if (!d.tried || (now - d.tryAt) > lim) return i;
  }
  return -1;
}

// ------------------------------------------------------------ Radar ISS (dati) -
// Posizione reale da Open Notify (HTTP, nessuna chiave); modello orbitale e prossimi passaggi calcolati qui.
// Tutto viene aggiornato dal task dati solo mentre la schermata Radar ISS e' aperta.
volatile bool gIssOpen = false;
static iss::Model issMsh = {false, 0, 0, 0, 0};     // modello condiviso (protetto da mtx)
static iss::Pass issNextSh = {}, issVisSh = {};
static bool issDoneSh = false;
static uint32_t issFixAt = 0;                       // quando e' arrivata l'ultima misura (secondi unix)
// stato interno del task dati
static float issN = iss::N_DEF, issPrevLat = 0, issAnchorU = 0;
static uint32_t issPrevT = 0, issAnchorT = 0, issScanAt = 0, issNextFetch = 0;
static bool issHaveAnchor = false;
static iss::Model issM = {false, 0, 0, 0, 0};
static iss::Scan issScan = {};

static void issApplyFix(uint32_t t, float lat, float lon) {
  bool asc = true, known = false;
  if (issPrevT && t > issPrevT + 4 && t - issPrevT < 900 && fabsf(lat - issPrevLat) > 0.0005f) {
    asc = lat > issPrevLat;
    known = true;
  } else if (issM.ok) {
    asc = iss::ascending(issM, t);
    known = true;
  }
  issPrevLat = lat;
  issPrevT = t;
  if (!known) return;                   // serve una seconda misura per capire se sale o scende
  iss::Model nm;
  iss::fit(t, lat, lon, asc, issN, nm);
  const float u = iss::argLat(nm, t);
  if (fabsf(cosf(u)) >= 0.35f) {
    if (!issHaveAnchor) {
      issHaveAnchor = true;
      issAnchorU = u;
      issAnchorT = t;
    } else {
      float ne;
      if (iss::refineN(issAnchorU, issAnchorT, u, t, issN, ne)) {
        issN = 0.5f * (issN + ne);
        nm.n = issN;
      }
      if (t - issAnchorT > 3UL * 3600UL) {
        issAnchorU = u;
        issAnchorT = t;
      }
    }
  }
  issM = nm;
  xSemaphoreTake(mtx, portMAX_DELAY);
  issMsh = nm;
  issFixAt = t;
  xSemaphoreGive(mtx);
}

static bool fetchIss() {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(client, "http://api.open-notify.org/iss-now.json")) return false;
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  const String body = http.getString();             // risposta di un centinaio di byte
  http.end();
  JsonDocument doc;
  if (!(deserializeJson(doc, body) == DeserializationError::Ok)) return false;
  const char* ls = doc["iss_position"]["latitude"] | "";
  const char* os = doc["iss_position"]["longitude"] | "";
  if (!ls[0] || !os[0]) return false;
  uint32_t ts = doc["timestamp"] | 0UL;
  if (ts < 1700000000UL) {
    const time_t n = time(nullptr);
    if (n < 1700000000) return false;
    ts = (uint32_t)n;
  }
  issApplyFix(ts, (float)atof(ls), (float)atof(os));
  return true;
}

// chiamata dal task dati ad ogni giro: misura ogni minuto (15 s finche' manca il modello) e ricerca dei passaggi
static void issStep() {
  if (!gIssOpen) return;
  if ((int32_t)(millis() - issNextFetch) >= 0) {
    const bool ok = fetchIss();
    issNextFetch = millis() + (ok ? (issM.ok ? 60000UL : 15000UL) : 30000UL);
  }
  const time_t tn = time(nullptr);
  if (!issM.ok || tn < 1700000000) return;
  const uint32_t nowT = (uint32_t)tn;
  const bool idle = issScan.st == iss::Scan::IDLE || issScan.st == iss::Scan::DONE;
  if (idle && (issScan.st == iss::Scan::IDLE || nowT - issScanAt > 1200 || (issNextSh.valid && nowT > issNextSh.rise + issNextSh.dur))) {
    iss::scanStart(issScan, issM, cfg.lat, cfg.lon, nowT, 48UL * 3600UL);
    issScanAt = nowT;
    xSemaphoreTake(mtx, portMAX_DELAY);
    issDoneSh = false;
    xSemaphoreGive(mtx);
  }
  if (issScan.st == iss::Scan::COARSE || issScan.st == iss::Scan::FINE) {
    if (iss::scanStep(issScan, 150)) {
      xSemaphoreTake(mtx, portMAX_DELAY);
      issNextSh = issScan.first;
      issVisSh = issScan.firstVis;
      issDoneSh = true;
      xSemaphoreGive(mtx);
    }
  }
}

// ------------------------------------------------------------ Bibi AI (dati) ---
// L'intelligenza artificiale gira su un servizio esterno (Claude di Anthropic oppure un servizio compatibile OpenAI):
// l'ESP32 manda solo una richiesta HTTPS con il contesto (meteo, aria, cripto) e la memoria della conversazione.
const char* condText(int c);   // definite piu' sotto
int aqiBand(float v);

static const char* const AI_MOODS[15] = {"felice", "amore", "festa", "sorpreso", "triste", "pensieroso", "sonno",
                                         "freddo", "caldo", "pioggia", "neve", "tempesta", "su", "giu", "mascherina"};
static const int AI_HIST = 12;                       // messaggi ricordati (6 scambi)
struct AiMsg {
  bool user = false;
  String raw;                                        // testo come lo ha scritto l'utente o come lo ha dato l'AI
};
static AiMsg aiHist[AI_HIST];
static int aiHistN = 0;
static volatile uint8_t aiState = 0;                 // 0 libero, 1 richiesta in coda, 2 in elaborazione
static volatile uint32_t aiVer = 0;                  // cambia ad ogni risposta o errore
static String aiQ;                                   // domanda in coda
static uint8_t aiQKind = 0;                          // 0 domanda dal web, 1 commento rapido, 2 frase del giorno
static String aiShort, aiLong, aiErr;                // ultima risposta (breve per il display, completa per la chat) ed eventuale errore
static uint8_t aiMood = 0;
static uint32_t aiReplyAt = 0;                       // millis() dell'ultima risposta
static String aiDailyText;                           // frase del giorno
static uint8_t aiDailyMood = 0;
static int32_t aiDailyKey = 0;                       // giorno della frase (anno*1000 + giorno dell'anno)
static uint32_t aiDailyTry = 0;
static bool aiLoaded = false;
static bool fsOk = false;                            // memoria interna (LittleFS) disponibile

static int32_t aiDayKey() {
  struct tm t;
  const time_t n = time(nullptr);
  if (n < 1700000000) return 0;
  localtime_r(&n, &t);
  return (t.tm_year + 1900) * 1000 + t.tm_yday;
}

static void aiSaveHist() {                           // memoria su file (sopravvive al riavvio)
  if (!fsOk) return;
  File f = LittleFS.open("/bibi.json", "w");
  if (!f) return;
  JsonDocument d;
  JsonArray a = d["h"].to<JsonArray>();
  for (int i = 0; i < aiHistN; i++) {
    JsonObject o = a.add<JsonObject>();
    o["u"] = aiHist[i].user ? 1 : 0;
    o["r"] = aiHist[i].raw;
  }
  String out;
  serializeJson(d, out);
  f.write((const uint8_t*)out.c_str(), out.length());
  f.close();
}

static void aiLoadHist() {
  aiLoaded = true;
  Preferences pf;                                    // frase del giorno salvata
  pf.begin("nmtv", true);
  aiDailyKey = pf.getInt("bdk", 0);
  aiDailyText = pf.getString("bdt", "");
  aiDailyMood = pf.getUChar("bdm", 0);
  pf.end();
  if (aiDailyMood > 14) aiDailyMood = 0;
  if (!fsOk || !LittleFS.exists("/bibi.json")) return;
  File f = LittleFS.open("/bibi.json", "r");
  if (!f) return;
  String body;
  while (f.available()) body += (char)f.read();
  f.close();
  JsonDocument d;
  if (deserializeJson(d, body) != DeserializationError::Ok) return;
  aiHistN = 0;
  for (JsonObject o : d["h"].as<JsonArray>()) {
    if (aiHistN >= AI_HIST) break;
    aiHist[aiHistN].user = (o["u"] | 0) != 0;
    aiHist[aiHistN].raw = String(o["r"] | "");
    aiHistN++;
  }
  while (aiHistN > 0 && !aiHist[0].user) {           // la memoria deve iniziare da una domanda
    for (int i = 1; i < aiHistN; i++) aiHist[i - 1] = aiHist[i];
    aiHistN--;
  }
}

static void aiPushHist(bool user, const String& raw) {
  if (aiHistN >= AI_HIST) {                          // scarta lo scambio piu' vecchio (due messaggi)
    for (int i = 2; i < aiHistN; i++) aiHist[i - 2] = aiHist[i];
    aiHistN -= 2;
  }
  aiHist[aiHistN].user = user;
  aiHist[aiHistN].raw = raw;
  aiHistN++;
}

void aiClearHist() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  aiHistN = 0;
  aiShort = "";
  aiLong = "";
  aiErr = "";
  aiVer = aiVer + 1;
  xSemaphoreGive(mtx);
  LittleFS.remove("/bibi.json");
}

// accoda una richiesta; falso se Bibi e' gia' impegnato
bool aiAsk(const String& q, uint8_t kind) {
  if (aiState != 0 || !q.length()) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  aiQ = q;
  aiQKind = kind;
  aiErr = "";
  aiState = 1;
  aiVer = aiVer + 1;
  xSemaphoreGive(mtx);
  return true;
}

static bool aiKeyOk() { return cfg.aiProv == 1 || cfg.aiKey.length() > 8; }

// contesto: ora, meteo, aria e cripto (dati veri del dispositivo)
static String aiContext(bool wantCrypto) {
  String c;
  char b[200];
  struct tm t;
  if (getTm(t)) {
    snprintf(b, sizeof b, "Oggi e' %s %d %s %d, sono le %02d:%02d. ", GIORNI[t.tm_wday], t.tm_mday, MESI[t.tm_mon], t.tm_year + 1900, t.tm_hour, t.tm_min);
    c += b;
  }
  c += "Citta: " + (cfg.geo && cfg.city.length() ? cfg.city : cfg.cityq) + ". ";
  if (wantCrypto) {                                  // servono prezzi freschi: li scarica ora
    int got = 0;
    for (int i = 0; i < CR_COUNT && got < 3; i++) {
      if (!((cfg.cMask >> i) & 1u)) continue;
      got++;
      if (!(CD[i].valid && (millis() - CD[i].tryAt) < 600000UL)) fetchCrypto(i);
    }
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  const Weather w = Wsh;
  const Air a = Ash;
  xSemaphoreGive(mtx);
  if (w.valid) {
    snprintf(b, sizeof b, "Meteo adesso: %.0f gradi, %s, percepita %.0f, umidita %.0f%%, vento %.0f km/h. ", w.t, condText(w.code), w.feels, w.hum, w.wind);
    c += b;
    for (int d = 0; d < 3; d++) {
      snprintf(b, sizeof b, "%s: %s, min %.0f max %.0f. ", d == 0 ? "Oggi" : (d == 1 ? "Domani" : "Dopodomani"), condText(w.dcode[d]), w.tmin[d], w.tmax[d]);
      c += b;
    }
  } else {
    c += "Dati meteo non ancora disponibili. ";
  }
  if (a.valid) {
    snprintf(b, sizeof b, "Qualita dell'aria: indice europeo %.0f (%s), PM2.5 %.0f, PM10 %.0f, UV %.0f. ", a.aqi, BANDTXT[aqiBand(a.aqi)], a.pm25, a.pm10, a.uv);
    c += b;
  } else {
    c += "Dati sull'aria non ancora disponibili. ";
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  int n = 0;
  String cr;
  for (int i = 0; i < CR_COUNT && n < 4; i++) {
    const CoinData& d = CD[i];
    if (!d.valid || d.n < 2 || d.o[0] <= 0) continue;
    const float last = d.c[d.n - 1], chg = (last - d.o[0]) / d.o[0] * 100.0f;
    snprintf(b, sizeof b, "%s %.*f USDT (%+.1f%%), ", COINS[i].sym, last >= 100 ? 0 : (last >= 1 ? 2 : 4), last, chg);
    cr += b;
    n++;
  }
  xSemaphoreGive(mtx);
  if (n) c += "Criptovalute (variazione sul periodo del grafico): " + cr + ". ";
  return c;
}

static String aiSystem(bool wantCrypto) {
  String s =
      "Sei Bibi, un simpatico barboncino nero che vive dentro un piccolo display da tavolo. Parli in italiano, in prima persona, "
      "con tono affettuoso, vivace e un po' buffo. Rispondi SEMPRE con una sola riga nel formato: [umore] frase breve ## risposta completa. "
      "La frase breve e' per il display: massimo 60 caratteri, senza emoji. La risposta completa e' per la chat: massimo 220 caratteri, senza emoji e senza a capo. "
      "Gli umori possibili sono: felice, amore, festa, sorpreso, triste, pensieroso, sonno, freddo, caldo, pioggia, neve, tempesta, su, giu, mascherina "
      "(scegli quello che rispecchia la risposta: ad esempio pioggia se piove, su se le cripto salgono). "
      "Quando commenti meteo, aria o cripto usa solo i dati reali qui sotto e non inventare numeri; se un dato manca dillo con simpatia. DATI: ";
  return s + aiContext(wantCrypto);
}

// "[umore] breve ## lunga" -> campi
static void aiParse(const String& raw, uint8_t& mood, String& sh, String& lg) {
  mood = 0;
  String t = raw;
  t.trim();
  if (t.length() && t[0] == '[') {
    const int e = t.indexOf(']');
    if (e > 0) {
      String m = t.substring(1, e);
      m.toLowerCase();
      for (int i = 0; i < 15; i++)
        if (m == AI_MOODS[i]) { mood = (uint8_t)i; break; }
      t = t.substring(e + 1);
      t.trim();
    }
  }
  const int sep = t.indexOf("##");
  if (sep >= 0) {
    sh = t.substring(0, sep);
    lg = t.substring(sep + 2);
  } else {
    sh = t;
    lg = t;
  }
  sh.trim();
  lg.trim();
  if (!lg.length()) lg = sh;
  if (!sh.length()) sh = lg;
}

// una richiesta al servizio AI; ritorna il testo grezzo della risposta (vuoto in caso di errore, con aiErr compilato)
static int aiLastCode = 0;                           // ultimo codice HTTP del servizio AI

// una chiamata con al massimo "keep" messaggi di memoria (le risposte vecchie sono inviate in forma compatta)
static String aiCallN(const String& q, bool wantCrypto, int keep) {
  JsonDocument d;
  const String sys = aiSystem(wantCrypto);
  JsonArray msgs;
  if (cfg.aiProv == 0) {
    d["model"] = cfg.aiModel.length() ? cfg.aiModel : String("claude-haiku-5-5");
    d["max_tokens"] = 400;
    d["system"] = sys;
    msgs = d["messages"].to<JsonArray>();
  } else {
    d["model"] = cfg.aiModel.length() ? cfg.aiModel : String("openai");
    d["max_tokens"] = 400;
    d["temperature"] = 0.9;
    msgs = d["messages"].to<JsonArray>();
    JsonObject so = msgs.add<JsonObject>();
    so["role"] = "system";
    so["content"] = sys;
  }
  if (keep > 0) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    int st = aiHistN > keep ? aiHistN - keep : 0;
    if (st < aiHistN && !aiHist[st].user) st++;        // la memoria inviata deve iniziare da una domanda
    for (int i = st; i < aiHistN; i++) {
      JsonObject o = msgs.add<JsonObject>();
      if (aiHist[i].user) {
        o["role"] = "user";
        o["content"] = aiHist[i].raw.substring(0, 120);
      } else {                                         // solo "[umore] frase breve": la parte lunga non serve come memoria
        uint8_t mood;
        String sh, lg;
        aiParse(aiHist[i].raw, mood, sh, lg);
        o["role"] = "assistant";
        o["content"] = String("[") + AI_MOODS[mood] + "] " + sh.substring(0, 100);
      }
    }
    xSemaphoreGive(mtx);
  }
  JsonObject uo = msgs.add<JsonObject>();
  uo["role"] = "user";
  uo["content"] = q;
  String body;
  serializeJson(d, body);

  String url = cfg.aiProv == 0 ? String("https://api.anthropic.com/v1/messages") : cfg.aiUrl;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(30000);
  if (!http.begin(client, url)) {
    aiErr = "Indirizzo del servizio AI non valido";
    return "";
  }
  http.addHeader("Content-Type", "application/json");
  if (cfg.aiProv == 0) {
    http.addHeader("x-api-key", cfg.aiKey);
    http.addHeader("anthropic-version", "2023-06-01");
  } else if (cfg.aiKey.length()) {
    http.addHeader("Authorization", "Bearer " + cfg.aiKey);
  }
  const int code = http.POST(body);
  aiLastCode = code;
  String resp = code > 0 ? http.getString() : String("");
  http.end();
  if (code != 200) {
    aiErr = code > 0 ? "Errore del servizio AI (HTTP " + String(code) + ")" : String("Servizio AI non raggiungibile");
    if (code == 401 || code == 403) aiErr = "Chiave API non valida o mancante";
    if (code == 402 || code == 429) aiErr = "Limite del servizio AI raggiunto (HTTP " + String(code) + "): riprova tra poco";
    return "";
  }
  JsonDocument r;
  if (deserializeJson(r, resp) != DeserializationError::Ok) {
    aiErr = "Risposta AI non leggibile";
    return "";
  }
  String text;
  if (cfg.aiProv == 0) text = String(r["content"][0]["text"] | "");
  else text = String(r["choices"][0]["message"]["content"] | "");
  text.trim();
  if (!text.length()) aiErr = "Risposta AI vuota";
  return text;
}

// con la memoria completa; se il servizio rifiuta la richiesta (402, 413, 429: troppo lunga o limite del servizio gratuito)
// riprova con meno memoria e, in ultimo, senza
static String aiCall(const String& q, bool wantCrypto, bool useHist) {
  static const int KEEP[3] = {6, 2, 0};
  String r;
  for (int i = useHist ? 0 : 2; i < 3; i++) {
    r = aiCallN(q, wantCrypto, KEEP[i]);
    if (r.length()) return r;
    if (aiLastCode != 402 && aiLastCode != 413 && aiLastCode != 429) break;
    delay(i == 0 ? 1500 : 4000);                       // una breve pausa prima di riprovare
  }
  return r;
}

// chiamata dal task dati: elabora la richiesta in coda e la frase del giorno
static void aiStep() {
  if (!aiLoaded) aiLoadHist();
  if (aiState == 0 && cfg.aiDaily && aiKeyOk() && millis() > 45000UL) {   // frase del giorno: la prima volta che si puo', dopo le 6
    struct tm t;
    const int32_t k = aiDayKey();
    if (k && getTm(t) && t.tm_hour >= 6 && aiDailyKey != k && (aiDailyTry == 0 || millis() - aiDailyTry > 600000UL)) {
      aiDailyTry = millis();
      aiAsk("Dimmi la tua frase del giorno per oggi: un pensiero originale, allegro e diverso dai giorni scorsi, magari legato al tempo o alla stagione.", 2);
    }
  }
  if (aiState != 1) return;
  aiState = 2;
  const String q = aiQ;
  const uint8_t kind = aiQKind;
  String ql = q;
  ql.toLowerCase();
  const bool wantCr = ql.indexOf("cripto") >= 0 || ql.indexOf("bitcoin") >= 0 || ql.indexOf("btc") >= 0 || ql.indexOf("ethereum") >= 0 ||
                      ql.indexOf("eth") >= 0 || ql.indexOf("solana") >= 0 || ql.indexOf("mercat") >= 0;
  aiErr = "";
  String raw;
  if (!aiKeyOk()) aiErr = "Manca la chiave API: inseriscila nell'interfaccia web (Bibi AI)";
  else if (WiFi.status() != WL_CONNECTED) aiErr = "WiFi non connesso";
  else raw = aiCall(q, wantCr, kind != 2);
  if (kind == 2 && !raw.length()) aiErr = "";          // la frase del giorno e' un extra: se fallisce non disturba
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (raw.length()) {
    uint8_t mood;
    String sh, lg;
    aiParse(raw, mood, sh, lg);
    if (kind == 2) {
      aiDailyText = sh.length() ? sh : lg;
      aiDailyMood = mood;
      aiDailyKey = aiDayKey();
      Preferences pf;
      pf.begin("nmtv", false);
      pf.putInt("bdk", aiDailyKey);
      pf.putString("bdt", aiDailyText.substring(0, 120));
      pf.putUChar("bdm", mood);
      pf.end();
      if (aiHistN == 0 || !aiShort.length()) { aiShort = sh; aiLong = lg; aiMood = mood; aiReplyAt = millis(); }
    } else {
      aiPushHist(true, q);
      aiPushHist(false, raw);
      aiShort = sh;
      aiLong = lg;
      aiMood = mood;
      aiReplyAt = millis();
    }
  }
  aiState = 0;
  aiVer = aiVer + 1;
  xSemaphoreGive(mtx);
  if (raw.length() && kind != 2 && fsOk) aiSaveHist();
}

// ------------------------------------------------------------- Radar voli (dati) -
// Aerei vicini da adsb.fi (dati ADS-B aperti, nessuna chiave); destinazione da adsbdb (rotta del codice volo).
struct Plane {
  char cs[9] = "";
  char org[5] = "", dst[5] = "", city[20] = "";
  uint8_t rt = 0;                                    // 0 rotta da cercare, 1 trovata, 2 sconosciuta
  float lat = 0, lon = 0, alt = 0, gs = 0, dKm = 0;  // alt in metri, gs in km/h
  int16_t trk = 0;
  uint32_t t = 0;                                    // millis() della misura
};
static const int FL_MAX = 12;
static Plane flSh[FL_MAX];
static int flShN = 0;
static volatile uint32_t flVer = 0;
static volatile uint8_t flStat = 0;                  // 0 in attesa, 1 ok, 2 errore
static uint32_t flNextAt = 0;
volatile bool gFlOpen = false;
static char gFlWant[9] = "";                         // codice volo selezionato sul display (la sua rotta ha la precedenza)

struct RouteCache { char cs[9]; char org[5], dst[5], city[20]; uint8_t rt; };
static RouteCache rcache[24];
static int rcN = 0, rcPos = 0;

static bool flFetch() {
  const float nm = cfg.flKm / 1.852f;
  char u[160];
  bool ok = false;
  JsonDocument doc;
  for (int pass = 0; pass < 2 && !ok; pass++) {
    if (pass == 0) snprintf(u, sizeof u, "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%.0f", cfg.lat, cfg.lon, nm);
    else snprintf(u, sizeof u, "https://api.adsb.lol/v2/point/%.4f/%.4f/%.0f", cfg.lat, cfg.lon, nm);
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(12000);
    if (!http.begin(client, u)) continue;
    if (http.GET() != HTTP_CODE_OK) { http.end(); continue; }
    if (http.getSize() > 120000) { http.end(); continue; }   // troppi aerei: non c'e' memoria
    const String body = http.getString();
    http.end();
    JsonDocument filter;
    for (const char* key : {"aircraft", "ac"}) {
      JsonObject f = filter[key][0].to<JsonObject>();
      f["flight"] = true;
      f["hex"] = true;
      f["lat"] = true;
      f["lon"] = true;
      f["alt_baro"] = true;
      f["alt_geom"] = true;
      f["gs"] = true;
      f["track"] = true;
    }
    doc.clear();
    ok = deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok;
  }
  if (!ok) return false;
  JsonArray arr = doc["aircraft"].is<JsonArray>() ? doc["aircraft"].as<JsonArray>() : doc["ac"].as<JsonArray>();
  Plane tmp[FL_MAX];
  int n = 0;
  const float kx = 111.32f * cosf(cfg.lat * 0.0174533f), ky = 110.57f;
  const uint32_t now = millis();
  for (JsonObject a : arr) {
    if (!a["lat"].is<float>() || !a["lon"].is<float>()) continue;
    if (a["alt_baro"].is<const char*>()) continue;               // "ground": a terra
    const float lat = a["lat"], lon = a["lon"];
    const float dx = (lon - cfg.lon) * kx, dy = (lat - cfg.lat) * ky;
    const float dk = sqrtf(dx * dx + dy * dy);
    if (dk > cfg.flKm * 1.05f) continue;
    Plane p;
    const char* fl = a["flight"] | "";
    int j = 0;
    for (int i = 0; fl[i] && j < 8; i++)
      if (fl[i] != ' ') p.cs[j++] = fl[i];
    p.cs[j] = 0;
    if (!j) {                                                    // senza codice volo: usa l'indirizzo ICAO
      const char* hx = a["hex"] | "";
      for (int i = 0; hx[i] && j < 6; i++) p.cs[j++] = (char)toupper((unsigned char)hx[i]);
      p.cs[j] = 0;
      p.rt = 2;
    }
    p.lat = lat;
    p.lon = lon;
    const float altFt = a["alt_baro"].is<float>() ? (float)a["alt_baro"] : (float)(a["alt_geom"] | 0.0f);
    p.alt = altFt * 0.3048f;
    p.gs = (float)(a["gs"] | 0.0f) * 1.852f;
    p.trk = (int16_t)lroundf((float)(a["track"] | 0.0f));
    p.dKm = dk;
    p.t = now;
    // inserimento ordinato per distanza (i piu' vicini)
    int pos = n < FL_MAX ? n : FL_MAX - 1;
    if (n >= FL_MAX && dk >= tmp[FL_MAX - 1].dKm) continue;
    while (pos > 0 && tmp[pos - 1].dKm > dk) {
      tmp[pos] = tmp[pos - 1];
      pos--;
    }
    tmp[pos] = p;
    if (n < FL_MAX) n++;
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < n; i++) {                                  // conserva le rotte gia' trovate
    for (int k = 0; k < rcN; k++)
      if (!strcmp(rcache[k].cs, tmp[i].cs)) {
        strcpy(tmp[i].org, rcache[k].org);
        strcpy(tmp[i].dst, rcache[k].dst);
        strcpy(tmp[i].city, rcache[k].city);
        tmp[i].rt = rcache[k].rt;
        break;
      }
    flSh[i] = tmp[i];
  }
  flShN = n;
  flVer = flVer + 1;
  xSemaphoreGive(mtx);
  return true;
}

// cerca la rotta (partenza e destinazione) di un codice volo
static void flRoute() {
  int pick = -1;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int pass = 0; pass < 2 && pick < 0; pass++)
    for (int i = 0; i < flShN; i++) {
      if (flSh[i].rt != 0) continue;
      if (pass == 0 && strcmp(flSh[i].cs, gFlWant) != 0) continue;
      pick = i;
      break;
    }
  char cs[9] = "";
  if (pick >= 0) strcpy(cs, flSh[pick].cs);
  xSemaphoreGive(mtx);
  if (pick < 0) return;
  RouteCache rc = {};
  strcpy(rc.cs, cs);
  rc.rt = 2;
  JsonDocument doc;
  if (getJson(String("https://api.adsbdb.com/v0/callsign/") + cs, doc)) {
    JsonObject fr = doc["response"]["flightroute"];
    const char* o = fr["origin"]["iata_code"] | "";
    const char* d = fr["destination"]["iata_code"] | "";
    const char* c = fr["destination"]["municipality"] | "";
    if (d[0]) {
      strncpy(rc.org, o, 4);
      strncpy(rc.dst, d, 4);
      noteCopy(rc.city, sizeof rc.city, c);
      rc.rt = 1;
    }
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  RouteCache* slot = nullptr;
  for (int k = 0; k < rcN; k++)
    if (!strcmp(rcache[k].cs, cs)) slot = &rcache[k];
  if (!slot) {
    slot = &rcache[rcPos];
    rcPos = (rcPos + 1) % 24;
    if (rcN < 24) rcN++;
  }
  *slot = rc;
  for (int i = 0; i < flShN; i++)
    if (!strcmp(flSh[i].cs, cs)) {
      strcpy(flSh[i].org, rc.org);
      strcpy(flSh[i].dst, rc.dst);
      strcpy(flSh[i].city, rc.city);
      flSh[i].rt = rc.rt;
    }
  flVer = flVer + 1;
  xSemaphoreGive(mtx);
}

static void flStep() {
  if (!gFlOpen) return;
  if (!cfg.geo) return;
  if ((int32_t)(millis() - flNextAt) >= 0) {
    const bool ok = flFetch();
    flStat = ok ? 1 : 2;
    flVer = flVer + 1;
    flNextAt = millis() + (ok ? 15000UL : 45000UL);
    return;
  }
  flRoute();                                         // una ricerca di rotta per giro
}

void dataTask(void*) {
  uint32_t nextW = 0, nextA = 0, lastTry = millis();
  for (;;) {
    if (gPortal) {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      setStatus(ST_WIFI);
      if (millis() - lastTry > 60000UL) {
        lastTry = millis();
        WiFi.disconnect();
        WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    lastTry = millis();

    if (!cfg.geo) {
      setStatus(ST_GEO);
      int r = geocode();
      if (r == 0) {
        setStatus(ST_FETCH);
        nextW = 0;
        nextA = 0;
      } else {
        setStatus(r == 2 ? ST_ERR_GEO : ST_ERR_NET);
        vTaskDelay(30000 / portTICK_PERIOD_MS);
        continue;
      }
    }

    bool failed = false;
    if ((int32_t)(millis() - nextW) >= 0) {
      bool ok = fetchWeather();
      nextW = millis() + (ok ? WEATHER_MS : 60000UL);
      if (!ok) failed = true;
    }
    if ((int32_t)(millis() - nextA) >= 0) {
      bool ok = fetchAir();
      nextA = millis() + (ok ? AIR_MS : 60000UL);
      if (!ok) failed = true;
    }
    setStatus(failed ? ST_ERR_NET : ST_OK);
    const int ci = pickCrypto();       // criptovalute: solo quando la schermata Cripto e' aperta
    if (ci >= 0) fetchCrypto(ci);
    issStep();                         // Radar ISS: solo quando la schermata e' aperta
    flStep();                          // Radar voli: idem
    aiStep();                          // Bibi AI: richieste in coda e frase del giorno
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ------------------------------------------------------ Testo sul display ---
static int degW(int font) { return font == 4 ? 10 : 7; }

int strW(const String& s, int font) {
  int w = 0;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '~') w += degW(font);
    else w += tft.textWidth(String(c), font);
  }
  return w;
}

// Disegna testo (font 2 o 4). Il carattere '~' diventa il simbolo del grado.
// align: 0 = sinistra, 1 = centro, 2 = destra. Ritorna la larghezza disegnata.
int drawStr(const String& s, int x, int y, uint8_t font, uint8_t align, uint16_t fg, uint16_t bg) {
  tft.setTextColor(fg, bg);
  int w = strW(s, font);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  int fh = tft.fontHeight(font);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '~') {
      int r = (font == 4) ? 3 : 2;
      tft.fillRect(x, y, degW(font), fh, bg);
      tft.drawCircle(x + r + 2, y + (font == 4 ? 6 : 4), r, fg);
      if (font == 4) tft.drawCircle(x + r + 2, y + 6, r - 1, fg);
      x += degW(font);
    } else {
      x += tft.drawChar((uint16_t)c, x, y, font);
    }
  }
  return w;
}

// ------------------------------------------------------------ Icone ---------
enum { IC_CLEAR, IC_PARTLY, IC_CLOUD, IC_FOG, IC_DRIZZLE, IC_RAIN, IC_SNOW, IC_STORM };

static inline int S(float v, float k) { return (int)lroundf(v * k); }

void iconSun(int cx, int cy, float k) {
  for (int i = 0; i < 8; i++) {
    float a = i * 0.785398f;
    float ca = cosf(a), sa = sinf(a);
    int x0 = cx + (int)lroundf(ca * S(17, k)), y0 = cy + (int)lroundf(sa * S(17, k));
    int x1 = cx + (int)lroundf(ca * S(25, k)), y1 = cy + (int)lroundf(sa * S(25, k));
    tft.drawLine(x0, y0, x1, y1, C_SUN);
    tft.drawLine(x0 + 1, y0, x1 + 1, y1, C_SUN);
    tft.drawLine(x0, y0 + 1, x1, y1 + 1, C_SUN);
  }
  tft.fillCircle(cx, cy, S(12, k), C_SUN);
}

void iconMoon(int cx, int cy, float k, uint16_t bg) {
  tft.fillCircle(cx, cy, S(16, k), C_MOON);
  tft.fillCircle(cx + S(8, k), cy - S(5, k), S(14, k), bg);
}

void cloudShape(int cx, int cy, float k, uint16_t col) {
  tft.fillCircle(cx - S(14, k), cy + S(4, k), S(10, k), col);
  tft.fillCircle(cx, cy - S(5, k), S(14, k), col);
  tft.fillCircle(cx + S(15, k), cy + S(3, k), S(11, k), col);
  tft.fillRect(cx - S(14, k), cy + S(4, k), S(29, k), S(10, k), col);
}

void drops(int cx, int cy, float k, int len) {
  for (int i = 0; i < 3; i++) {
    int x = cx - S(12, k) + i * S(12, k);
    int y = cy + S(10, k) + (i % 2) * S(4, k);
    tft.drawLine(x, y, x - S(3, k), y + S(len, k), C_RAIN);
    tft.drawLine(x + 1, y, x - S(3, k) + 1, y + S(len, k), C_RAIN);
  }
}

void drawIcon(int cx, int cy, int type, bool day, float k, uint16_t bg) {
  switch (type) {
    case IC_CLEAR:
      if (day) iconSun(cx, cy, k * 1.05f);
      else iconMoon(cx, cy, k * 1.1f, bg);
      break;
    case IC_PARTLY:
      if (day) iconSun(cx - S(10, k), cy - S(10, k), k * 0.75f);
      else iconMoon(cx - S(10, k), cy - S(10, k), k * 0.8f, bg);
      cloudShape(cx + S(6, k), cy + S(8, k), k * 0.85f, C_CLOUD);
      break;
    case IC_CLOUD:
      cloudShape(cx, cy, k * 1.05f, C_CLOUD);
      break;
    case IC_FOG:
      cloudShape(cx, cy - S(10, k), k * 0.9f, C_CLOUDD);
      for (int i = 0; i < 3; i++) {
        int w = S(44, k) - (i % 2) * S(10, k);
        tft.fillRoundRect(cx - w / 2, cy + S(8, k) + i * S(7, k), w, S(3, k) + 1, 1, C_CLOUD);
      }
      break;
    case IC_DRIZZLE:
      cloudShape(cx, cy - S(8, k), k * 0.9f, C_CLOUD);
      drops(cx, cy - S(2, k), k * 0.9f, 5);
      break;
    case IC_RAIN:
      cloudShape(cx, cy - S(8, k), k * 0.9f, C_CLOUDD);
      drops(cx, cy - S(2, k), k * 0.9f, 10);
      break;
    case IC_SNOW:
      cloudShape(cx, cy - S(8, k), k * 0.9f, C_CLOUD);
      for (int i = 0; i < 3; i++) {
        int x = cx - S(12, k) + i * S(12, k);
        int y = cy + S(14, k) + (i % 2) * S(6, k);
        tft.fillCircle(x, y, S(2.6f, k) + 1, C_SNOW);
      }
      break;
    case IC_STORM:
      cloudShape(cx, cy - S(10, k), k * 0.9f, C_CLOUDD);
      tft.fillTriangle(cx + S(5, k), cy + S(2, k), cx - S(6, k), cy + S(16, k), cx + S(1, k), cy + S(16, k), C_BOLT);
      tft.fillTriangle(cx + S(1, k), cy + S(12, k), cx - S(3, k), cy + S(28, k), cx + S(9, k), cy + S(10, k), C_BOLT);
      break;
  }
}

void iconWind(int cx, int cy, float k, uint16_t c1, uint16_t c2, uint16_t bg) {
  // riga alta con ricciolo
  tft.fillRoundRect(cx - S(46, k), cy - S(14, k), S(64, k), S(6, k), S(3, k), c1);
  tft.fillCircle(cx + S(18, k), cy - S(23, k), S(11, k), c1);
  tft.fillCircle(cx + S(18, k), cy - S(23, k), S(6, k), bg);
  tft.fillRect(cx + S(6, k), cy - S(23, k), S(12, k), S(8, k), bg);
  // riga centrale con ricciolo
  tft.fillRoundRect(cx - S(58, k), cy + S(4, k), S(98, k), S(6, k), S(3, k), c2);
  tft.fillCircle(cx + S(40, k), cy - S(6, k), S(10, k), c2);
  tft.fillCircle(cx + S(40, k), cy - S(6, k), S(5, k), bg);
  tft.fillRect(cx + S(29, k), cy - S(6, k), S(11, k), S(8, k), bg);
  // riga bassa con ricciolo verso il basso
  tft.fillRoundRect(cx - S(38, k), cy + S(22, k), S(60, k), S(6, k), S(3, k), c1);
  tft.fillCircle(cx + S(22, k), cy + S(33, k), S(10, k), c1);
  tft.fillCircle(cx + S(22, k), cy + S(33, k), S(5, k), bg);
}

int iconFor(int c) {
  if (c == 0 || c == 1) return IC_CLEAR;
  if (c == 2) return IC_PARTLY;
  if (c == 3) return IC_CLOUD;
  if (c == 45 || c == 48) return IC_FOG;
  if (c >= 51 && c <= 57) return IC_DRIZZLE;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return IC_RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return IC_SNOW;
  if (c >= 95) return IC_STORM;
  return IC_CLOUD;
}

const char* condText(int c) {
  switch (c) {
    case 0: return "Sereno";
    case 1: return "Quasi sereno";
    case 2: return "Poco nuvoloso";
    case 3: return "Coperto";
    case 45: case 48: return "Nebbia";
    case 51: case 53: case 55: return "Pioviggine";
    case 56: case 57: return "Pioviggine gelata";
    case 61: return "Pioggia debole";
    case 63: return "Pioggia";
    case 65: return "Pioggia forte";
    case 66: case 67: return "Pioggia gelata";
    case 71: return "Neve debole";
    case 73: return "Neve";
    case 75: return "Neve forte";
    case 77: return "Granelli di neve";
    case 80: return "Rovesci";
    case 81: case 82: return "Rovesci forti";
    case 85: case 86: return "Rovesci di neve";
    case 95: return "Temporale";
    case 96: case 99: return "Temp. grandine";
    default: return "Variabile";
  }
}

// ------------------------------------------------------------ Gesti ---------
enum Gesture { G_NONE, G_SINGLE, G_DOUBLE, G_LONG, G_VLONG };

static const uint32_t LONG_MS    = 800;
static const uint32_t VLONG_MS   = 8000;
static const uint32_t DOUBLE_MS  = 350;
static const uint32_t MIN_TAP_MS = 30;

bool inSplash = true;      // durante la schermata iniziale i gesti sono ignorati

float touchBase = 60;
uint16_t thrOn = 50, thrOff = 55;
uint16_t touchRaw = 0;
bool tState = false;
uint32_t stuckSince = 0;
uint32_t warmUntil = 0;    // fino a questo istante il touch si calibra e i gesti sono ignorati
uint8_t belowCnt = 0;

// Soglie relative alla base a riposo: il tocco abbassa il valore di almeno il 10 %
// (minimo 4 unita'), il rilascio avviene a meta' di quella distanza (isteresi).
static void setThr() {
  float drop = touchBase * 0.10f;
  if (drop < 4) drop = 4;
  thrOn = (uint16_t)(touchBase - drop);
  thrOff = (uint16_t)(touchBase - drop * 0.5f);
}

uint16_t readTouch() {          // mediana di 5 letture (il WiFi disturba il sensore)
  uint16_t v[5];
  for (int i = 0; i < 5; i++) v[i] = touchRead(PIN_TOUCH);
  for (int i = 1; i < 5; i++) {
    uint16_t k = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
    v[j + 1] = k;
  }
  return v[2];
}

void calibrateTouch() {
  touchRead(PIN_TOUCH);          // la prima lettura puo' essere sporca
  delay(30);
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += readTouch();
    delay(8);
  }
  touchBase = sum / 16.0f;
  setThr();
  Serial.printf("Touch: base=%.0f soglia on=%u off=%u\n", touchBase, thrOn, thrOff);
}

volatile uint32_t lastTouchAct = 0;      // ultimo istante in cui il pulsante e' stato toccato (per il salvaschermo)

bool touchPressed() {
  uint16_t v = readTouch();
  touchRaw = v;
  uint32_t now = millis();

  // Fase di assestamento (schermata iniziale e WiFi in avvio): la base segue il valore, nessun gesto.
  if (inSplash || (int32_t)(now - warmUntil) < 0) {
    touchBase += ((float)v - touchBase) / 8.0f;
    setThr();
    tState = false;
    belowCnt = 0;
    return false;
  }

  if (!tState) {
    if (v < thrOn) {
      if (++belowCnt >= 2) { tState = true; belowCnt = 0; lastTouchAct = now; }   // 2 letture di fila
    } else {
      belowCnt = 0;
    }
  } else if (v > thrOff) {
    tState = false;
    belowCnt = 0;
  }

  if (!tState) {
    stuckSince = 0;
    if (v >= thrOff) {                       // segue lentamente la deriva a riposo
      touchBase += ((float)v - touchBase) / 256.0f;
      setThr();
    }
  } else {
    if (!stuckSince) stuckSince = now;
    else if (now - stuckSince > 12000) {     // "premuto" da troppo: ricalibra
      touchBase = v;
      setThr();
      tState = false;
      stuckSince = 0;
    }
  }
  return tState;
}

Gesture readGesture() {
  static bool down = false, longF = false, vlongF = false;
  static uint32_t downAt = 0, lastRel = 0;
  static uint8_t taps = 0;
  uint32_t now = millis();
  bool p = touchPressed();

  if (p && !down) {
    down = true;
    downAt = now;
    longF = false;
    vlongF = false;
  } else if (p && down) {
    if (!longF && now - downAt >= LONG_MS) {
      longF = true;
      taps = 0;
      return G_LONG;
    }
    if (longF && !vlongF && now - downAt >= VLONG_MS) {
      vlongF = true;
      return G_VLONG;
    }
  } else if (!p && down) {
    down = false;
    if (!longF && now - downAt >= MIN_TAP_MS) {
      taps++;
      lastRel = now;
      if (taps >= 2) {
        taps = 0;
        return G_DOUBLE;
      }
    }
  }
  if (!down && taps == 1 && now - lastRel >= DOUBLE_MS) {
    taps = 0;
    return G_SINGLE;
  }
  return G_NONE;
}

// ------------------------------------------------------ Stato schermate -----
enum { M_METEO, M_ARIA, M_LUNA, M_ISS, M_FLIGHT, M_CRIPTO, M_BIBI, M_BAI, M_FOTO, M_QUAD, M_FRAT, M_POMO, M_TIMER, M_COUNT, M_CITA, M_NUM, M_POST, M_SIS };
static const int NMENU = 18;         // Meteo, Aria, Fasi Lunari, Radar ISS, Radar Voli, Cripto, Bibigotchi, Bibi AI, Foto, Quadranti, Frattali, Pomodoro, Timer, Countdown, Citazione, Numeri, Post-it, Sistema
int menuIdx = 0;
int funcIdx = 0;
bool inFunc = false;
bool needFull = true;

int funcCount(int m) { return m == M_METEO ? 3 : (m == M_ARIA ? 2 : (m == M_QUAD ? 5 : (m == M_POMO ? 2 : 1))); }

String cityName() { return (cfg.geo && cfg.city.length()) ? cfg.city : cfg.cityq; }

const char* menuName(int m) {
  static const char* const N[NMENU] = {"Meteo", "Aria", "Fasi Lunari", "Radar ISS", "Radar Voli", "Cripto", "Bibigotchi", "Bibi AI", "Foto", "Quadranti", "Frattali", "Pomodoro", "Timer", "Countdown", "Citazione", "Numeri", "Post-it", "Sistema"};
  return (m >= 0 && m < NMENU) ? N[m] : "";
}

const char* funcName(int m, int f) {
  if (m == 0) return f == 0 ? "Principale" : (f == 1 ? "Prossimi 3 giorni" : "Grafico temperatura");
  if (m == 1) return f == 0 ? "Qualità dell'aria" : "Salute";
  if (m == M_TIMER) return "Timer a scalare";
  if (m == M_FLIGHT) return "Aerei vicino a te";
  if (m == M_BAI) return "Assistente AI";
  if (m == M_FRAT) return "Frattali animati";
  if (m == M_COUNT) return "Conto alla rovescia";
  if (m == M_CITA) return "Citazione del giorno";
  if (m == M_NUM) return "Numeri fortunati";
  if (m == M_POST) return "Una nota veloce";
  if (m == M_LUNA) return "Fase della luna";
  if (m == M_ISS) return "Posizione e passaggi";
  if (m == M_CRIPTO) return "Candele in tempo reale";
  if (m == M_BIBI) return "Il compagno Bibi";
  if (m == M_FOTO) return "Galleria foto";
  if (m == M_POMO) return f == 0 ? "Timer Pomodoro" : "Livelli e XP";
  if (m == M_QUAD) return f == 0 ? "Orologio semplice" : (f == 1 ? "Orologio analogico" : (f == 2 ? "Orologio flip" : (f == 3 ? "Orologio Matrix" : "Orologi nel mondo")));
  return "Matrix";
}

// scelta "schermata all'avvio" (interfaccia web): 0 = menu, 1..N = una funzione, in ordine
int selTotal() {
  int n = 0;
  for (int m = 0; m < NMENU; m++) n += funcCount(m);
  return n;
}
bool selToMF(int sel, int& mi, int& fi) {
  if (sel < 1) return false;
  int k = sel - 1;
  for (int m = 0; m < NMENU; m++) {
    if (k < funcCount(m)) { mi = m; fi = k; return true; }
    k -= funcCount(m);
  }
  return false;
}

String stateText() {
  String r;
  if (inSplash) r = "Avvio in corso...";
  else if (!inFunc) r = String("Menu: ") + menuName(menuIdx);
  else r = String(menuName(menuIdx)) + " - " + funcName(menuIdx, funcIdx);
  if (nightActive()) r += " (modalità notturna)";
  return r;
}

// ------------------------------------------------------------- MENU ---------
void iconSystem(int cx, int cy);   // definita con la schermata Sistema
void iconClock(int cx, int cy);    // definita con i Quadranti
void iconTimer(int cx, int cy);
void iconFlight(int cx, int cy);
void iconBibiAi(int cx, int cy);
void iconFract(int cx, int cy);
void iconCount(int cx, int cy);
void iconQuote(int cx, int cy);
void iconLucky(int cx, int cy);
void iconNote(int cx, int cy);
void iconMoon(int cx, int cy);     // definita con la schermata Fasi Lunari
void iconIss(int cx, int cy);      // definita con la schermata Radar ISS
void iconCrypto(int cx, int cy);   // definita con la schermata Cripto
void iconPhoto(int cx, int cy);    // definita con la schermata Foto
void iconBibi(int cx, int cy);     // definita con la schermata Bibigotchi
void iconPomodoro(int cx, int cy); // definita con la schermata Pomodoro

void menuFull() {
  tft.fillScreen(C_BG);
  const char* title = menuName(menuIdx);

  // titolo grande con linea neon
  tft.setTextSize(tft.textWidth(title, 4) * 2 <= 236 ? 2 : 1);   // titoli lunghi (Fasi Lunari) in dimensione normale
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(title, 120, tft.textWidth(title, 4) * 2 <= 236 ? 12 : 20, 4);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.fillRoundRect(70, 70, 100, 3, 1, C_ACC2);
  tft.fillRoundRect(95, 76, 50, 2, 1, C_ACC);

  // anelli decorativi dietro la grafica
  tft.drawCircle(120, 150, 68, c565(mixRGB(th->bg, th->a1, 110)));
  tft.drawCircle(120, 150, 70, c565(mixRGB(th->bg, th->a2, 80)));

  if (menuIdx == M_METEO) drawIcon(120, 150, IC_PARTLY, true, 1.6f, C_BG);
  else if (menuIdx == M_ARIA) iconWind(120, 146, 1.15f, C_AIR, RGB565(150, 235, 255), C_BG);
  else if (menuIdx == M_LUNA) iconMoon(120, 150);
  else if (menuIdx == M_ISS) iconIss(120, 150);
  else if (menuIdx == M_FLIGHT) iconFlight(120, 150);
  else if (menuIdx == M_BAI) iconBibiAi(120, 150);
  else if (menuIdx == M_CRIPTO) iconCrypto(120, 150);
  else if (menuIdx == M_BIBI) iconBibi(120, 150);
  else if (menuIdx == M_FOTO) iconPhoto(120, 150);
  else if (menuIdx == M_QUAD) iconClock(120, 150);
  else if (menuIdx == M_TIMER) iconTimer(120, 150);
  else if (menuIdx == M_FRAT) iconFract(120, 150);
  else if (menuIdx == M_COUNT) iconCount(120, 150);
  else if (menuIdx == M_CITA) iconQuote(120, 150);
  else if (menuIdx == M_NUM) iconLucky(120, 150);
  else if (menuIdx == M_POST) iconNote(120, 150);
  else if (menuIdx == M_POMO) iconPomodoro(120, 150);
  else iconSystem(120, 150);

  // pallini di scorrimento
  const float stp = (NMENU - 1) * 20 > 200 ? 200.0f / (NMENU - 1) : 20.0f;     // con tante schermate i pallini si stringono
  for (int i = 0; i < NMENU; i++) {
    int x = 120 + (int)lroundf((i - (NMENU - 1) / 2.0f) * stp);
    tft.fillCircle(x, 229, stp < 16 ? 4 : 5, i == menuIdx ? C_ACC : C_TRACK);
  }
}

// -------------------------------------------------------- Schermata iniziale -
uint32_t splashStart = 0;
String splashIp = "";
bool splashIpDrawn = false;

// logo JPEG nella flash (PROGMEM), decodificato a pezzi direttamente sul display
struct PgmJpg { const uint8_t* d; size_t n, pos; int ox, oy; };
static int pgmJpgRead(void* c, uint8_t* b, int len) {
  PgmJpg* j = (PgmJpg*)c;
  size_t r = j->n - j->pos;
  if ((size_t)len < r) r = (size_t)len;
  memcpy_P(b, j->d + j->pos, r);
  j->pos += r;
  return (int)r;
}
static void pgmJpgOut(void* c, int x, int y, int w, int h, uint16_t* px) {
  PgmJpg* j = (PgmJpg*)c;
  tft.pushImage(x + j->ox, y + j->oy, w, h, px);
}
static void drawPgmJpg(const uint8_t* d, size_t n, int x0, int y0) {
  PgmJpg src = {d, n, 0, x0, y0};
  JpegDecoder* dec = new (std::nothrow) JpegDecoder(pgmJpgRead, &src);
  if (!dec) return;
  tft.setSwapBytes(true);
  if (dec->begin() == JpegDecoder::OK) dec->decode(pgmJpgOut, &src, false);
  delete dec;
}

void splashStatic() {
  tft.fillScreen(C_BG);
  drawPgmJpg(LOGO_BOOT_JPG, LOGO_BOOT_JPG_LEN, 0, 34);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("V." FW_VERSION, 120, 92, 2);
  tft.setTextDatum(TL_DATUM);
  tft.fillRoundRect(40, 116, 160, 3, 1, C_ACC);
  tft.fillRoundRect(70, 122, 100, 2, 1, C_ACC2);
  drawStr("Non toccare il pulsante", 120, 192, 2, 1, C_DIM, C_BG);
  tft.fillRoundRect(20, 216, 200, 6, 3, C_TRACK);
  splashIpDrawn = false;
}

void splashTick() {
  static uint32_t lastDraw = 0;
  uint32_t now = millis();
  if (now - lastDraw < 100) return;
  lastDraw = now;
  uint32_t el = now - splashStart;

  bool con = (WiFi.status() == WL_CONNECTED);
  String ip = con ? WiFi.localIP().toString() : String("");
  if (!splashIpDrawn || ip != splashIp) {
    splashIp = ip;
    splashIpDrawn = true;
    tft.fillRect(0, 134, 240, 54, C_BG);
    if (con) {
      drawStr("Indirizzo IP", 120, 136, 2, 1, C_DIM, C_BG);
      drawStr(ip, 120, 156, 4, 1, C_TEXT, C_BG);
    } else {
      drawStr("Connessione WiFi...", 120, 150, 2, 1, C_DIM, C_BG);
    }
  }

  uint32_t e = el > 6000 ? 6000 : el;
  int w = (int)(e * 200UL / 6000UL);
  if (w > 0) tft.fillRoundRect(20, 216, w < 6 ? 6 : w, 6, 3, C_ACC);

  if (el >= 6000 && (con || el >= 12000)) {
    inSplash = false;
    warmUntil = now + 1500;      // ancora un attimo di assestamento dopo la connessione
    needFull = true;
    int sm, sf;
    if (selToMF(cfg.startSel, sm, sf)) {   // avvio diretto in una funzione scelta dal web
      menuIdx = sm;
      funcIdx = sf;
      inFunc = true;
    }
  }
}

// ------------------------------------------------------------ METEO 1 -------
static int lastSec = -1;
static uint32_t rotIdx = 0xFFFFFFFF;

void drawDate(const struct tm* t) {
  tft.fillRect(0, 4, 240, 18, C_BG);
  if (!t) {
    drawStr("Sincronizzo l'ora...", 120, 4, 2, 1, C_DIM, C_BG);
    return;
  }
  char b[48];
  snprintf(b, sizeof b, "%s %d %s %d", GIORNI[t->tm_wday], t->tm_mday, MESI[t->tm_mon], t->tm_year + 1900);
  drawStr(b, 120, 4, 2, 1, C_ACC2, C_BG);
}

void drawClock(const struct tm* t) {
  String s;
  bool pm = false;
  uint16_t col = C_TEXT;
  if (!t) {
    s = "00:00";
    col = C_TRACK;
  } else {
    int h = t->tm_hour;
    char b[16];
    if (cfg.fmt12) {
      pm = h >= 12;
      h %= 12;
      if (h == 0) h = 12;
      snprintf(b, sizeof b, "%d:%02d", h, t->tm_min);
    } else {
      snprintf(b, sizeof b, "%02d:%02d", h, t->tm_min);
    }
    s = b;
  }
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(col, C_BG);
  tft.setTextPadding(176);
  tft.drawString(s, 120, 24, 7);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);

  int w = tft.textWidth(s, 7);
  if (t && (t->tm_sec & 1)) {                        // i due punti lampeggiano ogni secondo
    const int ci = s.indexOf(':');
    if (ci > 0) {
      const int cxl = 120 - w / 2 + tft.textWidth(s.substring(0, ci), 7);
      tft.fillRect(cxl, 24, tft.textWidth(":", 7), 50, C_BG);
    }
  }
  int xr = 120 + w / 2 + 3;
  if (xr < 240) tft.fillRect(xr, 46, 240 - xr, 30, C_BG);
  if (t && cfg.fmt12) drawStr(pm ? "PM" : "AM", xr + 2, 50, 4, 0, C_ACC, C_BG);
}

void drawSecBar(const struct tm* t) {
  int w = 0;
  if (t) w = (t->tm_sec + 1) * 220 / 60;
  if (w > 0) tft.fillRect(10, 78, w, 6, C_ACC);
  if (w < 220) tft.fillRect(10 + w, 78, 220 - w, 6, C_TRACK);
}

// tre riquadri centrati (Min, Max, Umidita') con caratteri grandi
void statCards(int y, const char* l0, const String& v0, uint16_t c0,
               const char* l1, const String& v1, uint16_t c1,
               const char* l2, const String& v2, uint16_t c2, int h) {
  const int cx[3] = {44, 120, 196};
  const char* lab[3] = {l0, l1, l2};
  const String* val[3] = {&v0, &v1, &v2};
  const uint16_t cc[3] = {c0, c1, c2};
  const uint16_t edge = c565(mixRGB(th->card, th->a1, 110));
  for (int i = 0; i < 3; i++) {
    int x = cx[i] - 36;
    tft.fillRoundRect(x, y, 72, h, 10, C_CARD);
    tft.drawRoundRect(x, y, 72, h, 10, edge);
    drawStr(lab[i], cx[i], y + 3, 2, 1, C_DIM, C_CARD);
    drawStr(*val[i], cx[i], y + (h - 26) / 2 + 8, 4, 1, cc[i], C_CARD);
  }
}

// freccia di tendenza della temperatura: su = in aumento, giu = in calo, doppia freccia sinistra/destra = stabile
void drawTrend(int cx, int cy, int trend) {
  if (trend > 0) {
    tft.fillTriangle(cx, cy - 17, cx - 12, cy - 1, cx + 12, cy - 1, C_MAX);
    tft.fillRect(cx - 4, cy - 2, 9, 19, C_MAX);
  } else if (trend < 0) {
    tft.fillTriangle(cx, cy + 17, cx - 12, cy + 1, cx + 12, cy + 1, C_MIN);
    tft.fillRect(cx - 4, cy - 17, 9, 19, C_MIN);
  } else {
    tft.fillTriangle(cx - 16, cy, cx - 6, cy - 10, cx - 6, cy + 10, C_ACC);   // doppia freccia: stabile
    tft.fillTriangle(cx + 16, cy, cx + 6, cy - 10, cx + 6, cy + 10, C_ACC);
    tft.fillRect(cx - 7, cy - 3, 15, 7, C_ACC);
  }
}

void drawWeatherBlock() {
  tft.fillRect(0, 90, 240, 102, C_BG);
  if (!W.valid) {
    drawIcon(46, 116, IC_CLOUD, true, 0.95f, C_BG);
    drawStr("--", 100, 106, 4, 0, C_DIM, C_BG);
    statCards(145, "MIN", "--", C_DIM, "MAX", "--", C_DIM, "UMID.", "--", C_DIM, 46);
    return;
  }
  drawIcon(46, 116, iconFor(W.code), W.day, 0.95f, C_BG);

  int tv = (int)lroundf(W.t);
  bool neg = tv < 0;
  String ts = String(abs(tv));
  int tw = tft.textWidth(ts, 6);
  int total = tw + (neg ? 20 : 0);
  int x0 = 94;
  if (x0 + total + 34 + 30 > 238) x0 = 238 - 34 - 30 - total;   // lascia posto alla freccia di tendenza
  int y0 = 92;
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(TL_DATUM);
  if (neg) tft.fillRect(x0, y0 + 34, 14, 7, C_TEXT);
  tft.drawString(ts, x0 + (neg ? 20 : 0), y0, 6);
  int xe = x0 + total + 4;
  tft.drawCircle(xe + 5, y0 + 8, 4, C_TEXT);
  tft.drawCircle(xe + 5, y0 + 8, 3, C_TEXT);
  drawStr("C", xe + 14, y0 + 2, 4, 0, C_TEXT, C_BG);
  drawTrend(xe + 14 + 17 + (238 - (xe + 14 + 17)) / 2, y0 + 24, W.trend);

  statCards(145, "MIN", String((int)lroundf(W.tmin[0])) + "~", C_MIN,
            "MAX", String((int)lroundf(W.tmax[0])) + "~", C_MAX,
            "UMID.", String((int)lroundf(W.hum)) + "%", C_ACC, 46);
}

void drawCondLine() {
  tft.fillRoundRect(8, 194, 224, 30, 8, C_CARD);
  String s;
  uint16_t col = C_TEXT;
  if (!W.valid) {
    s = statusText();
    if (!s.length()) s = "In attesa dei dati";
    col = C_DIM;
  } else {
    switch (rotIdx % 6) {
      case 0: s = condText(W.code); col = C_TEXT; break;
      case 1: s = "Percepita " + String((int)lroundf(W.feels)) + "~"; col = C_MAX; break;
      case 2: s = "Vento " + String((int)lroundf(W.wind)) + " km/h"; col = C_ACC; break;
      case 3: s = "Atm " + String((int)lroundf(W.press)) + " hPa"; col = C_CLOUD; break;
      case 4: s = "Alba " + fmtHM(W.srH, W.srM); col = C_SUN; break;
      default: s = "Tramonto " + fmtHM(W.ssH, W.ssM); col = RGB565(255, 140, 100); break;
    }
  }
  uint8_t f = 4;
  if (strW(s, 4) > 212) f = 2;
  drawStr(s, 120, f == 4 ? 196 : 201, f, 1, col, C_CARD);
}

void drawFooter() {
  tft.fillRect(0, 224, 240, 16, C_BG);
  drawStr(cityName(), 8, 224, 2, 0, C_DIM, C_BG);
  if (gStatus != ST_OK || !W.valid) {
    String s = statusText();
    if (s.length()) drawStr(s, 232, 224, 2, 2, (gStatus == ST_ERR_GEO || gStatus == ST_ERR_NET) ? C_ERR : C_DIM, C_BG);
  } else if (W.upd) {
    struct tm u;
    localtime_r(&W.upd, &u);
    drawStr("agg. " + fmtHM(u.tm_hour, u.tm_min), 232, 224, 2, 2, C_DIM, C_BG);
  }
}

void meteoFull() {
  tft.fillScreen(C_BG);
  struct tm t;
  bool ok = getTm(t);
  const struct tm* tp = ok ? &t : nullptr;
  lastSec = ok ? t.tm_sec : -1;
  rotIdx = (millis() / 4000) % 6;
  drawDate(tp);
  drawClock(tp);
  drawSecBar(tp);
  drawWeatherBlock();
  drawCondLine();
  drawFooter();
}

void meteoTick(bool dataChanged) {
  struct tm t;
  bool ok = getTm(t);
  if (ok && t.tm_sec != lastSec) {
    static int lastDay = -1;
    if (lastSec == -1 || t.tm_mday != lastDay) {   // primo orario valido o cambio giorno
      drawDate(&t);
      lastDay = t.tm_mday;
    }
    lastSec = t.tm_sec;
    drawClock(&t);
    drawSecBar(&t);
  }
  uint32_t r = (millis() / 4000) % 6;
  if (r != rotIdx) {
    rotIdx = r;
    drawCondLine();
  }
  if (dataChanged) {
    drawWeatherBlock();
    drawCondLine();
    drawFooter();
  }
}

// ------------------------------------------------------------ METEO 2 -------
void previsioniFull() {
  tft.fillScreen(C_BG);
  drawStr("Prossimi 3 giorni", 120, 3, 2, 1, C_ACC2, C_BG);
  if (!W.valid) {
    String s = statusText();
    if (!s.length()) s = "In attesa dei dati";
    drawStr(s, 120, 110, 4, 1, C_DIM, C_BG);
    return;
  }
  for (int i = 1; i <= 3; i++) {
    int y = 22 + (i - 1) * 72;
    tft.fillRoundRect(6, y, 228, 66, 10, C_CARD);
    drawIcon(38, y + 33, iconFor(W.dcode[i]), true, 0.62f, C_CARD);

    String label;
    if (i == 1) {
      label = "Domani";
    } else {
      int yy = W.dday[i].substring(0, 4).toInt();
      int mm = W.dday[i].substring(5, 7).toInt();
      int dd = W.dday[i].substring(8, 10).toInt();
      if (mm >= 1 && mm <= 12) label = String(GIORNI[dowOf(yy, mm, dd)]) + " " + String(dd);
      else label = "--";
    }
    drawStr(label, 72, y + 6, 4, 0, C_TEXT, C_CARD);
    drawStr(condText(W.dcode[i]), 72, y + 40, 2, 0, C_DIM, C_CARD);
    drawStr(String((int)lroundf(W.tmax[i])) + "~", 228, y + 6, 4, 2, C_MAX, C_CARD);
    drawStr(String((int)lroundf(W.tmin[i])) + "~", 228, y + 36, 4, 2, C_MIN, C_CARD);
  }
}

// ------------------------------------------------------------ METEO 3 -------
// Grafico della temperatura nelle prossime 24 ore
void graficoFull() {
  tft.fillScreen(C_BG);
  drawStr("Temperatura - 24 ore", 120, 3, 2, 1, C_ACC2, C_BG);
  if (!W.valid || !W.hvalid) {
    String s = statusText();
    if (!s.length()) s = "In attesa dei dati";
    drawStr(s, 120, 110, 4, 1, C_DIM, C_BG);
    return;
  }

  float mn = W.hh[0], mx = W.hh[0];
  int imn = 0, imx = 0;
  for (int i = 1; i < 24; i++) {
    if (W.hh[i] < mn) { mn = W.hh[i]; imn = i; }
    if (W.hh[i] > mx) { mx = W.hh[i]; imx = i; }
  }
  float lo = floorf(mn) - 1.0f, hi = ceilf(mx) + 1.0f;
  if (hi - lo < 4.0f) {
    float m = (hi + lo) / 2.0f;
    lo = m - 2.0f;
    hi = m + 2.0f;
  }

  const int X0 = 18, X1 = 222, Y0 = 42, Y1 = 140;
  const int NX = X1 - X0 + 1;
  static int16_t ys[205];

  tft.fillRoundRect(6, 22, 228, 148, 12, C_CARD);
  tft.drawRoundRect(6, 22, 228, 148, 12, c565(mixRGB(th->card, th->a1, 110)));

  // area sotto la curva
  const uint16_t areaCol = c565(mixRGB(th->card, th->a1, 70));
  for (int x = X0; x <= X1; x++) {
    float f = (float)(x - X0) / (float)(X1 - X0) * 23.0f;
    int i = (int)f;
    if (i > 22) i = 22;
    float fr = f - (float)i;
    float v = W.hh[i] + (W.hh[i + 1] - W.hh[i]) * fr;
    int y = Y1 - (int)lroundf((v - lo) / (hi - lo) * (float)(Y1 - Y0));
    ys[x - X0] = (int16_t)y;
    tft.drawFastVLine(x, y, Y1 - y + 1, areaCol);
  }

  // griglia tratteggiata
  const uint16_t gridCol = c565(mixRGB(th->card, th->dim, 120));
  for (int g = 0; g <= 3; g++) {
    int y = Y0 + (Y1 - Y0) * g / 3;
    for (int x = X0; x <= X1; x += 8) tft.drawFastHLine(x, y, 3, gridCol);
  }

  // curva (spessore 2)
  for (int x = 1; x < NX; x++) {
    tft.drawLine(X0 + x - 1, ys[x - 1], X0 + x, ys[x], C_ACC);
    tft.drawLine(X0 + x - 1, ys[x - 1] + 1, X0 + x, ys[x] + 1, C_ACC);
  }

  // ora attuale
  tft.fillCircle(X0, ys[0], 5, C_ACC2);
  tft.drawCircle(X0, ys[0], 5, C_TEXT);

  // minimo e massimo con il valore
  int xmx = X0 + (X1 - X0) * imx / 23, xmn = X0 + (X1 - X0) * imn / 23;
  int ymx = ys[xmx - X0], ymn = ys[xmn - X0];
  tft.fillCircle(xmx, ymx, 3, C_MAX);
  tft.fillCircle(xmn, ymn, 3, C_MIN);
  int lx = xmx < 30 ? 30 : (xmx > 210 ? 210 : xmx);
  drawStr(String((int)lroundf(mx)) + "~", lx, ymx - 20, 2, 1, C_MAX, C_CARD);
  lx = xmn < 30 ? 30 : (xmn > 210 ? 210 : xmn);
  drawStr(String((int)lroundf(mn)) + "~", lx, ymn - 20, 2, 1, C_MIN, C_CARD);

  // etichette delle ore
  const int idx[4] = {0, 6, 12, 18};
  for (int k = 0; k < 4; k++) {
    int x = X0 + (X1 - X0) * idx[k] / 23;
    int xl = x < 30 ? 30 : x;
    drawStr(hourLabel((W.hstart + idx[k]) % 24), xl, 148, 2, 1, C_DIM, C_CARD);
  }

  statCards(176, "ADESSO", String((int)lroundf(W.t)) + "~", C_TEXT,
            "MIN", String((int)lroundf(mn)) + "~", C_MIN,
            "MAX", String((int)lroundf(mx)) + "~", C_MAX, 56);
}

// ------------------------------------------------------------- ARIA ---------
void ringSeg(int cx, int cy, int r0, int r1, float a0, float a1, uint16_t col) {
  const float step = 3.0f, D = 0.0174533f;
  for (float a = a0; a > a1 + 0.01f; a -= step) {
    float b = a - step;
    if (b < a1) b = a1;
    float ca = cosf(a * D), sa = sinf(a * D), cb = cosf(b * D), sb = sinf(b * D);
    int x0 = cx + (int)lroundf(r0 * ca), y0 = cy - (int)lroundf(r0 * sa);
    int x1 = cx + (int)lroundf(r1 * ca), y1 = cy - (int)lroundf(r1 * sa);
    int x2 = cx + (int)lroundf(r1 * cb), y2 = cy - (int)lroundf(r1 * sb);
    int x3 = cx + (int)lroundf(r0 * cb), y3 = cy - (int)lroundf(r0 * sb);
    tft.fillTriangle(x0, y0, x1, y1, x2, y2, col);
    tft.fillTriangle(x0, y0, x2, y2, x3, y3, col);
  }
}

int aqiBand(float v) {
  if (v < 20) return 0;
  if (v < 40) return 1;
  if (v < 60) return 2;
  if (v < 80) return 3;
  if (v < 100) return 4;
  return 5;
}

void pmRow(int y, const char* label, float v, float scale, const float* thr) {
  int band = 0;
  while (band < 5 && v >= thr[band]) band++;
  drawStr(label, 10, y, 2, 0, C_TEXT, C_BG);
  char b[24];
  snprintf(b, sizeof b, "%.1f ug/m3", v);
  drawStr(b, 230, y, 2, 2, BANDCOL[band], C_BG);
  tft.fillRoundRect(10, y + 19, 220, 7, 3, C_TRACK);
  float f = v / scale;
  if (f > 1) f = 1;
  if (f < 0) f = 0;
  int w = (int)lroundf(220 * f);
  if (w < 6 && v > 0) w = 6;
  if (w > 0) tft.fillRoundRect(10, y + 19, w, 7, 3, BANDCOL[band]);
}

void airFull() {
  tft.fillScreen(C_BG);
  drawStr("EAQI", 8, 2, 2, 0, C_DIM, C_BG);
  const int cx = 120, cy = 116;
  for (int i = 0; i < 6; i++) ringSeg(cx, cy, 64, 96, 179.0f - 30 * i, 151.0f - 30 * i, BANDCOL[i]);

  if (!A.valid) {
    String s = statusText();
    if (!s.length()) s = "In attesa dei dati";
    drawStr(s, 120, 84, 4, 1, C_DIM, C_BG);
  } else {
    float v = A.aqi;
    if (v < 0) v = 0;
    if (v > 120) v = 120;
    float ang = (180.0f - 180.0f * v / 120.0f) * 0.0174533f;
    float ca = cosf(ang), sa = sinf(ang);
    int tx = cx + (int)lroundf(100 * ca), ty = cy - (int)lroundf(100 * sa);
    float bx = cx + 114 * ca, by = cy - 114 * sa;
    tft.fillTriangle(tx, ty,
                     (int)lroundf(bx + 7 * sa), (int)lroundf(by + 7 * ca),
                     (int)lroundf(bx - 7 * sa), (int)lroundf(by - 7 * ca), C_TEXT);

    int band = aqiBand(A.aqi);
    int val = (int)lroundf(A.aqi);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString(String(val), 120, val >= 100 ? 76 : 66, 7);
    tft.setTextDatum(TL_DATUM);
    drawStr(BANDTXT[band], 120, 126, 4, 1, BANDCOL[band], C_BG);

    static const float thPm25[5] = {10, 20, 25, 50, 75};
    static const float thPm10[5] = {20, 40, 50, 100, 150};
    pmRow(160, "PM2.5", A.pm25, 100, thPm25);
    pmRow(192, "PM10", A.pm10, 200, thPm10);
  }

  tft.fillRect(0, 224, 240, 16, C_BG);
  drawStr(cityName(), 8, 224, 2, 0, C_DIM, C_BG);
  if (gStatus != ST_OK || !A.valid) {
    String s = statusText();
    if (s.length()) drawStr(s, 232, 224, 2, 2, (gStatus == ST_ERR_GEO || gStatus == ST_ERR_NET) ? C_ERR : C_DIM, C_BG);
  } else if (A.upd) {
    struct tm u;
    localtime_r(&A.upd, &u);
    drawStr("agg. " + fmtHM(u.tm_hour, u.tm_min), 232, 224, 2, 2, C_DIM, C_BG);
  }
}

// ------------------------------------------------------------ ARIA 2 --------
// Salute: indice UV e pollini (graminacee, olivo, betulla), come in SmallTV Evolution
static const uint32_t UV_COL[5] = {0x3DDC84, 0xF2D338, 0xFF8A3D, 0xE5384F, 0xA24BD6};
static const char* const UV_NAME[5] = {"Basso", "Moderato", "Alto", "Molto alto", "Estremo"};
static const char* const UV_TIP[5] = {"Nessun rischio", "Metti la crema", "Crema e ombra", "Evita il sole", "Resta all'ombra"};
static const uint32_t PO_COL[5] = {0x6B7399, 0x3DDC84, 0xF2D338, 0xFF8A3D, 0xE5384F};
static const char* const PO_NAME[5] = {"Assenti", "Bassi", "Moderati", "Alti", "Molto alti"};

// livello UV: 0 basso (0-2), 1 moderato (3-5), 2 alto (6-7), 3 molto alto (8-10), 4 estremo (11+)
uint8_t uvLevel(float uv) { return uv < 3.0f ? 0 : uv < 6.0f ? 1 : uv < 8.0f ? 2 : uv < 11.0f ? 3 : 4; }

// livello pollini (granuli/m3): 0 assenti, 1 bassi, 2 moderati, 3 alti, 4 molto alti.
// Soglie indicative (tipo: 0 graminacee, 1 olivo, 2 betulla).
uint8_t pollenLevel(float v, uint8_t kind) {
  static const float T[3][3] = {{19, 50, 150}, {19, 50, 150}, {10, 70, 300}};  // fine "bassi", "moderati", "alti"
  if (v < 1.0f) return 0;
  const float* t = T[kind > 2 ? 2 : kind];
  return v < t[0] ? 1 : v < t[1] ? 2 : v < t[2] ? 3 : 4;
}

void healthFull() {
  const int oy = -8;
  const uint16_t cbg = C_CARD;
  tft.fillScreen(C_BG);
  drawStr("Salute", 120, 3, 2, 1, C_ACC2, C_BG);
  if (!A.valid) {
    String s = statusText();
    if (!s.length()) s = "In attesa dei dati";
    drawStr(s, 120, 110, 4, 1, C_DIM, C_BG);
    return;
  }
  tft.fillRoundRect(6, 25, 228, 209, 14, cbg);
  tft.drawRoundRect(6, 25, 228, 209, 14, c565(mixRGB(th->card, th->a1, 100)));
  const uint32_t inner = mixRGB(th->card, th->bg, 110);

  // --- indice UV: numero grande a sinistra, livello e consiglio a destra, scala a 5 fasce con indicatore
  const uint8_t ul = uvLevel(A.uv);
  const uint32_t uc = UV_COL[ul];
  char b[24];
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, cbg);
  tft.drawString("INDICE UV", 16, 40 + oy, 2);
  if (A.uvMax > 0.05f) {   // massimo previsto oggi (di notte l'indice attuale e' 0)
    char m[20];
    snprintf(m, sizeof(m), "max oggi %.1f", A.uvMax);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(m, 224, 40 + oy, 2);
    tft.setTextDatum(TL_DATUM);
  }
  tft.setTextDatum(MC_DATUM);
  snprintf(b, sizeof(b), A.uv < 10.0f ? "%.1f" : "%.0f", A.uv);
  tft.setTextColor(c565(uc), cbg);
  tft.drawString(b, 62, 80 + oy, 6);
  const char* nm = UV_NAME[ul];
  tft.setTextColor(c565(uc), cbg);
  tft.drawString(nm, 172, 68 + oy, tft.textWidth(nm, 4) <= 108 ? 4 : 2);
  tft.setTextColor(c565(mixRGB(th->dim, th->text, 110)), cbg);
  tft.drawString(UV_TIP[ul], 172, 94 + oy, 2);
  // scala 0..12: 5 fasce (0-3, 3-6, 6-8, 8-11, 11-12) con la fascia attuale accesa
  static const uint8_t EDGE[6] = {0, 3, 6, 8, 11, 12};
  const int sx = 16, sw = 208, sy = 112 + oy, sh = 8;
  for (int i = 0; i < 5; i++) {
    const int x0 = sx + sw * EDGE[i] / 12, x1 = sx + sw * EDGE[i + 1] / 12;
    tft.fillRoundRect(x0 + 1, sy, x1 - x0 - 2, sh, 3, c565(i == ul ? UV_COL[i] : mixRGB(th->card, UV_COL[i], 70)));
  }
  float up = A.uv > 12.0f ? 12.0f : A.uv;
  const int mx = sx + (int)(sw * up / 12.0f);
  tft.fillTriangle(mx - 5, sy + sh + 2, mx + 5, sy + sh + 2, mx, sy + sh - 3, 0xFFFF);

  tft.drawFastHLine(16, 134 + oy, 208, c565(mixRGB(th->card, th->dim, 70)));

  // --- pollini
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_DIM, cbg);
  tft.drawString("POLLINI", 16, 137 + oy, 2);
  if (A.hasPollen && pollenLevel(A.pGrass, 0) == 0 && pollenLevel(A.pOlive, 1) == 0 && pollenLevel(A.pBirch, 2) == 0) {
    tft.setTextDatum(TR_DATUM);
    tft.drawString("nessuno rilevato", 224, 137 + oy, 2);
    tft.setTextDatum(TL_DATUM);
  }
  if (!A.hasPollen) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(c565(mixRGB(th->dim, th->text, 110)), cbg);
    tft.drawString("Non disponibili", 120, 182, 4);
    tft.setTextColor(C_DIM, cbg);
    tft.drawString("fuori stagione o zona", 120, 208, 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }
  static const char* const LAB[3] = {"Graminacee", "Olivo", "Betulla"};
  const float val[3] = {A.pGrass, A.pOlive, A.pBirch};
  for (int i = 0; i < 3; i++) {
    const int y = 149 + i * 28, h = 24;
    const uint8_t lv = pollenLevel(val[i], (uint8_t)i);
    const uint32_t pc = PO_COL[lv];
    const uint16_t ib = c565(inner);
    tft.fillRoundRect(12, y, 216, h, 8, ib);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_TEXT, ib);
    tft.drawString(LAB[i], 20, y + h / 2 + 1, 2);
    // 4 tacche: tante accese quanto il livello
    for (int k = 0; k < 4; k++) tft.fillRoundRect(98 + k * 14, y + 7, 11, 10, 3, c565(k < lv ? pc : mixRGB(th->card, th->dim, 60)));
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(c565(pc), ib);
    tft.drawString(PO_NAME[lv], 222, y + h / 2 + 1, 2);
  }
  tft.setTextDatum(TL_DATUM);
}

// ------------------------------------------------------------ SISTEMA -------
// Pioggia di caratteri stile Matrix con un pannello di statistiche (WiFi, IP, segnale, uptime, memoria),
// come in SmallTV Evolution. Colonne da 8 px, righe da 10 px (30 x 24 celle). Nessun buffer di caratteri:
// il carattere di ogni cella deriva da un hash di colonna, riga e seme, quindi resta stabile mentre scende.
static const uint8_t SY_COLS = 30, SY_ROWS = 24;
static const int SY_PX = 14, SY_PY = 48, SY_PW = 212, SY_PH = 152;  // pannello (le celle che lo toccano non si disegnano)
static int8_t syHead[SY_COLS];
static uint8_t sySpeed[SY_COLS], syLen[SY_COLS], syCnt[SY_COLS], sySeed[SY_COLS];
static uint32_t syNextFrame = 0, syNextPanel = 0;
static bool syCursor = false;

static int syHX = SY_PX, syHY = SY_PY, syHW = SY_PW, syHH = SY_PH;   // area coperta dal pannello (non si disegna la pioggia)

static inline bool syCellHidden(int c, int r) {
  const int x = c * 8, y = r * 10;
  return x + 8 > syHX && x < syHX + syHW && y + 10 > syHY && y < syHY + syHH;
}

static char syGlyph(int c, int r, uint8_t seed) {
  static const char G[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-*<>=#$%&?!";
  uint32_t hs = (uint32_t)c * 2654435761UL ^ (uint32_t)r * 40503UL ^ (uint32_t)seed * 2246822519UL;
  hs ^= hs >> 15;
  hs *= 2246822519UL;
  hs ^= hs >> 13;
  return G[hs % (sizeof(G) - 1)];
}

static void syDrawCell(int c, int r, uint16_t color) {
  if (r < 0 || r >= SY_ROWS || syCellHidden(c, r)) return;
  tft.setTextColor(color, 0);
  tft.drawChar(syGlyph(c, r, sySeed[c]), c * 8 + 1, r * 10 + 1, 1);
}

static void syEraseCell(int c, int r) {
  if (r < 0 || r >= SY_ROWS || syCellHidden(c, r)) return;
  tft.fillRect(c * 8, r * 10, 8, 10, 0);
}

static void syResetColumn(int c, bool initial) {
  syHead[c] = (int8_t)(initial ? -(int)(random(0, SY_ROWS + 6)) : -(int)(random(1, 10)));
  sySpeed[c] = (uint8_t)random(1, 4);
  syLen[c] = (uint8_t)random(8, 17);
  syCnt[c] = 0;
  sySeed[c] = (uint8_t)random(1, 255);
}

static void sySignalText(char* out, size_t n, int rssi) {
  int q = 2 * (rssi + 100);
  q = q < 0 ? 0 : (q > 100 ? 100 : q);
  const char* w = q >= 75 ? "Ottimo" : (q >= 55 ? "Buono" : (q >= 35 ? "Discreto" : "Debole"));
  snprintf(out, n, "%s %d%%", w, q);
}

static void syPanel(bool full) {
  const uint16_t g1 = RGB565(0, 255, 70), g2 = RGB565(0, 170, 45), g3 = RGB565(0, 90, 25), wt = RGB565(210, 255, 220);
  if (full) {
    tft.fillRect(SY_PX, SY_PY, SY_PW, SY_PH, 0);
    tft.drawRect(SY_PX, SY_PY, SY_PW, SY_PH, g3);
    tft.drawRect(SY_PX + 1, SY_PY + 1, SY_PW - 2, SY_PH - 2, RGB565(0, 40, 12));
  }
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(wt, 0);
  tft.drawString("> SISTEMA", SY_PX + 10, SY_PY + 8, 2);
  tft.fillRect(SY_PX + 10 + 80, SY_PY + 10, 8, 12, syCursor ? g1 : 0);

  char ssid[24], ip[20], sig[28], up[20], ram[24], tmp[20];
  const bool con = (WiFi.status() == WL_CONNECTED);
  if (con) snprintf(ssid, sizeof(ssid), "%.14s", WiFi.SSID().c_str());
  else snprintf(ssid, sizeof(ssid), "non connesso");
  snprintf(ip, sizeof(ip), "%s", con ? WiFi.localIP().toString().c_str() : "--");
  if (!con) {
    snprintf(sig, sizeof(sig), "--");
  } else {
    const int rssi = WiFi.RSSI();
    sySignalText(tmp, sizeof(tmp), rssi);
    snprintf(sig, sizeof(sig), "%s %d dBm", tmp, rssi);
  }
  const uint32_t s = millis() / 1000UL, dd = s / 86400UL;
  if (dd > 0) snprintf(up, sizeof(up), "%lud %02lu:%02lu:%02lu", (unsigned long)dd, (unsigned long)((s / 3600) % 24), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  else snprintf(up, sizeof(up), "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  snprintf(ram, sizeof(ram), "%u KB (blk %u)", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));

  const char* L[5] = {"Wi-Fi", "IP", "Segnale", "Uptime", "Memoria"};
  const char* V[5] = {ssid, ip, sig, up, ram};
  for (int i = 0; i < 5; i++) {
    const int y = SY_PY + 36 + i * 22;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(g2, 0);
    tft.drawString(L[i], SY_PX + 10, y, 2);
    tft.setTextDatum(TR_DATUM);
    tft.setTextPadding(124);
    tft.setTextColor(i == 0 || i == 1 ? g1 : wt, 0);
    tft.drawString(V[i], SY_PX + SY_PW - 10, y, 2);
    tft.setTextPadding(0);
  }
  tft.setTextDatum(TL_DATUM);
}

void sysFull() {
  syHX = SY_PX; syHY = SY_PY; syHW = SY_PW; syHH = SY_PH;
  tft.fillScreen(0);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  for (int c = 0; c < SY_COLS; c++) syResetColumn(c, true);
  syCursor = true;
  syPanel(true);
  syNextFrame = millis() + 50;
  syNextPanel = millis() + 500;
}

static void syRain() {
  const uint32_t ms = millis();
  if ((int32_t)(ms - syNextFrame) >= 0) {
    syNextFrame = ms + 55;
    tft.setTextSize(1);
    for (int c = 0; c < SY_COLS; c++) {
      if (++syCnt[c] < sySpeed[c]) continue;
      syCnt[c] = 0;
      const int hd = ++syHead[c];
      syEraseCell(c, hd - syLen[c]);
      if (hd - syLen[c] > (int)SY_ROWS) {   // la scia e' uscita: nuova caduta
        syResetColumn(c, false);
        continue;
      }
      static const uint8_t DIST[5] = {1, 2, 4, 7, 10};
      static const uint8_t TRAIL[5][3] = {{0, 255, 70}, {0, 215, 55}, {0, 160, 40}, {0, 100, 28}, {0, 55, 16}};
      for (int k = 4; k >= 0; k--)
        if (DIST[k] < syLen[c]) syDrawCell(c, hd - DIST[k], RGB565(TRAIL[k][0], TRAIL[k][1], TRAIL[k][2]));
      syDrawCell(c, hd, RGB565(210, 255, 220));
    }
    yield();
  }
}

void sysTick() {
  syRain();
  const uint32_t ms = millis();
  if ((int32_t)(ms - syNextPanel) >= 0) {
    syNextPanel = ms + 500;
    syCursor = !syCursor;
    syPanel(false);
  }
}

// icona del menu: monitor con pioggia di caratteri verdi
void iconSystem(int cx, int cy) {
  const uint16_t scr = RGB565(0, 14, 4), g1 = RGB565(0, 255, 70), g2 = RGB565(0, 170, 45), g3 = RGB565(0, 90, 25);
  const uint16_t wt = RGB565(210, 255, 220);
  tft.fillRoundRect(cx - 54, cy - 42, 108, 74, 9, scr);
  tft.drawRoundRect(cx - 54, cy - 42, 108, 74, 9, g2);
  tft.drawRoundRect(cx - 53, cy - 41, 106, 72, 8, g3);
  tft.fillRect(cx - 6, cy + 32, 12, 7, g3);
  tft.fillRoundRect(cx - 26, cy + 38, 52, 5, 2, g3);
  tft.setTextSize(1);
  for (int i = 0; i < 9; i++) {
    const int x = cx - 48 + i * 11;
    const int len = 3 + (i * 5) % 4;
    const int head = cy - 20 + ((i * 7) % 5) * 8;
    for (int k = 0; k < len; k++) {
      const int y = head - k * 10;
      if (y < cy - 38 || y > cy + 24) continue;
      tft.setTextColor(k == 0 ? wt : (k == 1 ? g1 : (k == 2 ? g2 : g3)), scr);
      tft.drawChar(syGlyph(i, k, 7), x, y, 1);
    }
  }
}

// ------------------------------------------------------------ CRIPTO --------
// Una cripto alla volta con candele; si passa alla successiva ogni N secondi (web) o col tocco.
static int crList[CR_COUNT], crN = 0, crPos = 0;
static uint32_t crSwitchAt = 0, crSeen = 0xFFFFFFFF;
static CoinData crCur;
static int crShown = -1, crBarW = -1;

static const uint16_t CR_UP = RGB565(0, 230, 118), CR_DN = RGB565(255, 61, 87);

static void crBuild() {
  crN = 0;
  for (int i = 0; i < CR_COUNT; i++)
    if ((cfg.cMask >> i) & 1u) crList[crN++] = i;
  if (crN == 0) crList[crN++] = 0;
  if (crPos >= crN) crPos = 0;
}

static int crCoin() {
  crBuild();
  return crList[crPos];
}

void crStep(int d) {
  crBuild();
  crPos = (crPos + d + crN) % crN;
  crSwitchAt = millis();
}

static void fmtPrice(char* b, size_t n, float p) {
  if (p >= 1000) snprintf(b, n, "%.2f", p);
  else if (p >= 10) snprintf(b, n, "%.3f", p);
  else if (p >= 1) snprintf(b, n, "%.4f", p);
  else if (p >= 0.01f) snprintf(b, n, "%.5f", p);
  else snprintf(b, n, "%.8f", p);
}

static void fmtCompact(char* b, size_t n, float p) {
  if (p >= 100000) snprintf(b, n, "%.0fk", p / 1000);
  else if (p >= 1000) snprintf(b, n, "%.1fk", p / 1000);
  else if (p >= 10) snprintf(b, n, "%.1f", p);
  else if (p >= 1) snprintf(b, n, "%.3f", p);
  else if (p >= 0.01f) snprintf(b, n, "%.4f", p);
  else snprintf(b, n, "%.6f", p);
}

static void crHeader(int id) {
  const uint16_t ac = c565(COINS[id].col);
  tft.fillCircle(24, 24, 18, ac);
  tft.drawCircle(24, 24, 19, c565(mixRGB(th->bg, COINS[id].col, 140)));
  char l[2] = {COINS[id].sym[0], 0};
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(RGB565(10, 12, 24), ac);
  tft.drawString(l, 24, 25, 4);
  tft.setTextDatum(TL_DATUM);
  drawStr(COINS[id].sym, 50, 3, 4, 0, C_TEXT, C_BG);
  drawStr(COINS[id].name, 50, 29, 2, 0, C_DIM, C_BG);
}

static void crDots(int id) {
  tft.fillRect(0, 224, 240, 16, C_BG);
  if (crN < 2) return;
  const uint16_t ac = c565(COINS[id].col);
  for (int i = 0; i < crN; i++) {
    const int x = 120 + (int)lroundf((i - (crN - 1) / 2.0f) * 10);
    tft.fillCircle(x, 228, i == crPos ? 3 : 2, i == crPos ? ac : C_TRACK);
  }
}

static void crBar(bool force) {
  if (crN < 2) {
    if (force || crBarW != 0) { tft.fillRect(4, 236, 232, 3, C_BG); crBarW = 0; }
    return;
  }
  const uint32_t tot = (uint32_t)cfg.cSec * 1000UL;
  uint32_t el = millis() - crSwitchAt;
  if (el > tot) el = tot;
  const int w = (int)((tot - el) * 232UL / tot);
  if (!force && w == crBarW) return;
  crBarW = w;
  const uint16_t ac = c565(COINS[crList[crPos]].col);
  if (w > 0) tft.fillRect(4, 236, w, 3, ac);
  if (w < 232) tft.fillRect(4 + w, 236, 232 - w, 3, C_TRACK);
}

static void crChart(const CoinData& c, int id) {
  const int cx0 = 4, cy0 = 100, cw = 232, ch = 106;
  tft.fillRoundRect(cx0, cy0, cw, ch, 10, C_CARD);
  tft.drawRoundRect(cx0, cy0, cw, ch, 10, c565(mixRGB(th->card, th->a1, 90)));
  if (!(c.valid && c.itv == cfg.cInt && c.n >= 2)) {
    drawStr(c.err ? "Dati non disponibili" : "Carico i dati...", 120, cy0 + 40, 4, 1, c.err ? C_ERR : C_DIM, C_CARD);
    if (c.err) drawStr("controlla la connessione", 120, cy0 + 72, 2, 1, C_DIM, C_CARD);
    return;
  }
  float lo = c.l[0], hi = c.h[0];
  for (int i = 1; i < c.n; i++) {
    if (c.l[i] < lo) lo = c.l[i];
    if (c.h[i] > hi) hi = c.h[i];
  }
  const float rawLo = lo, rawHi = hi;
  float pad = (hi - lo) * 0.06f;
  if (pad < hi * 0.0005f) pad = hi * 0.0005f + 1e-9f;
  lo -= pad;
  hi += pad;
  const int y0 = cy0 + 9, y1 = cy0 + ch - 9;
  const float sc = (float)(y1 - y0) / (hi - lo);
  auto Y = [&](float v) { return y1 - (int)lroundf((v - lo) * sc); };

  // griglia tratteggiata
  const uint16_t gr = c565(mixRGB(th->card, th->dim, 70));
  for (int g = 0; g < 3; g++) {
    const int gy = y0 + g * (y1 - y0) / 2;
    for (int x = 58; x < 232; x += 6) tft.drawFastHLine(x, gy, 3, gr);
  }
  // candele
  const int xr = 232;
  for (int i = 0; i < c.n; i++) {
    const int xi = xr - (c.n - i) * 4;
    const bool up = c.c[i] >= c.o[i];
    const uint16_t col = up ? CR_UP : CR_DN;
    const int yh = Y(c.h[i]), yl = Y(c.l[i]);
    int yt = Y(c.o[i]), yb = Y(c.c[i]);
    if (yt > yb) { const int t = yt; yt = yb; yb = t; }
    if (yb - yt < 1) yb = yt + 1;
    tft.drawFastVLine(xi + 1, yh, yl - yh + 1, col);
    tft.fillRect(xi, yt, 3, yb - yt + 1, col);
  }
  // ultimo prezzo: linea tratteggiata e freccetta
  const float last = c.c[c.n - 1];
  const uint16_t lc = last >= c.o[c.n - 1] ? CR_UP : CR_DN;
  const int ly = Y(last);
  for (int x = 58; x < 232; x += 4) tft.drawPixel(x, ly, lc);
  tft.fillTriangle(50, ly - 4, 50, ly + 4, 57, ly, lc);
  // minimo e massimo
  char b[16];
  fmtCompact(b, sizeof b, rawHi);
  drawStr(b, 8, Y(rawHi) - 4, 2, 0, C_DIM, C_CARD);
  fmtCompact(b, sizeof b, rawLo);
  drawStr(b, 8, Y(rawLo) - 12, 2, 0, C_DIM, C_CARD);
  (void)id;
}

static void crData(const CoinData& c, int id) {
  const bool ok = c.valid && c.itv == cfg.cInt && c.n >= 2;
  const uint16_t ac = c565(COINS[id].col);
  // variazione sulla finestra di candele
  tft.fillRect(118, 2, 122, 42, C_BG);
  if (ok) {
    const float chg = (c.c[c.n - 1] - c.o[0]) / (c.o[0] > 0 ? c.o[0] : 1) * 100.0f;
    const bool up = chg >= 0;
    const uint16_t col = up ? CR_UP : CR_DN;
    char b[16];
    snprintf(b, sizeof b, fabsf(chg) >= 10 ? "%+.1f%%" : "%+.2f%%", chg);
    const int w = strW(b, 4) + 34;
    const int x = 236 - w;
    tft.fillRoundRect(x, 6, w, 30, 15, c565(mixRGB(th->bg, up ? 0x00E676 : 0xFF3D57, 60)));
    tft.drawRoundRect(x, 6, w, 30, 15, col);
    if (up) tft.fillTriangle(x + 15, 14, x + 9, 26, x + 21, 26, col);
    else tft.fillTriangle(x + 15, 28, x + 9, 16, x + 21, 16, col);
    drawStr(b, x + 26, 9, 4, 0, col, c565(mixRGB(th->bg, up ? 0x00E676 : 0xFF3D57, 60)));
  }
  // prezzo
  tft.fillRect(0, 48, 240, 50, C_BG);
  if (ok) {
    char b[20];
    fmtPrice(b, sizeof b, c.c[c.n - 1]);
    tft.setTextColor(C_TEXT, C_BG);
    tft.setTextDatum(TC_DATUM);
    if (tft.textWidth(String(b), 6) <= 224) {
      tft.drawString(b, 120, 50, 6);
    } else {
      drawStr(b, 120, 62, 4, 1, C_TEXT, C_BG);
    }
    tft.setTextDatum(TL_DATUM);
  } else {
    drawStr("--", 120, 62, 4, 1, C_DIM, C_BG);
  }
  crChart(c, id);
  // piede: finestra e ora dell'aggiornamento
  tft.fillRect(0, 208, 240, 15, C_BG);
  if (ok) {
    const uint32_t mins = (uint32_t)c.n * CR_INTMIN[cfg.cInt];
    char b[32];
    if (mins < 120) snprintf(b, sizeof b, "Ultimi %lu min", (unsigned long)mins);
    else if (mins < 2880) snprintf(b, sizeof b, "Ultime %lu ore", (unsigned long)(mins / 60));
    else snprintf(b, sizeof b, "Ultimi %lu giorni", (unsigned long)(mins / 1440));
    drawStr(b, 8, 208, 2, 0, C_DIM, C_BG);
    if (c.upd) {
      struct tm u;
      localtime_r(&c.upd, &u);
      snprintf(b, sizeof b, "agg. %02d:%02d:%02d", u.tm_hour, u.tm_min, u.tm_sec);
      drawStr(b, 232, 208, 2, 2, C_DIM, C_BG);
    }
  }
  (void)ac;
}

void cryptoFull() {
  crBuild();
  const int id = crList[crPos];
  crShown = id;
  crSwitchAt = millis();
  tft.fillScreen(C_BG);
  xSemaphoreTake(mtx, portMAX_DELAY);
  crCur = CD[id];
  crSeen = gCVer;
  xSemaphoreGive(mtx);
  crHeader(id);
  crData(crCur, id);
  crDots(id);
  crBar(true);
}

void cryptoTick() {
  crBuild();
  if (crN > 1 && millis() - crSwitchAt >= (uint32_t)cfg.cSec * 1000UL) {   // passa alla cripto successiva
    crStep(1);
    needFull = true;
    return;
  }
  const int id = crList[crPos];
  if (id != crShown) {
    needFull = true;
    return;
  }
  if (gCVer != crSeen) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    crCur = CD[id];
    crSeen = gCVer;
    xSemaphoreGive(mtx);
    crData(crCur, id);
  }
  static uint32_t barAt = 0;
  if (millis() - barAt >= 100) {
    barAt = millis();
    crBar(false);
  }
}

// icona del menu: candele giapponesi
void iconCrypto(int cx, int cy) {
  static const int8_t T[7] = {-4, -14, -8, -22, -12, -26, -20};   // inizio corpo (relativo)
  static const int8_t H[7] = {18, 22, 14, 26, 18, 24, 30};        // altezza corpo
  static const int8_t U[7] = {0, 1, 0, 1, 0, 1, 1};
  for (int i = 0; i < 7; i++) {
    const int x = cx - 42 + i * 14;
    const int yt = cy + T[i] + 10;
    const uint16_t col = U[i] ? CR_UP : CR_DN;
    tft.drawFastVLine(x + 4, yt - 8, H[i] + 16, col);
    tft.fillRect(x, yt, 9, H[i], col);
  }
  tft.drawFastHLine(cx - 50, cy + 46, 100, C_DIM);
}

// ------------------------------------------------------------ FASI LUNARI ----
// Disco lunare disegnato per pixel (mari, crateri, alone, stelle) con la fase di oggi, illuminazione e prossima luna piena.
static uint16_t mnCol(uint8_t r, uint8_t g, uint8_t b) { return RGB565(r, g, b); }
static void mnRow(int x, int y, int w, const uint16_t* px) { tft.pushImage(x, y, w, 1, (uint16_t*)px); }
static uint32_t mnNextDraw = 0;

static long mnDays(const struct tm& t) {          // giorni dal 1970 (calendario civile)
  long y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void moonFull() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  drawStr("FASI LUNARI", 120, 3, 4, 1, C_ACC, C_BG);
  mnNextDraw = millis() + 600000UL;               // ridisegna ogni 10 minuti
  const time_t now = time(nullptr);
  if (now < 1700000000) {
    drawStr("In attesa dell'ora", 120, 100, 4, 1, C_TEXT, C_BG);
    drawStr("(serve la connessione WiFi)", 120, 136, 2, 1, C_DIM, C_BG);
    mnNextDraw = millis() + 3000UL;
    return;
  }
  struct tm ti;
  localtime_r(&now, &ti);
  const moon::Phase p = moon::phaseAt((double)now);
  tft.setSwapBytes(true);
  moon::drawDisc(120, 82, 40, 60, (float)p.elong, th->bg, th->a1, mnCol, mnRow, 30);
  drawStr(moon::phaseName(p), 120, 143, 4, 1, C_TEXT, C_BG);

  const int cy = 174, ch = 62, cw = 111;
  const int cx[2] = {6, 123};
  const uint16_t edge = c565(mixRGB(th->card, th->a1, 100));
  for (int i = 0; i < 2; i++) {
    tft.fillRoundRect(cx[i], cy, cw, ch, 12, C_CARD);
    tft.drawRoundRect(cx[i], cy, cw, ch, 12, edge);
  }
  drawStr("ILLUMINATA", cx[0] + cw / 2, cy + 4, 2, 1, C_ACC, C_CARD);
  drawStr("LUNA PIENA", cx[1] + cw / 2, cy + 4, 2, 1, C_ACC, C_CARD);
  char buf[28];
  snprintf(buf, sizeof buf, "%d%%", (int)lroundf((float)p.lit * 100.0f));
  drawStr(buf, cx[0] + cw / 2, cy + 20, 4, 1, C_TEXT, C_CARD);
  drawStr(p.elong < 180.0 ? "in crescita" : "in calo", cx[0] + cw / 2, cy + 44, 2, 1, C_DIM, C_CARD);

  const double nf = moon::nextFullMoon((double)now);
  const time_t nt = (time_t)nf;
  struct tm tf;
  localtime_r(&nt, &tf);
  const long days = mnDays(tf) - mnDays(ti);
  if (days <= 0) snprintf(buf, sizeof buf, "oggi");
  else snprintf(buf, sizeof buf, "%d %.3s", tf.tm_mday, MESI[tf.tm_mon]);
  drawStr(buf, cx[1] + cw / 2, cy + 20, 4, 1, C_TEXT, C_CARD);
  if (days <= 0) snprintf(buf, sizeof buf, "alle %02d:%02d", tf.tm_hour, tf.tm_min);
  else if (days == 1) snprintf(buf, sizeof buf, "domani");
  else snprintf(buf, sizeof buf, "tra %ld giorni", days);
  drawStr(buf, cx[1] + cw / 2, cy + 44, 2, 1, C_DIM, C_CARD);
}

void moonTick() {
  if ((int32_t)(millis() - mnNextDraw) >= 0) moonFull();
}

// icona del menu: falce di luna con qualche stella
void iconMoon(int cx, int cy) {
  const uint16_t lune = RGB565(246, 238, 210);
  tft.fillCircle(cx - 4, cy, 40, lune);
  tft.fillCircle(cx + 14, cy - 8, 34, C_BG);
  tft.fillCircle(cx - 22, cy + 6, 4, RGB565(222, 212, 178));
  tft.fillCircle(cx - 14, cy + 22, 3, RGB565(222, 212, 178));
  tft.fillCircle(cx - 18, cy - 14, 2, RGB565(222, 212, 178));
  const uint16_t st = RGB565(255, 255, 255);
  tft.fillRect(cx + 28, cy - 30, 3, 3, st);
  tft.drawFastHLine(cx + 25, cy - 29, 9, c565(mixRGB(th->bg, 0xFFFFFF, 140)));
  tft.drawFastVLine(cx + 29, cy - 33, 9, c565(mixRGB(th->bg, 0xFFFFFF, 140)));
  tft.fillRect(cx + 34, cy + 14, 2, 2, st);
  tft.fillRect(cx + 18, cy + 34, 2, 2, st);
}

// ------------------------------------------------------------ RADAR ISS ------
// Mappa del mondo a punti con giorno/notte, traccia dell'orbita, posizione dell'ISS e prossimo passaggio sopra la citta'.
static const int ISS_X0 = 9, ISS_Y0 = 33;          // angolo della mappa (74 x 31 celle da 3 pixel)
static uint32_t issFrame = 0, issNextFrame = 0, issNextInfo = 0;
static float issSunLat = 0, issSunLon = 0;
static uint32_t issSunT = 0, issTrT = 0;
static float issColCos[iss::MAP_COLS];
static int16_t issTrX[48], issTrY[48];
static uint8_t issTrN = 0, issTrPast = 0;
// copia locale (letta sotto mutex) dei dati calcolati dal task dati
static iss::Model uM = {false, 0, 0, 0, 0};
static iss::Pass uNext = {}, uVis = {};
static bool uDone = false;

static void issSync() {
  xSemaphoreTake(mtx, portMAX_DELAY);
  uM = issMsh;
  uNext = issNextSh;
  uVis = issVisSh;
  uDone = issDoneSh;
  xSemaphoreGive(mtx);
}

static void issMapXY(float lat, float lon, int& x, int& y) {
  x = ISS_X0 + (int)((lon + 180.0f) / 360.0f * (iss::MAP_COLS * 3));
  y = ISS_Y0 + (int)((iss::MAP_LAT_TOP - lat) / iss::MAP_LAT_SPAN * (iss::MAP_ROWS * 3));
}

// orario di un passaggio: "21:47", oppure "Gio 05:12" se non e' oggi
static void issFmtWhen(char* out, size_t n, uint32_t t, const struct tm& nowTm) {
  time_t tt = (time_t)t;
  struct tm lt;
  localtime_r(&tt, &lt);
  if (lt.tm_yday == nowTm.tm_yday && lt.tm_year == nowTm.tm_year) snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
  else snprintf(out, n, "%s %02d:%02d", GIORNI[lt.tm_wday], lt.tm_hour, lt.tm_min);
}

static void issUpdateTrack(uint32_t now) {         // 12 punti indietro e 34 avanti, ogni 90 secondi
  issTrN = 0;
  issTrPast = 0;
  for (int k = -12; k < 34; k++) {
    float la, lo;
    iss::pos(uM, now + (int32_t)k * 90, la, lo);
    int x, y;
    issMapXY(la, lo, x, y);
    issTrX[issTrN] = (int16_t)x;
    issTrY[issTrN] = (int16_t)y;
    if (k < 0) issTrPast++;
    issTrN++;
  }
  issTrT = now;
}

static void issDrawMap(uint32_t now, uint32_t f) {
  const uint32_t cardRGB = th->card;
  const uint16_t cbg = c565(cardRGB);
  if (issSunT == 0 || now - issSunT > 120) {
    iss::sunPoint(now, issSunLat, issSunLon);
    issSunT = now;
    for (int c = 0; c < iss::MAP_COLS; c++) {
      const float lon = -180.0f + (c + 0.5f) * (360.0f / iss::MAP_COLS);
      issColCos[c] = cosf((lon - issSunLon) * iss::DEG);
    }
  }
  if (uM.ok && (issTrT != now || issTrN == 0)) issUpdateTrack(now);

  uint16_t oc[3], lc[3];
  oc[0] = c565(mixRGB(cardRGB, th->a1, 46));
  oc[1] = c565(mixRGB(cardRGB, th->a1, 30));
  oc[2] = c565(mixRGB(cardRGB, th->a1, 14));
  lc[0] = c565(mixRGB(cardRGB, th->a1, 215));
  lc[1] = c565(mixRGB(cardRGB, th->a1, 140));
  lc[2] = c565(mixRGB(cardRGB, th->a1, 70));
  const float sd = sinf(issSunLat * iss::DEG), cd = cosf(issSunLat * iss::DEG);
  static uint16_t line[iss::MAP_COLS * 3];
  tft.setSwapBytes(true);
  for (int r = 0; r < iss::MAP_ROWS; r++) {
    const float lat = iss::MAP_LAT_TOP - (r + 0.5f) * (iss::MAP_LAT_SPAN / iss::MAP_ROWS);
    const float A = sinf(lat * iss::DEG) * sd, B = cosf(lat * iss::DEG) * cd;
    for (int c = 0; c < iss::MAP_COLS; c++) {
      const float z = A + B * issColCos[c];
      const int lv = z > 0.05f ? 0 : (z > -0.10f ? 1 : 2);
      const uint16_t v = iss::landAt(c, r) ? lc[lv] : oc[lv];
      line[c * 3] = v;
      line[c * 3 + 1] = v;
      line[c * 3 + 2] = cbg;
    }
    tft.pushImage(ISS_X0, ISS_Y0 + r * 3, iss::MAP_COLS * 3, 1, line);
    tft.pushImage(ISS_X0, ISS_Y0 + r * 3 + 1, iss::MAP_COLS * 3, 1, line);
    tft.fillRect(ISS_X0, ISS_Y0 + r * 3 + 2, iss::MAP_COLS * 3, 1, cbg);
  }

  // la citta' impostata
  int cx, cy;
  issMapXY(cfg.lat, cfg.lon, cx, cy);
  const uint16_t yel = c565(0xFFD54A);
  tft.drawRect(cx - 2, cy - 2, 5, 5, yel);
  tft.drawPixel(cx, cy, yel);
  if (!uM.ok) return;

  // traccia: passato (fioca) e futuro (colorata)
  const uint16_t cPast = c565(mixRGB(cardRGB, th->a2, 90));
  for (int i = 0; i < issTrN; i++) {
    const bool past = i < issTrPast;
    const uint16_t c = past ? cPast : c565(mixRGB(cardRGB, th->a2, (uint8_t)(255 - (i - issTrPast) * 4)));
    tft.fillRect(issTrX[i] - 1, issTrY[i] - 1, 2, 2, c);
  }
  float la, lo;
  iss::pos(uM, now, la, lo);
  const float el = iss::elevFromCos(iss::cosPsi(cfg.lat, cfg.lon, la, lo));
  if (el >= 0) {                                  // sopra l'orizzonte: la citta' pulsa
    const int k = (int)(f % 3);
    tft.drawRect(cx - 4 - k, cy - 4 - k, 9 + 2 * k, 9 + 2 * k, yel);
  }
  // ISS: onda che si espande e icona del satellite
  int mx, my;
  issMapXY(la, lo, mx, my);
  const int rr = 4 + (int)(f % 8);
  tft.drawCircle(mx, my, rr, c565(mixRGB(cardRGB, th->a2, (uint8_t)(255 - rr * 20))));
  tft.fillRect(mx - 6, my - 1, 4, 3, C_ACC);
  tft.fillRect(mx + 3, my - 1, 4, 3, C_ACC);
  tft.fillRect(mx - 1, my - 1, 3, 3, 0xFFFF);
  tft.drawFastVLine(mx, my - 3, 2, 0xFFFF);
}

static void issDrawInfo(uint32_t now) {            // pannello delle informazioni (una volta al secondo)
  tft.fillRect(10, 134, 220, 102, C_CARD);
  tft.setTextDatum(TL_DATUM);
  char b[44];
  if (!uM.ok) {
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(WiFi.status() == WL_CONNECTED ? "Cerco l'ISS..." : "Manca la rete", 12, 146, 4);
    tft.setTextColor(C_DIM, C_CARD);
    tft.drawString("La posizione arriva in circa", 12, 186, 2);
    tft.drawString("un minuto (Open Notify)", 12, 206, 2);
    return;
  }
  struct tm nowTm;
  time_t nt = (time_t)now;
  localtime_r(&nt, &nowTm);
  float la, lo;
  iss::pos(uM, now, la, lo);
  const float cp = iss::cosPsi(cfg.lat, cfg.lon, la, lo);
  const float el = iss::elevFromCos(cp);
  const float dist = acosf(cp > 1.0f ? 1.0f : cp) * iss::RE;
  const uint16_t soft = c565(mixRGB(th->dim, th->text, 110));

  snprintf(b, sizeof b, "%.1f%c %.1f%c", fabsf(la), la >= 0 ? 'N' : 'S', fabsf(lo), lo >= 0 ? 'E' : 'W');
  tft.setTextColor(C_TEXT, C_CARD);
  tft.drawString(b, 12, 135, 4);
  snprintf(b, sizeof b, "%d km da %.10s", (int)(dist + 0.5f), cityName().c_str());
  tft.setTextColor(soft, C_CARD);
  tft.drawString(b, 12, 163, 2);
  tft.drawFastHLine(12, 182, 216, c565(mixRGB(th->card, th->a1, 90)));

  if (el >= iss::MIN_EL) {
    const bool blink = ((issFrame / 3) & 1) == 0;
    tft.setTextColor(blink ? c565(0xFFD54A) : 0xFFFF, C_CARD);
    tft.drawString("SOPRA DI TE!", 12, 192, 4);
    snprintf(b, sizeof b, "elevazione %d gradi", (int)(el + 0.5f));
    tft.setTextColor(C_ACC, C_CARD);
    tft.drawString(b, 12, 222, 2);
    return;
  }
  tft.setTextColor(C_DIM, C_CARD);
  tft.drawString("Prossimo passaggio", 12, 186, 2);
  if (!(uNext.valid && (uNext.rise + uNext.dur) > now)) {
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(uDone ? "Nessuno in 48 ore" : "calcolo...", 12, 205, 4);
    return;
  }
  char when[20];
  issFmtWhen(when, sizeof when, uNext.rise, nowTm);
  tft.setTextColor(C_TEXT, C_CARD);
  tft.drawString(when, 12, 203, 4);
  const int mins = (int)(((int32_t)(uNext.rise - now) + 30) / 60);
  if (mins >= 60) snprintf(b, sizeof b, "tra %d h %d min", mins / 60, mins % 60);
  else snprintf(b, sizeof b, "tra %d min", mins < 0 ? 0 : mins);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_ACC, C_CARD);
  tft.drawString(b, 226, 190, 2);
  tft.setTextDatum(TL_DATUM);
  snprintf(b, sizeof b, "max %d gr - %d min", uNext.maxEl, (uNext.dur + 30) / 60);
  tft.setTextColor(soft, C_CARD);
  tft.drawString(b, 12, 224, 2);
  if (uNext.vis) {
    tft.fillRoundRect(150, 205, 76, 20, 8, c565(0x1FB65A));
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0xFFFF, c565(0x1FB65A));
    tft.drawString("VISIBILE", 188, 215, 2);
    tft.setTextDatum(TL_DATUM);
  } else if (uVis.valid) {
    char w2[20];
    issFmtWhen(w2, sizeof w2, uVis.rise, nowTm);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(c565(0x4FD08A), C_CARD);
    tft.drawString("visibile", 226, 204, 2);
    tft.drawString(w2, 226, 219, 2);
    tft.setTextDatum(TL_DATUM);
  }
}

void issFull() {
  gIssOpen = true;
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  drawStr("RADAR ISS", 120, 3, 4, 1, C_ACC, C_BG);
  const uint16_t edge = c565(mixRGB(th->card, th->a1, 100));
  tft.fillRoundRect(6, 31, 228, 99, 10, C_CARD);
  tft.drawRoundRect(6, 31, 228, 99, 10, edge);
  tft.fillRoundRect(6, 131, 228, 107, 10, C_CARD);
  tft.drawRoundRect(6, 131, 228, 107, 10, edge);
  issSync();
  const time_t now = time(nullptr);
  issFrame = 0;
  issTrN = 0;
  issSunT = 0;
  if (now < 1700000000) {
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString("In attesa dell'ora", 12, 160, 4);
    issNextFrame = millis() + 1000;
    issNextInfo = millis() + 1000;
    return;
  }
  issDrawMap((uint32_t)now, 0);
  issDrawInfo((uint32_t)now);
  issNextFrame = millis() + 200;
  issNextInfo = millis() + 1000;
}

void issTick() {
  const uint32_t ms = millis();
  if ((int32_t)(ms - issNextFrame) < 0) return;
  const time_t now = time(nullptr);
  if (now < 1700000000) { issNextFrame = ms + 1000; return; }
  issNextFrame = ms + 200;
  issFrame++;
  issSync();
  issDrawMap((uint32_t)now, issFrame);
  if ((int32_t)(ms - issNextInfo) >= 0) {
    issNextInfo = ms + 1000;
    issDrawInfo((uint32_t)now);
  }
}

// icona del menu: la stazione spaziale con i pannelli solari
void iconIss(int cx, int cy) {
  const uint16_t pan = RGB565(42, 107, 255), panL = RGB565(120, 170, 255), met = RGB565(214, 220, 232);
  tft.fillRect(cx - 52, cy - 2, 104, 4, RGB565(150, 158, 178));
  for (int s = -1; s <= 1; s += 2) {
    for (int k = 0; k < 2; k++) {
      const int x = s < 0 ? cx - 50 + k * 14 : cx + 22 + k * 14;
      tft.fillRect(x, cy - 28, 12, 22, pan);
      tft.fillRect(x, cy + 6, 12, 22, pan);
      tft.drawRect(x, cy - 28, 12, 22, panL);
      tft.drawRect(x, cy + 6, 12, 22, panL);
      tft.drawFastHLine(x, cy - 17, 12, panL);
      tft.drawFastHLine(x, cy + 17, 12, panL);
    }
  }
  tft.fillRoundRect(cx - 12, cy - 11, 24, 22, 5, met);
  tft.fillRect(cx - 8, cy - 5, 16, 3, RGB565(120, 130, 150));
  tft.fillCircle(cx, cy + 4, 3, C_ACC2);
  tft.drawFastVLine(cx, cy - 20, 9, met);
  tft.fillCircle(cx, cy - 21, 3, C_ACC);
}

// ------------------------------------------------------------ SCHERMATE EXTRA (comuni) ----
static void nsLut(uint16_t* lut, uint32_t bg, uint32_t fg) {          // 16 livelli di trasparenza per i font anti-aliased
  for (int a = 0; a < 16; a++) lut[a] = c565(mixRGB(bg, fg, (uint8_t)(a * 17)));
}
static void nsFrame(const char* title) {                               // titolo + grande riquadro
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  drawStr(title, 120, 3, 4, 1, C_ACC, C_BG);
  tft.fillRoundRect(6, 33, 228, 201, 14, C_CARD);
  tft.drawRoundRect(6, 33, 228, 201, 14, c565(mixRGB(th->card, th->a1, 100)));
}
static void nsDots(int n, int pos) {                                   // pallini "quale di n" sotto il riquadro
  if (n < 2) return;
  for (int i = 0; i < n; i++) tft.fillCircle(120 + (int)lroundf((i - (n - 1) / 2.0f) * 10), 237, 2, i == pos ? C_ACC : C_TRACK);
}
static bool nsTime(struct tm& ti, long& day) {                         // ora locale e numero di giorno
  const time_t now = time(nullptr);
  if (now < 1700000000) { memset(&ti, 0, sizeof ti); day = 0; return false; }
  localtime_r(&now, &ti);
  day = mnDays(ti);
  return true;
}
static float nsCov(float d) { return d <= -0.5f ? 0.0f : (d >= 0.5f ? 1.0f : d + 0.5f); }

// ------------------------------------------------------------ FRATTALI ------
// Disegno progressivo a blocchi sempre piu' fini; i frattali "morph" restano animati.
static fractal::Renderer FR;
static uint8_t frIdx = 255;
static bool frCap = false;
static uint16_t frColF(uint8_t r, uint8_t g, uint8_t b) { return RGB565(r, g, b); }
static void frFillF(int x, int y, int w, int h, uint16_t c) { tft.fillRect(x, y, w, h, c); }

void fractalStep(int d) {
  if (frIdx == 255) frIdx = 0;
  frIdx = (uint8_t)((frIdx + d + fractal::PRESET_COUNT) % fractal::PRESET_COUNT);
}

void fractalFull() {
  if (frIdx == 255) {                              // prima volta: parte da quello del giorno
    struct tm ti;
    long day;
    nsTime(ti, day);
    frIdx = (uint8_t)(day % fractal::PRESET_COUNT);
  }
  tft.fillScreen(0);
  tft.setSwapBytes(true);
  FR.begin(frIdx, frColF, frFillF, mnRow);
  frCap = false;
}

static void fractalCaption() {                     // nome del frattale in basso a sinistra, a disegno completo
  const char* nm = FR.pr->name;
  const int w = tft.textWidth(nm, 2) + 14;
  tft.fillRoundRect(8, 212, w, 20, 8, 0);
  tft.drawRoundRect(8, 212, w, 20, 8, RGB565(90, 90, 110));
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(RGB565(230, 230, 240), 0);
  tft.drawString(nm, 15, 215, 2);
}

void fractalTick() {
  if (FR.done) return;
  if (FR.step_(20) && !FR.pr->morph && !frCap) {
    frCap = true;
    fractalCaption();
  }
}

// icona del menu: un piccolo Mandelbrot calcolato al momento
void iconFract(int cx, int cy) {
  const int cw = 2, nx = 56, ny = 48;
  for (int j = 0; j < ny; j++) {
    for (int i = 0; i < nx; i++) {
      const int px = cx - 56 + i * cw + 1, py = cy - 48 + j * cw + 1;
      const int dx = px - cx, dy = py - cy;
      if (dx * dx + dy * dy > 54 * 54) continue;       // resta dentro il cerchio del menu, senza coprirlo
      const float x0 = -2.1f + 2.8f * i / nx, y0 = -1.2f + 2.4f * j / ny;
      float x = 0, y = 0;
      int k = 0;
      while (k < 24 && x * x + y * y < 4.0f) {
        const float t = x * x - y * y + x0;
        y = 2 * x * y + y0;
        x = t;
        k++;
      }
      if (k >= 24) continue;
      const uint32_t c = k < 12 ? mixRGB(0x2A1B7A, 0x2D8CFF, (uint8_t)(k * 21)) : mixRGB(0x2D8CFF, 0xFFD54A, (uint8_t)((k - 12) * 21));
      tft.fillRect(cx - 56 + i * cw, cy - 48 + j * cw, cw, cw, c565(c));
    }
  }
}

// ------------------------------------------------------------ COUNTDOWN -----
static uint8_t cdCur = 0;

// giorni mancanti all'evento (0 = oggi); -1 se vuoto o passato
static long cdDaysLeft(const CdEvent& e, const struct tm& ti, long* target = nullptr, struct tm* when = nullptr) {
  if (!e.m) return -1;
  struct tm t = ti;
  t.tm_mon = e.m - 1;
  t.tm_mday = e.d;
  t.tm_year = (e.yearly ? ti.tm_year + 1900 : e.y) - 1900;
  const long today = mnDays(ti);
  long tg = mnDays(t);
  if (e.yearly && tg < today) {
    t.tm_year++;
    tg = mnDays(t);
  }
  if (tg < today) return -1;
  if (target) *target = tg;
  if (when) *when = t;
  return tg - today;
}

static int cdValid(const struct tm& ti, uint8_t* idx) {      // elenco degli eventi validi
  int n = 0;
  for (uint8_t i = 0; i < 3; i++)
    if (cdDaysLeft(cfg.cd[i], ti) >= 0) idx[n++] = i;
  return n;
}

void countdownStep(int d) {
  struct tm ti;
  long day;
  if (!nsTime(ti, day)) return;
  uint8_t v[3];
  const int n = cdValid(ti, v);
  if (n < 2) return;
  int pos = 0;
  for (int k = 0; k < n; k++)
    if (v[k] == cdCur) pos = k;
  cdCur = v[(pos + d + n) % n];
}

// anello che si riempie in senso orario dall'alto (colori a1 -> a2), pallino luminoso in testa
static void cdRing(int cx, int cy, int rIn, int rOut, float fill, uint32_t cA, uint32_t cB, bool gold) {
  const uint32_t cardRGB = th->card;
  const uint16_t cbg = c565(cardRGB);
  const uint32_t track = mixRGB(cardRGB, th->dim, 70);
  const float fillAng = fill * 6.2831853f;
  const float rm = 0.5f * (rIn + rOut);
  const float tx = cx + rm * sinf(fillAng), ty = cy - rm * cosf(fillAng);
  const int x0 = cx - rOut - 4, w = 2 * rOut + 9;
  static uint16_t line[160];
  tft.setSwapBytes(true);
  for (int y = cy - rOut - 4; y <= cy + rOut + 4; y++) {
    const int dy = y - cy;
    for (int i = 0; i < w; i++) {
      const int dx = x0 + i - cx;
      uint32_t c = cardRGB;
      bool plain = true;
      const float r2 = (float)(dx * dx + dy * dy);
      if (r2 <= (float)((rOut + 1) * (rOut + 1)) && r2 >= (float)((rIn - 1) * (rIn - 1))) {
        const float r = sqrtf(r2);
        float cov = nsCov(rOut - r);
        const float c2 = nsCov(r - rIn);
        if (c2 < cov) cov = c2;
        if (cov > 0) {
          float ph = atan2f((float)dx, (float)-dy);
          if (ph < 0) ph += 6.2831853f;
          uint32_t rc = track;
          if (ph <= fillAng) {
            const uint8_t t = (uint8_t)(fillAng > 0 ? ph / fillAng * 255.0f : 0);
            rc = gold ? mixRGB(0xFFB000, 0xFFE066, t) : mixRGB(cA, cB, t);
          }
          c = mixRGB(cardRGB, rc, (uint8_t)(cov * 255));
          plain = false;
        }
      }
      if (fill > 0.0f) {
        const float ex = (float)(x0 + i) - tx, ey = (float)y - ty;
        const float d2 = ex * ex + ey * ey;
        if (d2 < 11.0f * 11.0f) {
          const float d = sqrtf(d2);
          c = mixRGB(c, gold ? 0xFFE066 : cB, (uint8_t)(nsCov(11.0f - d) * 255) / 4);
          c = mixRGB(c, 0xFFFFFF, (uint8_t)(nsCov(5.5f - d) * 255));
          plain = false;
        }
      }
      line[i] = plain ? cbg : c565(c);
    }
    tft.pushImage(x0, y, w, 1, line);
  }
}

void countdownFull() {
  nsFrame("COUNTDOWN");
  struct tm ti;
  long day;
  tft.setTextDatum(MC_DATUM);
  if (!nsTime(ti, day)) {
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString("In attesa dell'ora", 120, 120, 4);
    tft.setTextDatum(TL_DATUM);
    return;
  }
  uint8_t v[3];
  const int n = cdValid(ti, v);
  if (n == 0) {
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString("Nessun evento", 120, 110, 4);
    tft.setTextColor(C_DIM, C_CARD);
    tft.drawString("Aggiungilo dal web:", 120, 148, 2);
    tft.drawString("sezione Countdown", 120, 166, 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }
  int pos = -1;
  for (int k = 0; k < n; k++)
    if (v[k] == cdCur) pos = k;
  if (pos < 0) { pos = 0; cdCur = v[0]; }
  const CdEvent& e = cfg.cd[cdCur];
  long target = 0;
  struct tm when = ti;
  const long days = cdDaysLeft(e, ti, &target, &when);
  const bool today = (days == 0);
  float fill = 1.0f - (float)days / 365.0f;
  if (fill < 0.03f) fill = 0.03f;
  if (today) fill = 1.0f;
  const int cx = 120, cy = 104, rIn = 52, rOut = 66;
  cdRing(cx, cy, rIn, rOut, fill, th->a1, th->a2, today);
  tft.setTextDatum(MC_DATUM);
  char b[24];
  if (today) {
    tft.setTextColor(c565(0xFFD54A), C_CARD);
    tft.drawString("OGGI!", cx, cy, 4);
    static const uint32_t CF[6] = {0xE8384F, 0x2D8CFF, 0x1FB65A, 0x9B5DE5, 0xFF8A1F, 0xFFC83D};
    uint32_t seed = (uint32_t)target * 2654435761u + 12345u;
    for (int p = 0; p < 34; p++) {                          // coriandoli
      seed = seed * 1664525u + 1013904223u;
      const int px = 14 + (int)((seed >> 8) % 212);
      seed = seed * 1664525u + 1013904223u;
      const int py = 40 + (int)((seed >> 8) % 134);
      const int dx = px - cx, dy = py - cy;
      if (dx * dx + dy * dy < 72 * 72) continue;
      const uint16_t cc = c565(CF[p % 6]);
      if (p & 1) tft.fillRect(px, py, 4, 4, cc);
      else tft.fillCircle(px, py, 2, cc);
    }
  } else {
    snprintf(b, sizeof b, "%ld", days);
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(b, cx, cy - 8, days >= 1000 ? 4 : 6);
    tft.setTextColor(C_DIM, C_CARD);
    tft.drawString(days == 1 ? "GIORNO" : "GIORNI", cx, cy + 28, 2);
  }
  tft.setTextColor(C_TEXT, C_CARD);
  const int nf = (tft.textWidth(e.name, 4) <= 212) ? 4 : 2;
  tft.drawString(e.name[0] ? e.name : "EVENTO", 120, 188, nf);
  char ds[32];
  snprintf(ds, sizeof ds, "%s %d %s %d", GIORNI[(target + 4) % 7], when.tm_mday, MESI[when.tm_mon], when.tm_year + 1900);
  tft.setTextColor(C_ACC, C_CARD);
  tft.drawString(ds, 120, 211, 2);
  tft.setTextDatum(TL_DATUM);
  nsDots(n, pos);
}

// icona del menu: anello che si riempie con un numero al centro
void iconCount(int cx, int cy) {
  const uint16_t tr = c565(mixRGB(th->bg, th->dim, 90));
  for (int a = 0; a < 360; a += 3) {
    const float r = a * 0.0174533f;
    const int x = cx + (int)lroundf(42.0f * sinf(r)), y = cy - (int)lroundf(42.0f * cosf(r));
    const uint16_t c = a <= 250 ? c565(mixRGB(th->a1, th->a2, (uint8_t)(a * 255 / 250))) : tr;
    tft.fillCircle(x, y, 5, c);
  }
  const float r = 250 * 0.0174533f;
  tft.fillCircle(cx + (int)lroundf(42.0f * sinf(r)), cy - (int)lroundf(42.0f * cosf(r)), 8, 0xFFFF);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("24", cx, cy - 2, 6);
  tft.setTextDatum(TL_DATUM);
}

// ------------------------------------------------------------ CITAZIONE -----
static int qtOff = 0;                    // 0 = citazione di oggi; tocco = avanti/indietro nell'elenco

void quoteStep(int d) { qtOff += d; }

void quoteFull() {
  nsFrame("CITAZIONE");
  struct tm ti;
  long day;
  nsTime(ti, day);
  tft.setSwapBytes(true);
  uint16_t lutM[16], lutT[16], lutA[16];
  nsLut(lutM, th->card, th->a2);
  nsLut(lutT, th->card, th->text);
  nsLut(lutA, th->card, th->a1);
  QGlyph g;
  const int mw = qr::glyph(QF_MARK, 0x201C, g) ? g.w : 30;
  qr::drawLine(QF_MARK, "\xE2\x80\x9C", 3, 16, 38, 208, lutM, mnRow, 0, (208 - mw) / 2);
  long i = (day + qtOff) % QUOTE_COUNT;
  if (i < 0) i += QUOTE_COUNT;
  char qt[200], qa[48];
  strncpy_P(qt, QUOTES[i].text, sizeof(qt) - 1);
  qt[sizeof(qt) - 1] = 0;
  strncpy_P(qa, QUOTES[i].author, sizeof(qa) - 1);
  qa[sizeof(qa) - 1] = 0;
  tft.drawFastHLine(100, 203, 40, c565(mixRGB(th->card, th->a2, 170)));
  qr::drawQuoteBody(qt, qa, 16, 70, 208, 158, lutT, lutA, mnRow);
}

// icona del menu: grandi virgolette
void iconQuote(int cx, int cy) {
  const uint16_t c1 = c565(th->a1), c2 = c565(th->a2);
  for (int k = 0; k < 2; k++) {
    const int x = cx - 24 + k * 44;
    const uint16_t c = k ? c2 : c1;
    tft.fillCircle(x, cy - 8, 17, c);
    tft.fillTriangle(x - 17, cy - 8, x + 17, cy - 8, x - 20, cy + 34, c);
    tft.fillCircle(x + 3, cy - 12, 5, C_BG);
  }
}

// ------------------------------------------------------------ NUMERI --------
static const uint32_t BALL_COL[5] = {0xE8384F, 0x2D8CFF, 0x1FB65A, 0x9B5DE5, 0xFF8A1F};
static const uint32_t JOLLY_COL = 0xFFC83D;
static int lkOff = 0;                    // 0 = oggi; tocco = giorni dopo/prima

void luckyStep(int d) { lkOff += d; }

static void drawClover(int cx, int cy) {
  const uint16_t g1 = c565(0x2ECC71), g2 = c565(0x1E9E52), gs = c565(0x178A44);
  tft.drawLine(cx + 1, cy + 3, cx + 5, cy + 9, gs);
  tft.drawLine(cx + 2, cy + 3, cx + 6, cy + 9, gs);
  const int o = 4;
  tft.fillCircle(cx - o + 1, cy - o + 1, 4, g1);
  tft.fillCircle(cx + o - 1, cy - o + 1, 4, g1);
  tft.fillCircle(cx - o + 1, cy + o - 1, 4, g1);
  tft.fillCircle(cx + o - 1, cy + o - 1, 4, g1);
  tft.fillCircle(cx, cy, 2, g2);
}

void luckyFull() {
  nsFrame("NUMERI");
  tft.setTextDatum(TL_DATUM);
  drawClover(120 + tft.textWidth("NUMERI", 4) / 2 + 14, 13);
  struct tm ti;
  long day;
  nsTime(ti, day);
  tft.setSwapBytes(true);
  // etichetta del giorno a sinistra del titolo
  char lb[16];
  if (lkOff == 0) snprintf(lb, sizeof lb, "oggi");
  else if (lkOff == 1) snprintf(lb, sizeof lb, "domani");
  else if (lkOff == -1) snprintf(lb, sizeof lb, "ieri");
  else snprintf(lb, sizeof lb, "%+d gg", lkOff);
  drawStr(lb, 6, 8, 2, 0, lkOff == 0 ? C_DIM : C_ACC, C_BG);

  uint8_t n[6];
  lucky::numbers(day + lkOff, n);
  static const uint8_t PX[5] = {58, 120, 182, 89, 151};
  static const uint8_t PY[5] = {66, 66, 66, 130, 130};
  tft.setTextDatum(MC_DATUM);
  char b[4];
  for (int i = 0; i < 5; i++) {
    lucky::drawBall(PX[i], PY[i], 27, BALL_COL[i], th->card, false, mnCol, mnRow);
    snprintf(b, sizeof b, "%d", n[i]);
    tft.setTextColor(c565(mixRGB(BALL_COL[i], 0x000000, 150)));
    tft.drawString(b, PX[i], PY[i] + 1, 4);
  }
  const uint16_t dc = c565(mixRGB(th->card, th->a1, 90));
  for (int x = 20; x < 220; x += 8) tft.drawFastHLine(x, 166, 4, dc);
  lucky::drawBall(178, 201, 24, JOLLY_COL, th->card, true, mnCol, mnRow);
  snprintf(b, sizeof b, "%d", n[5]);
  tft.setTextColor(c565(0x6B4A00));
  tft.drawString(b, 178, 202, 4);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(c565(mixRGB(th->dim, th->text, 120)), C_CARD);
  tft.drawString("NUMERO", 20, 183, 2);
  tft.setTextColor(c565(JOLLY_COL), C_CARD);
  tft.drawString("JOLLY", 20, 201, 4);
}

// riga di pixel che salta lo sfondo: le palline restano "trasparenti" e non coprono cerchio e palline vicine
static void icRowSkipBg(int x, int y, int w, const uint16_t* px) {
  const uint16_t bg = C_BG;
  int i = 0;
  while (i < w) {
    while (i < w && px[i] == bg) i++;
    const int a = i;
    while (i < w && px[i] != bg) i++;
    if (i > a) tft.pushImage(x + a, y, i - a, 1, (uint16_t*)(px + a));
  }
}

// icona del menu: tre palline numerate
void iconLucky(int cx, int cy) {
  tft.setSwapBytes(true);
  static const int8_t PX[3] = {-27, 27, 0}, PY[3] = {-20, -20, 22};
  static const uint8_t NUM[3] = {7, 21, 13};
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 3; i++) {
    lucky::drawBall(cx + PX[i], cy + PY[i], 24, BALL_COL[i], th->bg, false, mnCol, icRowSkipBg);
    char b[4];
    snprintf(b, sizeof b, "%d", NUM[i]);
    tft.setTextColor(c565(mixRGB(BALL_COL[i], 0x000000, 150)));
    tft.drawString(b, cx + PX[i], cy + PY[i] + 1, 4);
  }
  tft.setTextDatum(TL_DATUM);
}

// ------------------------------------------------------------ POST-IT -------
enum EmoKind : uint8_t { EM_NONE, EM_SMILE, EM_WINK, EM_GRIN, EM_SAD, EM_TONGUE, EM_SURPRISE, EM_HEART, EM_COOL, EM_CRY, EM_HAPPY };

struct EmoCode {
  const char* code;
  uint8_t kind;
};
static const EmoCode EMO_CODES[] = {
    {":)", EM_SMILE},  {":-)", EM_SMILE}, {";)", EM_WINK},     {";-)", EM_WINK},     {":D", EM_GRIN},     {":-D", EM_GRIN},
    {":(", EM_SAD},    {":-(", EM_SAD},   {":P", EM_TONGUE},   {":p", EM_TONGUE},    {":-P", EM_TONGUE},  {":-p", EM_TONGUE},
    {":O", EM_SURPRISE}, {":o", EM_SURPRISE}, {":-O", EM_SURPRISE}, {":-o", EM_SURPRISE}, {"<3", EM_HEART}, {"8)", EM_COOL},
    {"8-)", EM_COOL},  {"B)", EM_COOL},   {"B-)", EM_COOL},    {":'(", EM_CRY},      {"^_^", EM_HAPPY},
};

static inline bool isAlnumC(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// riconosce un'emoticon alla posizione i del testo; ritorna il tipo (0 = nessuna) e la lunghezza
static uint8_t emoMatch(const char* s, size_t i, size_t& len) {
  uint8_t best = 0;
  len = 0;
  for (size_t k = 0; k < sizeof(EMO_CODES) / sizeof(EMO_CODES[0]); k++) {
    const char* c = EMO_CODES[k].code;
    const size_t n = strlen(c);
    if (n <= len || strncmp(s + i, c, n) != 0) continue;
    if (isAlnumC(c[0]) && i > 0 && isAlnumC(s[i - 1])) continue;       // "8)" e "B)" solo se isolati
    if (isAlnumC(c[n - 1]) && isAlnumC(s[i + n])) continue;           // ":D" in ":Domani" non e' un sorriso
    best = EMO_CODES[k].kind;
    len = n;
  }
  return best;
}

struct NoteTok {
  uint8_t emo;  // 0 = carattere normale
  char ch;
  uint8_t w;    // larghezza in pixel
};
struct NoteLine {
  uint8_t a, b;  // token [a, b)
  uint16_t w;
};

static const uint8_t NT_MAXTOK = 44;

static void noteEyes(int cx, int cy, int r, int sep, int er, uint16_t c) {
  tft.fillCircle(cx - sep, cy - r / 4, er, c);
  tft.fillCircle(cx + sep, cy - r / 4, er, c);
}

// arco della bocca: curve > 0 sorriso, curve < 0 smorfia triste
static void noteMouth(int cx, int cy, int w, int curve, int thick, uint16_t c) {
  for (int dx = -w; dx <= w; dx++) {
    const float u = (float)dx / (float)w;
    const int y = cy + (int)lroundf((float)curve * (1.0f - u * u));
    tft.fillRect(cx + dx, y - (curve < 0 ? 0 : 0), thick, thick, c);
  }
}

static void drawEmoticon(int cx, int cy, int r, uint8_t kind) {
  const uint16_t ink = c565(0x4A2F00), face = c565(0xFFC83D), white = c565(0xFFFFFF);
  const int thk = r >= 9 ? 2 : 1;
  if (kind == EM_HEART) {
    const uint16_t red = c565(0xE5384F);
    const int lr = r * 5 / 9;
    tft.fillCircle(cx - lr + 1, cy - r / 4, lr, red);
    tft.fillCircle(cx + lr - 1, cy - r / 4, lr, red);
    tft.fillTriangle(cx - r + 1, cy - r / 8, cx + r - 1, cy - r / 8, cx, cy + r, red);
    tft.fillCircle(cx - lr, cy - r / 3, r / 6 + 1, c565(0xFF9AA8));
    return;
  }
  tft.fillCircle(cx, cy, r, ink);
  tft.fillCircle(cx, cy, r - 1, face);
  const int sep = r / 3 + 1, er = r >= 9 ? 2 : 1;
  const int my = cy + r / 3;
  switch (kind) {
    case EM_SMILE:
      noteEyes(cx, cy, r, sep, er, ink);
      noteMouth(cx, my - 1, r / 2 + 1, r / 4 + 1, thk, ink);
      break;
    case EM_WINK:
      tft.fillCircle(cx - sep, cy - r / 4, er, ink);
      tft.drawFastHLine(cx + sep - er - 1, cy - r / 4, 2 * er + 3, ink);
      if (thk > 1) tft.drawFastHLine(cx + sep - er - 1, cy - r / 4 + 1, 2 * er + 3, ink);
      noteMouth(cx, my - 1, r / 2 + 1, r / 4 + 1, thk, ink);
      break;
    case EM_GRIN: {
      noteEyes(cx, cy, r, sep, er, ink);
      const int w = r / 2 + 2, h = r / 2 + 2;
      for (int y = 0; y <= h; y++) {
        const int hw = (int)lroundf((float)w * sqrtf(1.0f - (float)(y * y) / (float)(h * h + 1)));
        tft.drawFastHLine(cx - hw, my - 2 + y, 2 * hw + 1, y < 2 ? white : ink);
      }
      break;
    }
    case EM_SAD:
      noteEyes(cx, cy, r, sep, er, ink);
      noteMouth(cx, my + r / 4 + 1, r / 2, -(r / 4 + 1), thk, ink);
      // per la smorfia triste le estremita' stanno piu' in basso del centro
      break;
    case EM_TONGUE:
      noteEyes(cx, cy, r, sep, er, ink);
      tft.fillRect(cx - r / 2, my, r + 1, thk, ink);
      tft.fillRoundRect(cx, my, r / 2 + 2, r / 2 + 2, 2, c565(0xFF6F91));
      tft.drawFastVLine(cx + r / 4, my + 1, r / 3, ink);
      break;
    case EM_SURPRISE:
      tft.fillCircle(cx - sep, cy - r / 4, er + 1, white);
      tft.fillCircle(cx + sep, cy - r / 4, er + 1, white);
      tft.fillCircle(cx - sep, cy - r / 4, er > 1 ? 1 : 0, ink);
      tft.fillCircle(cx + sep, cy - r / 4, er > 1 ? 1 : 0, ink);
      tft.fillCircle(cx, my + 1, r / 4 + 1, ink);
      break;
    case EM_COOL:
      tft.fillRoundRect(cx - r + 2, cy - r / 3, r - 2, r / 2 + 1, 2, ink);
      tft.fillRoundRect(cx + 1, cy - r / 3, r - 2, r / 2 + 1, 2, ink);
      tft.drawFastHLine(cx - 2, cy - r / 3 + 1, 4, ink);
      tft.drawFastHLine(cx - r + 3, cy - r / 3 + 1, 2, white);
      noteMouth(cx, my, r / 2 + 1, r / 4 + 1, thk, ink);
      break;
    case EM_CRY:
      noteEyes(cx, cy, r, sep, er, ink);
      noteMouth(cx, my + r / 4 + 1, r / 2, -(r / 4 + 1), thk, ink);
      tft.drawFastVLine(cx - sep - 1, cy - r / 4 + er + 1, r / 2, c565(0x2D8CFF));
      tft.drawFastVLine(cx + sep + 1, cy - r / 4 + er + 1, r / 2, c565(0x2D8CFF));
      break;
    case EM_HAPPY: {
      for (int k = -1; k <= 1; k += 2) {
        const int ex = cx + k * sep;
        tft.drawLine(ex - er - 1, cy - r / 4 + er, ex, cy - r / 4 - er, ink);
        tft.drawLine(ex, cy - r / 4 - er, ex + er + 1, cy - r / 4 + er, ink);
      }
      noteMouth(cx, my - 1, r / 2 + 1, r / 4 + 1, thk, ink);
      break;
    }
    default: break;
  }
}

// divide il testo in token (caratteri ed emoticon); ritorna quanti
static int noteTokenize(const char* s, NoteTok* t, int font, int emoW) {
  int n = 0;
  char b[2] = {0, 0};
  for (size_t i = 0; s[i] && n < NT_MAXTOK;) {
    size_t len = 0;
    const uint8_t k = emoMatch(s, i, len);
    if (k) {
      t[n].emo = k;
      t[n].ch = 0;
      t[n].w = (uint8_t)emoW;
      n++;
      i += len;
    } else {
      b[0] = s[i];
      t[n].emo = 0;
      t[n].ch = s[i];
      t[n].w = (uint8_t)tft.textWidth(b, font);
      n++;
      i++;
    }
  }
  return n;
}

// va a capo (alle parole, o dopo un'emoticon; se una parola e' troppo lunga la spezza); ritorna il numero di righe o -1
static int noteLayout(const NoteTok* t, int n, int maxW, NoteLine* L, int maxL) {
  int nl = 0, a = 0, cw = 0, brk = -1;
  for (int i = 0; i < n; i++) {
    if (t[i].ch == ' ' && cw == 0 && !t[i].emo) {  // niente spazi a inizio riga
      a = i + 1;
      continue;
    }
    if (cw + t[i].w > maxW && i > a) {
      const int b = (brk > a) ? brk : i;
      if (nl >= maxL) return -1;
      L[nl].a = (uint8_t)a;
      L[nl].b = (uint8_t)b;
      nl++;
      a = b;
      while (a < i && t[a].ch == ' ' && !t[a].emo) a++;
      cw = 0;
      for (int j = a; j < i; j++) cw += t[j].w;
      brk = -1;
    }
    cw += t[i].w;
    if ((t[i].ch == ' ' && !t[i].emo) || t[i].emo) brk = i + 1;
  }
  if (a < n) {
    if (nl >= maxL) return -1;
    L[nl].a = (uint8_t)a;
    L[nl].b = (uint8_t)n;
    nl++;
  }
  for (int k = 0; k < nl; k++) {  // larghezza senza gli spazi finali
    int b = L[k].b, w = 0;
    while (b > L[k].a && t[b - 1].ch == ' ' && !t[b - 1].emo) b--;
    L[k].b = (uint8_t)b;
    for (int j = L[k].a; j < b; j++) w += t[j].w;
    L[k].w = (uint16_t)w;
  }
  return nl;
}


static uint8_t ntCur = 0;

static int ntValid(uint8_t* idx) {
  int n = 0;
  for (uint8_t i = 0; i < 3; i++)
    if (cfg.nt[i].on && cfg.nt[i].text[0]) idx[n++] = i;
  return n;
}

void noteStep(int d) {
  uint8_t v[3];
  const int n = ntValid(v);
  if (n < 2) return;
  int pos = 0;
  for (int k = 0; k < n; k++)
    if (v[k] == ntCur) pos = k;
  ntCur = v[(pos + d + n) % n];
}

void noteFull() {
  const uint16_t bg = C_BG;
  tft.fillScreen(bg);
  uint8_t v[3];
  const int na = ntValid(v);
  int pos = -1;
  for (int k = 0; k < na; k++)
    if (v[k] == ntCur) pos = k;
  if (na > 0 && pos < 0) { pos = 0; ntCur = v[0]; }
  const int idx = na > 0 ? ntCur : 0;
  static const uint32_t PAPER[3] = {0xFFE45E, 0xFFA8C8, 0xA9EFA4};
  const uint32_t pr = PAPER[idx];
  const uint16_t paper = c565(pr), ink = c565(0x23201A);

  // il foglio con l'angolo in basso a destra piegato
  tft.fillRoundRect(10, 12, 220, 214, 6, paper);
  tft.fillRect(10, 12, 220, 22, c565(mixRGB(pr, 0xFFFFFF, 70)));
  tft.drawFastHLine(10, 34, 220, c565(mixRGB(pr, 0x000000, 40)));
  tft.fillTriangle(230, 196, 230, 226, 200, 226, bg);
  tft.fillTriangle(230, 196, 200, 226, 200, 196, c565(mixRGB(pr, 0x000000, 90)));
  tft.fillCircle(120, 23, 7, c565(0x7A1222));                // puntina
  tft.fillCircle(119, 22, 6, c565(0xE5384F));
  tft.fillCircle(117, 20, 2, c565(0xFFB0BA));

  const char* txt = na > 0 ? cfg.nt[idx].text : "Scrivi una nota dal web :)";
  NoteTok tok[NT_MAXTOK];
  NoteLine ln[8];
  int font = 4, lh = 30, emoR = 11, nl, n;
  n = noteTokenize(txt, tok, 4, 25);
  nl = noteLayout(tok, n, 190, ln, 5);
  if (nl < 0) {
    font = 2;
    lh = 20;
    emoR = 7;
    n = noteTokenize(txt, tok, 2, 17);
    nl = noteLayout(tok, n, 192, ln, 8);
    if (nl < 0) nl = 8;
  }
  const int top = 40 + (150 - nl * lh) / 2;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ink, paper);
  char b[2] = {0, 0};
  for (int k = 0; k < nl; k++) {
    int x = 120 - ln[k].w / 2;
    const int y = top + k * lh;
    for (int j = ln[k].a; j < ln[k].b; j++) {
      if (tok[j].emo) {
        drawEmoticon(x + tok[j].w / 2, y + lh / 2 - 1, emoR, tok[j].emo);
      } else {
        b[0] = tok[j].ch;
        tft.drawString(b, x, y + (lh - (font == 4 ? 26 : 16)) / 2, font);
      }
      x += tok[j].w;
    }
  }
  if (na > 1)
    for (int i = 0; i < na; i++) tft.fillCircle(120 - (na - 1) * 7 + i * 14, 233, 3, i == pos ? C_TEXT : c565(mixRGB(th->bg, th->dim, 120)));
}

// icona del menu: foglietto giallo con puntina e sorriso
void iconNote(int cx, int cy) {
  const uint16_t paper = c565(0xFFE45E);
  tft.fillRoundRect(cx - 44, cy - 44, 88, 88, 6, paper);
  tft.fillRect(cx - 44, cy - 44, 88, 16, c565(mixRGB(0xFFE45E, 0xFFFFFF, 70)));
  tft.fillTriangle(cx + 44, cy + 20, cx + 44, cy + 44, cx + 20, cy + 44, C_BG);
  tft.fillTriangle(cx + 44, cy + 20, cx + 20, cy + 44, cx + 20, cy + 20, c565(mixRGB(0xFFE45E, 0x000000, 90)));
  tft.fillCircle(cx, cy - 36, 6, c565(0xE5384F));
  tft.fillCircle(cx - 2, cy - 38, 2, c565(0xFFB0BA));
  drawEmoticon(cx - 2, cy + 6, 22, EM_SMILE);
}

// ------------------------------------------------------------ BIBIGOTCHI -----
// Bibi, un barboncino nero pixel-art: cambia umore in base ai dati del firmware (meteo, aria, UV, pollini,
// notte, cripto, ora del giorno, feste). Col tocco avanti/indietro si scorrono i suoi stati attuali.
struct BibiEntry {
  bibi::State st;
  bibi::Sky sky;
  uint8_t variant;
  char cap[36];
  char sub[36];
};
static const int BB_MAX = 16;
static BibiEntry bbList[BB_MAX];
static int bbN = 0, bbIdx = 0;
static uint16_t bbFrame = 0;
static uint32_t bbNextFrame = 0;

static uint16_t bbCol(uint8_t r, uint8_t g, uint8_t b) { return RGB565(r, g, b); }
static void bbRow(int x, int y, int w, const uint16_t* px) { tft.pushImage(x, y, w, 1, (uint16_t*)px); }
static void bbRect(int x, int y, int w, int h, uint16_t c) { tft.fillRect(x, y, w, h, c); }

static void bbAdd(bibi::State st, bibi::Sky sky, uint8_t var, const char* cap, const char* sub) {
  if (bbN >= BB_MAX) return;
  BibiEntry& e = bbList[bbN++];
  e.st = st;
  e.sky = sky;
  e.variant = var;
  snprintf(e.cap, sizeof e.cap, "%s", cap);
  snprintf(e.sub, sizeof e.sub, "%s", sub);
}

// feste fisse (giorno, mese, nome)
static const char* bbHoliday(const struct tm& t) {
  static const struct { uint8_t d, m; const char* n; } H[] = {
    {1, 1, "Capodanno"}, {6, 1, "Epifania"}, {25, 4, "Liberazione"}, {1, 5, "Festa del lavoro"},
    {2, 6, "Festa della Repubblica"}, {15, 8, "Ferragosto"}, {1, 11, "Ognissanti"}, {8, 12, "Immacolata"},
    {25, 12, "Natale"}, {26, 12, "Santo Stefano"}, {31, 12, "San Silvestro"}};
  for (unsigned i = 0; i < sizeof(H) / sizeof(H[0]); i++)
    if (H[i].d == t.tm_mday && H[i].m == t.tm_mon + 1) return H[i].n;
  return nullptr;
}

// costruisce l'elenco degli stati attuali di Bibi, dal piu' importante al meno
static void bbBuild() {
  using namespace bibi;
  bbN = 0;
  const time_t now = time(nullptr);
  const bool tOk = now > 1700000000;
  struct tm ti;
  if (tOk) localtime_r(&now, &ti);
  else memset(&ti, 0, sizeof ti);
  const int hour = tOk ? ti.tm_hour : 12;
  const bool wxOk = W.valid;
  const int code = wxOk ? W.code : 0;
  const float temp = wxOk ? W.t : 20.0f;
  const bool night = nightActive() || (wxOk ? !W.day : (hour >= 22 || hour < 7));
  const bool storm = wxOk && code >= 95;
  const bool rainy = wxOk && ((code >= 51 && code <= 67) || (code >= 80 && code <= 82));
  const bool snowy = wxOk && ((code >= 71 && code <= 77) || code == 85 || code == 86);
  const bool aFresh = A.valid && (!A.upd || !tOk || (now - A.upd) < 3 * 3600);
  const bool badAir = aFresh && A.aqi >= 60;
  uint8_t polLv = 0;
  if (aFresh && A.hasPollen) {
    const uint8_t a = pollenLevel(A.pGrass, 0), b = pollenLevel(A.pOlive, 1), c = pollenLevel(A.pBirch, 2);
    polLv = a > b ? (a > c ? a : c) : (b > c ? b : c);
  }
  const bool strongUv = aFresh && A.uv >= 8.0f;

  // cripto: variazione piu' forte tra le monete caricate di recente (periodo del grafico)
  float bestChg = 0;
  const char* bestSym = "";
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < CR_COUNT; i++) {
    const CoinData& d = CD[i];
    if (!d.valid || d.n < 2 || d.o[0] <= 0 || (millis() - d.tryAt) > 3UL * 3600UL * 1000UL) continue;
    const float chg = (d.c[d.n - 1] - d.o[0]) / d.o[0] * 100.0f;
    if (fabsf(chg) > fabsf(bestChg)) { bestChg = chg; bestSym = COINS[i].sym; }
  }
  xSemaphoreGive(mtx);
  const char* holiday = tOk ? bbHoliday(ti) : nullptr;

  char cap[40], sub[40], wsub[40];
  wsub[0] = 0;
  if (wxOk) snprintf(wsub, sizeof wsub, "%d C - %s", (int)lroundf(temp), rainy ? "pioggia" : snowy ? "neve" : storm ? "temporale" : "meteo");

  if (storm) bbAdd(ST_STORM, SKY_RAIN, 0, "Che paura, un temporale!", wsub);
  if (night) bbAdd(ST_SLEEP, SKY_NIGHT, 0, "Zzz... Bibi dorme", "buonanotte");
  if (holiday) {
    snprintf(cap, sizeof cap, "Oggi: %.14s!", holiday);
    bbAdd(ST_PARTY, SKY_PARTY, 0, cap, "Bibi festeggia!");
  }
  if (bestChg <= -5.0f) {
    snprintf(cap, sizeof cap, "%s crolla: %.1f%%", bestSym, bestChg);
    bbAdd(ST_CRASH, SKY_CRASH, 0, cap, "le cripto in picchiata");
  } else if (bestChg >= 5.0f) {
    snprintf(cap, sizeof cap, "%s vola: +%.1f%%", bestSym, bestChg);
    bbAdd(ST_PUMP, SKY_PUMP, 0, cap, "le cripto salgono!");
  }
  if (badAir) {
    snprintf(sub, sizeof sub, "indice EAQI %d", (int)lroundf(A.aqi));
    bbAdd(ST_MASK, SKY_SMOG, 0, "Aria pessima: mascherina!", sub);
  }
  if (polLv >= 3) {
    const uint8_t lg = pollenLevel(A.pGrass, 0), lo = pollenLevel(A.pOlive, 1), lb = pollenLevel(A.pBirch, 2);
    const char* who = (lg >= lo && lg >= lb) ? "graminacee" : (lo >= lb ? "olivo" : "betulla");
    snprintf(sub, sizeof sub, "%s: %s", who, polLv >= 4 ? "molto alti" : "alti");
    bbAdd(ST_SNEEZE, SKY_DAY, 0, "Etciu'! Che pollini!", sub);
  }
  if (rainy) bbAdd(ST_RAIN, SKY_RAIN, 0, "Piove: ecco l'ombrello!", wsub);
  if (snowy) bbAdd(ST_SNOW, SKY_SNOW, 0, "Nevica! Che freddo!", wsub);
  if (wxOk && temp >= 30.0f) bbAdd(ST_HOT, SKY_DAY, 0, "Che caldo! Un ghiacciolo?", wsub);
  if (wxOk && temp <= 3.0f) bbAdd(ST_COLD, SKY_CLOUD, 0, "Brrr, che freddo!", wsub);
  if (strongUv) {
    snprintf(sub, sizeof sub, "indice UV %.0f", A.uv);
    bbAdd(ST_UV, SKY_DAY, 0, "UV alto: crema e cappello!", sub);
  }
  const Sky calm = night ? SKY_NIGHT : ((wxOk && (code == 3 || code == 45 || code == 48)) ? SKY_CLOUD : SKY_DAY);
  if (!night && hour >= 7 && hour < 10) bbAdd(ST_COFFEE, calm, 0, "Buongiorno! Un caffe'?", wsub);
  if (!night && ((hour == 12 || hour == 13) || (hour >= 19 && hour < 21))) bbAdd(ST_FOOD, calm, 0, "E' ora della pappa!", wsub);
  // gli umori di tutti i giorni
  static const char* const HC[4] = {"Bau! Tutto bene!", "Ciao, amico!", "Balliamo con Bibi!", "Ti voglio bene!"};
  for (uint8_t v = 0; v < 4; v++) bbAdd(ST_HAPPY, calm, v, HC[v], wsub);
  if (bbIdx >= bbN) bbIdx = 0;
}

static void bbDrawFrame() {
  const BibiEntry& e = bbList[bbIdx];
  const bibi::Scene sc = {e.st, e.sky, e.variant};
  tft.setSwapBytes(true);
  bibi::drawFrame(sc, bbFrame, th->bg, bbCol, bbRow, 8, 35);
}

void bibiStep(int d) {
  bbBuild();
  if (bbN > 0) bbIdx = (bbIdx + d + bbN) % bbN;
}

void bibiFull() {
  bbBuild();
  tft.fillScreen(C_BG);
  drawStr("BIBIGOTCHI", 120, 3, 4, 1, C_ACC, C_BG);
  char b[12];
  snprintf(b, sizeof b, "%d/%d", bbIdx + 1, bbN);
  drawStr(b, 236, 8, 2, 2, C_DIM, C_BG);
  bbFrame = 0;
  bbDrawFrame();
  const BibiEntry& e = bbList[bbIdx];
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  const int f = (tft.textWidth(e.cap, 4) <= 226) ? 4 : 2;
  tft.drawString(e.cap, 120, 212, f);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(e.sub, 120, 231, 2);
  tft.setTextDatum(TL_DATUM);
  bbNextFrame = millis() + 120;
}

void bibiTick() {
  const uint32_t ms = millis();
  if ((int32_t)(ms - bbNextFrame) < 0) return;
  bbNextFrame = ms + 120;
  bbFrame++;
  bbDrawFrame();
}

// icona del menu: il viso di Bibi
void iconBibi(int cx, int cy) {
  tft.setSwapBytes(true);
  bibi::portrait(th->bg, bbCol, bbRect, cx, cy);
}

// ------------------------------------------------------------ FOTO ----------
// Foto JPEG 240x240 salvate nella memoria interna (LittleFS) dall'interfaccia web e mostrate a giro.
static const int PH_MAX = 20;
static const size_t PH_MAX_BYTES = 40 * 1024;
static uint8_t phSlots[PH_MAX];
static int phN = 0, phPos = 0;
static uint32_t phRev = 1, phSeen = 0, phSwitchAt = 0;

static void phPath(char* out, size_t n, int slot) { snprintf(out, n, "/f%d.jpg", slot); }

void phScan() {
  phN = 0;
  if (fsOk) {
    char p[16];
    for (int i = 0; i < PH_MAX; i++) {
      phPath(p, sizeof p, i);
      if (LittleFS.exists(p)) phSlots[phN++] = (uint8_t)i;
    }
  }
  if (phPos >= phN) phPos = 0;
}

static int phRead(void* ctx, uint8_t* buf, int len) { return (int)((File*)ctx)->read(buf, (size_t)len); }
static void phOut(void*, int x, int y, int w, int h, uint16_t* px) {
  if (y >= 240) return;
  tft.pushImage(x, y, w, h, px);
}

// decodifica la foto dal file e la scrive sul display (stesso decoder dell'altro progetto: colori fedeli)
static bool phDraw(int slot) {
  char p[16];
  phPath(p, sizeof p, slot);
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  JpegDecoder* dec = new (std::nothrow) JpegDecoder(phRead, &f);
  if (!dec) { f.close(); return false; }
  bool ok = false;
  tft.setSwapBytes(true);
  if (dec->begin() == JpegDecoder::OK) {
    if (dec->width() < 240 || dec->height() < 240) tft.fillScreen(0);
    ok = (dec->decode(phOut, nullptr, false) == JpegDecoder::OK);
  }
  delete dec;
  f.close();
  return ok;
}

void phStep(int d) {
  if (phN > 0) phPos = (phPos + d + phN) % phN;
  phSwitchAt = millis();
}

void photoFull() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  phSeen = phRev;
  phSwitchAt = millis();
  if (!fsOk) {
    drawStr("Memoria foto", 120, 90, 4, 1, C_ERR, C_BG);
    drawStr("non disponibile", 120, 124, 4, 1, C_ERR, C_BG);
    return;
  }
  if (phN == 0) {
    drawStr("Nessuna foto", 120, 52, 4, 1, C_TEXT, C_BG);
    drawStr("Caricale dal browser", 120, 100, 2, 1, C_DIM, C_BG);
    drawStr("aprendo l'indirizzo", 120, 120, 2, 1, C_DIM, C_BG);
    drawStr(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("nmtv.local"), 120, 148, 4, 1, C_ACC, C_BG);
    drawStr("sezione Foto", 120, 190, 2, 1, C_DIM, C_BG);
    return;
  }
  for (int k = 0; k < phN; k++) {
    if (phDraw(phSlots[phPos])) return;
    phPos = (phPos + 1) % phN;
  }
  tft.fillScreen(C_BG);
  drawStr("Foto non leggibile", 120, 104, 4, 1, C_ERR, C_BG);
}

void photoTick() {
  if (phRev != phSeen) {            // foto aggiunta o eliminata dal web mentre e' aperta
    needFull = true;
    return;
  }
  if (phN > 1 && millis() - phSwitchAt >= (uint32_t)cfg.pSec * 1000UL) {
    phStep(1);
    needFull = true;
  }
}

// icona del menu: cornice con montagne e sole
void iconPhoto(int cx, int cy) {
  tft.fillRoundRect(cx - 52, cy - 40, 104, 80, 8, RGB565(40, 90, 160));
  tft.fillCircle(cx + 26, cy - 18, 9, RGB565(255, 210, 70));
  tft.fillTriangle(cx - 52, cy + 40, cx - 20, cy - 8, cx + 14, cy + 40, RGB565(30, 150, 100));
  tft.fillTriangle(cx - 6, cy + 40, cx + 24, cy + 6, cx + 52, cy + 40, RGB565(20, 105, 75));
  tft.drawRoundRect(cx - 52, cy - 40, 104, 80, 8, C_TEXT);
  tft.drawRoundRect(cx - 51, cy - 39, 102, 78, 7, C_TEXT);
}

// ------------------------------------------------------------ QUADRANTI ------
// Quattro orologi: 0 Semplice, 1 Analogico, 2 Flip, 3 Matrix.

// --- 0: Semplice (ora grande, data, icona meteo e temperatura) ---
static int clk1Day = -1;

static void clk1Weather() {
  tft.fillRect(0, 90, 240, 150, C_BG);
  if (!W.valid) {
    drawStr("In attesa dei dati", 120, 150, 4, 1, C_DIM, C_BG);
    return;
  }
  drawIcon(58, 162, iconFor(W.code), W.day, 1.1f, C_BG);
  const int tv = (int)lroundf(W.t);
  const bool neg = tv < 0;
  const String ts = String(abs(tv));
  const int tw = tft.textWidth(ts, 6);
  const int total = tw + (neg ? 20 : 0);
  int x0 = 122;
  if (x0 + total + 34 > 238) x0 = 238 - 34 - total;
  const int y0 = 138;
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(TL_DATUM);
  if (neg) tft.fillRect(x0, y0 + 34, 14, 7, C_TEXT);
  tft.drawString(ts, x0 + (neg ? 20 : 0), y0, 6);
  const int xe = x0 + total + 4;
  tft.drawCircle(xe + 5, y0 + 8, 4, C_TEXT);
  tft.drawCircle(xe + 5, y0 + 8, 3, C_TEXT);
  drawStr("C", xe + 14, y0 + 2, 4, 0, C_TEXT, C_BG);
}

void clk1Full() {
  tft.fillScreen(C_BG);
  struct tm t;
  const bool ok = getTm(t);
  lastSec = ok ? t.tm_sec : -1;
  clk1Day = ok ? t.tm_mday : -1;
  drawDate(ok ? &t : nullptr);
  drawClock(ok ? &t : nullptr);
  drawSecBar(ok ? &t : nullptr);
  clk1Weather();
}

void clk1Tick(bool dataChanged) {
  struct tm t;
  if (getTm(t) && t.tm_sec != lastSec) {
    if (lastSec == -1 || t.tm_mday != clk1Day) {
      drawDate(&t);
      clk1Day = t.tm_mday;
    }
    lastSec = t.tm_sec;
    drawClock(&t);
    drawSecBar(&t);
  }
  if (dataChanged) clk1Weather();
}

// --- 1: Analogico ---
static const int AX = 120, AY = 112, AR = 104;
static int anaSec = -1, anaMin = -1, anaHour = -1, anaDay = -1;
static float anaSA = 0, anaMA = 0, anaHA = 0;

static void anaHand(float deg, int len, int tail, int w, uint16_t col) {
  const float a = deg * 0.0174533f;
  const float dx = sinf(a), dy = -cosf(a);
  const int tx = AX + (int)lroundf(dx * len), ty = AY + (int)lroundf(dy * len);
  const int bx = (int)lroundf(-dy * w * 0.5f), by = (int)lroundf(dx * w * 0.5f);
  tft.fillTriangle(AX + bx, AY + by, AX - bx, AY - by, tx, ty, col);
  if (tail > 0) {
    const int hx = AX - (int)lroundf(dx * tail), hy = AY - (int)lroundf(dy * tail);
    tft.fillTriangle(AX + bx, AY + by, AX - bx, AY - by, hx, hy, col);
  }
}

static void anaDate(const struct tm* t) {
  tft.fillRect(0, 220, 240, 20, C_BG);
  if (!t) {
    drawStr("Sincronizzo l'ora...", 120, 220, 2, 1, C_DIM, C_BG);
    return;
  }
  char b[40];
  snprintf(b, sizeof b, "%s %d %s %d", GIORNI[t->tm_wday], t->tm_mday, MESI[t->tm_mon], t->tm_year + 1900);
  drawStr(b, 120, 220, 2, 1, C_ACC2, C_BG);
}

void anaTick() {
  struct tm t;
  if (!getTm(t)) return;
  if (t.tm_sec == anaSec) return;
  if (t.tm_mday != anaDay) {
    anaDate(&t);
    anaDay = t.tm_mday;
  }
  const bool minCh = (t.tm_min != anaMin || t.tm_hour != anaHour);
  if (anaSec >= 0) anaHand(anaSA, 84, 16, 2, C_CARD);        // cancella la lancetta dei secondi
  if (minCh && anaMin >= 0) {
    anaHand(anaHA, 52, 0, 7, C_CARD);
    anaHand(anaMA, 78, 0, 5, C_CARD);
  }
  anaHA = (float)((t.tm_hour % 12) * 30) + t.tm_min * 0.5f;
  anaMA = (float)t.tm_min * 6.0f;
  anaSA = (float)t.tm_sec * 6.0f;
  anaHand(anaHA, 52, 0, 7, C_TEXT);
  anaHand(anaMA, 78, 0, 5, C_ACC);
  anaHand(anaSA, 84, 16, 2, C_ACC2);
  tft.fillCircle(AX, AY, 6, C_ACC2);
  tft.fillCircle(AX, AY, 2, C_CARD);
  anaSec = t.tm_sec;
  anaMin = t.tm_min;
  anaHour = t.tm_hour;
}

void anaFull() {
  tft.fillScreen(C_BG);
  tft.fillCircle(AX, AY, AR, C_CARD);
  tft.drawCircle(AX, AY, AR, C_ACC);
  tft.drawCircle(AX, AY, AR - 1, C_ACC);
  for (int i = 0; i < 60; i++) {
    const float a = i * 6.0f * 0.0174533f;
    const float dx = sinf(a), dy = -cosf(a);
    if (i % 5 == 0) {
      const int x1 = AX + (int)lroundf(dx * 88), y1 = AY + (int)lroundf(dy * 88);
      const int x2 = AX + (int)lroundf(dx * 100), y2 = AY + (int)lroundf(dy * 100);
      tft.drawLine(x1, y1, x2, y2, C_TEXT);
      tft.drawLine(x1 + 1, y1, x2 + 1, y2, C_TEXT);
      tft.drawLine(x1, y1 + 1, x2, y2 + 1, C_TEXT);
    } else {
      tft.drawLine(AX + (int)lroundf(dx * 96), AY + (int)lroundf(dy * 96),
                   AX + (int)lroundf(dx * 100), AY + (int)lroundf(dy * 100), C_DIM);
    }
  }
  anaSec = anaMin = anaHour = anaDay = -1;
  struct tm t;
  if (!getTm(t)) anaDate(nullptr);
  anaTick();
}

// --- 2: Flip (cartellini che si girano) ---
static int8_t flipDig[6];
static bool flipNoTime = false;

static void flipRect(int i, int& x, int& y, int& w, int& h) {
  static const int16_t HX[4] = {12, 66, 124, 178};
  if (i < 4) { x = HX[i]; y = 44; w = 50; h = 84; }
  else       { x = (i == 4) ? 84 : 122; y = 148; w = 34; h = 56; }
}

// d: cifra 0..9, -2 = cartellino vuoto; blank = fotogramma intermedio del "giro"
static void flipDraw(int i, int d, bool blank) {
  int x, y, w, h;
  flipRect(i, x, y, w, h);
  const uint16_t shade = c565(mixRGB(th->card, th->bg, 150));
  const uint16_t edge = c565(mixRGB(th->card, th->a1, 110));
  tft.fillRoundRect(x, y, w, h, 8, blank ? shade : C_CARD);
  tft.drawRoundRect(x, y, w, h, 8, edge);
  if (!blank && d >= 0) {
    tft.setTextColor(C_TEXT, C_CARD);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(d), x + w / 2, y + h / 2 + 2, i < 4 ? 6 : 4);
    tft.setTextDatum(TL_DATUM);
  }
  tft.fillRect(x + 1, y + h / 2 - 1, w - 2, 2, C_BG);       // cerniera
  tft.fillRect(x, y + h / 2 - 3, 3, 6, C_BG);
  tft.fillRect(x + w - 3, y + h / 2 - 3, 3, 6, C_BG);
}

// cartellino disegnato dentro uno sprite e copiato in un buffer (per la animazione del giro)
static void flipCardSprite(TFT_eSprite& sp, int i, int d) {
  int x, y, w, h;
  flipRect(i, x, y, w, h);
  sp.fillSprite(C_BG);
  sp.fillRoundRect(0, 0, w, h, 8, C_CARD);
  sp.drawRoundRect(0, 0, w, h, 8, c565(mixRGB(th->card, th->a1, 110)));
  if (d >= 0) {
    sp.setTextColor(C_TEXT, C_CARD);
    sp.setTextDatum(MC_DATUM);
    sp.drawString(String(d), w / 2, h / 2 + 2, i < 4 ? 6 : 4);
  }
  sp.fillRect(1, h / 2 - 1, w - 2, 2, C_BG);
  sp.fillRect(0, h / 2 - 3, 3, 6, C_BG);
  sp.fillRect(w - 3, h / 2 - 3, 3, 6, C_BG);
}

// animazione "a pagina": la meta' alta vecchia si abbassa sulla cerniera, poi la meta' bassa nuova si apre
static bool flipAnimate(int i, int dOld, int dNew) {
  int x, y, w, h;
  flipRect(i, x, y, w, h);
  TFT_eSprite sp(&tft);
  sp.setColorDepth(16);
  if (!sp.createSprite(w, h)) return false;
  uint16_t* bo = (uint16_t*)malloc((size_t)w * h * 2);
  uint16_t* bn = (uint16_t*)malloc((size_t)w * h * 2);
  if (!bo || !bn) {
    free(bo);
    free(bn);
    sp.deleteSprite();
    return false;
  }
  flipCardSprite(sp, i, dOld);
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) bo[r * w + c] = sp.readPixel(c, r);
  flipCardSprite(sp, i, dNew);
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) bn[r * w + c] = sp.readPixel(c, r);
  sp.deleteSprite();

  tft.setSwapBytes(true);
  const int mid = h / 2;
  const int T = (i < 4) ? 5 : 4;                       // fotogrammi per fase
  uint32_t next = millis();
  for (int f = 1; f <= 2 * T; f++) {
    if (f <= T) {                                      // fase 1: il vecchio foglio alto cade verso la cerniera
      const int hs = mid * (T - f) / T;                // altezza residua del foglio che cade
      for (int r = 0; r < mid - hs; r++) tft.pushImage(x, y + r, w, 1, bn + r * w);            // nuova meta' alta scoperta
      for (int r = 0; r < hs; r++) tft.pushImage(x, y + mid - hs + r, w, 1, bo + (r * mid / (hs ? hs : 1)) * w);
    } else {                                           // fase 2: il nuovo foglio basso si apre
      const int hs = mid * (f - T) / T;                // altezza del foglio che si apre
      for (int r = 0; r < hs; r++) tft.pushImage(x, y + mid + r, w, 1, bn + (mid + r * mid / hs) * w);
      for (int r = hs; r < mid; r++) tft.pushImage(x, y + mid + r, w, 1, bo + (mid + r) * w);
    }
    next += 12;                                        // ritmo costante, circa 80 fotogrammi al secondo
    while ((int32_t)(millis() - next) < 0) { }
  }
  free(bo);
  free(bn);
  return true;
}

static void flipUpdate(bool full) {
  struct tm t;
  const bool ok = getTm(t);
  int8_t dg[6];
  if (ok) {
    int h = t.tm_hour;
    if (cfg.fmt12) { h %= 12; if (h == 0) h = 12; }
    dg[0] = (cfg.fmt12 && h < 10) ? -2 : (int8_t)(h / 10);
    dg[1] = (int8_t)(h % 10);
    dg[2] = (int8_t)(t.tm_min / 10);
    dg[3] = (int8_t)(t.tm_min % 10);
    dg[4] = (int8_t)(t.tm_sec / 10);
    dg[5] = (int8_t)(t.tm_sec % 10);
  } else {
    for (int i = 0; i < 6; i++) dg[i] = -2;
  }
  if (ok == flipNoTime || full) {          // cambio di stato: ridisegna la data
    flipNoTime = !ok;
    drawDate(ok ? &t : nullptr);
  }
  bool any = false;
  if (!full) {
    for (int i = 0; i < 6; i++)
      if (flipDig[i] != dg[i]) {
        if (flipDig[i] != -1 && flipAnimate(i, flipDig[i], dg[i])) any = true;      // giro fluido
        else { flipDraw(i, flipDig[i], true); any = true; delay(45); }               // senza memoria: giro a due fotogrammi
      }
  }
  for (int i = 0; i < 6; i++) {
    if (full || flipDig[i] != dg[i]) {
      flipDraw(i, dg[i], false);
      flipDig[i] = dg[i];
    }
  }
  if (ok && cfg.fmt12) {
    static int8_t lastPm = -1;
    const int8_t pm = t.tm_hour >= 12;
    if (full || pm != lastPm) {
      lastPm = pm;
      drawStr(pm ? "PM" : "AM", 170, 164, 4, 0, C_ACC, C_BG);
    }
  }
}

void flipFull() {
  tft.fillScreen(C_BG);
  struct tm t;
  flipNoTime = getTm(t);                   // forza il primo ridisegno della data
  for (int i = 0; i < 6; i++) flipDig[i] = -1;
  tft.fillRoundRect(116, 72, 8, 8, 2, C_ACC);   // due punti tra ore e minuti
  tft.fillRoundRect(116, 98, 8, 8, 2, C_ACC);
  flipUpdate(true);
}

void flipTick() {
  static uint32_t at = 0;
  if (millis() - at < 100) return;
  at = millis();
  flipUpdate(false);
}

// --- 3: Matrix (pioggia di caratteri, ora, data e temperatura) ---
static const int MX_X = 14, MX_Y = 50, MX_W = 212, MX_H = 140;
static int mxSec = -1, mxMin = -1, mxDay = -1;

static void mxTemp() {
  const uint16_t g1 = RGB565(0, 255, 70), g2 = RGB565(0, 170, 45);
  tft.fillRect(MX_X + 4, MX_Y + 108, MX_W - 8, 20, 0);
  drawStr("Temp", MX_X + 12, MX_Y + 110, 2, 0, g2, 0);
  if (W.valid) drawStr(String((int)lroundf(W.t)) + "~C", MX_X + MX_W - 12, MX_Y + 110, 2, 2, g1, 0);
  else drawStr("--", MX_X + MX_W - 12, MX_Y + 110, 2, 2, g2, 0);
}

static void mxDraw(bool full, bool dataChanged) {
  const uint16_t g1 = RGB565(0, 255, 70), g2 = RGB565(0, 170, 45), wt = RGB565(210, 255, 220);
  struct tm t;
  const bool ok = getTm(t);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  if (full) {
    tft.fillRect(MX_X, MX_Y, MX_W, MX_H, 0);
    tft.drawRect(MX_X, MX_Y, MX_W, MX_H, RGB565(0, 90, 25));
    tft.drawRect(MX_X + 1, MX_Y + 1, MX_W - 2, MX_H - 2, RGB565(0, 40, 12));
    mxSec = mxMin = mxDay = -1;
  }
  tft.setTextColor(wt, 0);
  tft.drawString("> OROLOGIO", MX_X + 10, MX_Y + 8, 2);
  tft.fillRect(MX_X + 10 + 96, MX_Y + 10, 8, 12, syCursor ? g1 : 0);
  if (!ok) {
    if (full) drawStr("Sincronizzo l'ora...", MX_X + MX_W / 2, MX_Y + 70, 2, 1, g2, 0);
    if (dataChanged) mxTemp();
    return;
  }
  int h = t.tm_hour;
  const bool pm = h >= 12;
  if (cfg.fmt12) { h %= 12; if (h == 0) h = 12; }
  const int key = h * 100 + t.tm_min;
  if (full || key != mxMin) {
    mxMin = key;
    char b[8];
    if (cfg.fmt12) snprintf(b, sizeof b, "%d:%02d", h, t.tm_min);
    else snprintf(b, sizeof b, "%02d:%02d", h, t.tm_min);
    tft.fillRect(MX_X + 4, MX_Y + 30, 172, 52, 0);
    tft.setTextColor(wt, 0);
    tft.drawString(b, MX_X + 12, MX_Y + 32, 7);
    if (cfg.fmt12) drawStr(pm ? "PM" : "AM", MX_X + MX_W - 12, MX_Y + 34, 2, 2, g2, 0);
  }
  if (full || t.tm_sec != mxSec) {
    mxSec = t.tm_sec;
    char b[8];
    snprintf(b, sizeof b, "%02d", t.tm_sec);
    tft.setTextColor(g1, 0);
    tft.drawString(b, MX_X + MX_W - 12 - tft.textWidth("88", 4), MX_Y + 56, 4);
  }
  if (full || t.tm_mday != mxDay) {
    mxDay = t.tm_mday;
    char b[40];
    snprintf(b, sizeof b, "%s %d %s %d", GIORNI[t.tm_wday], t.tm_mday, MESI[t.tm_mon], t.tm_year + 1900);
    tft.fillRect(MX_X + 4, MX_Y + 86, MX_W - 8, 18, 0);
    drawStr(b, MX_X + MX_W / 2, MX_Y + 88, 2, 1, g2, 0);
  }
  if (full || dataChanged) mxTemp();
}

void mxFull() {
  tft.fillScreen(0);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  syHX = MX_X; syHY = MX_Y; syHW = MX_W; syHH = MX_H;
  for (int c = 0; c < SY_COLS; c++) syResetColumn(c, true);
  syCursor = true;
  syNextFrame = millis() + 50;
  syNextPanel = millis() + 500;
  mxDraw(true, false);
}

void mxTick(bool dataChanged) {
  syRain();
  bool blink = false;
  if ((int32_t)(millis() - syNextPanel) >= 0) {
    syNextPanel = millis() + 500;
    syCursor = !syCursor;
    blink = true;
  }
  struct tm t;
  const bool secCh = getTm(t) && t.tm_sec != mxSec;
  if (blink || secCh || dataChanged) mxDraw(false, dataChanged);
}

void wcFull();   // orologi nel mondo (definiti piu' sotto)
void wcTick();

void clockFull(int f) {
  if (f == 0) clk1Full();
  else if (f == 1) anaFull();
  else if (f == 2) flipFull();
  else if (f == 3) mxFull();
  else wcFull();
}

void clockTick(int f, bool dataChanged) {
  if (f == 0) clk1Tick(dataChanged);
  else if (f == 1) anaTick();
  else if (f == 2) flipTick();
  else if (f == 3) mxTick(dataChanged);
  else wcTick();
}

// icona del menu: quadrante analogico
void iconClock(int cx, int cy) {
  tft.fillCircle(cx, cy, 48, C_CARD);
  tft.drawCircle(cx, cy, 48, C_ACC);
  tft.drawCircle(cx, cy, 47, C_ACC);
  for (int i = 0; i < 12; i++) {
    const float a = i * 30.0f * 0.0174533f;
    const float dx = sinf(a), dy = -cosf(a);
    tft.drawLine(cx + (int)lroundf(dx * 38), cy + (int)lroundf(dy * 38),
                 cx + (int)lroundf(dx * 44), cy + (int)lroundf(dy * 44), C_TEXT);
  }
  tft.fillTriangle(cx - 3, cy - 2, cx + 3, cy + 2, cx - 20, cy - 22, C_TEXT);   // ore (circa le 10)
  tft.fillTriangle(cx - 2, cy + 3, cx + 2, cy - 3, cx + 26, cy - 18, C_ACC);    // minuti (circa le 2)
  tft.drawLine(cx, cy, cx - 6, cy + 30, C_ACC2);
  tft.fillCircle(cx, cy, 4, C_ACC2);
}

// ------------------------------------------------------------ POMODORO ------
// Tecnica del pomodoro: 25 min di lavoro, 5 di pausa; dopo 4 pomodori una pausa lunga (tempi regolabili dal web).
static uint8_t poPhase = 0;      // 0 lavoro, 1 pausa breve, 2 pausa lunga
static uint8_t poState = 0;      // 0 pronto, 1 in corso, 2 in pausa
static uint8_t poDone = 0;       // pomodori completati nel ciclo (0..4)
static uint32_t poEndAt = 0, poLeft = 0;
static int poLastSec = -1, poSeg = -1;
static bool poBlink = false;
static uint32_t poBlinkAt = 0;
static const int PCX = 120, PCY = 118, PRO = 100, PRI = 86;

static uint32_t poDur(uint8_t ph) {
  return (uint32_t)(ph == 0 ? cfg.pomW : (ph == 1 ? cfg.pomB : cfg.pomL)) * 60000UL;
}
static uint16_t poCol() {
  return poPhase == 0 ? RGB565(255, 86, 64) : (poPhase == 1 ? RGB565(70, 220, 140) : RGB565(90, 170, 255));
}
static void poLoad(uint8_t ph) {
  poPhase = ph;
  poLeft = poDur(ph);
}
void poReset() {
  poState = 0;
  poDone = 0;
  poLoad(0);
  poLastSec = -1;
  poSeg = -1;
}
static uint32_t poRemain() {
  if (poState == 1) {
    const int32_t d = (int32_t)(poEndAt - millis());
    return d > 0 ? (uint32_t)d : 0;
  }
  return poLeft;
}
void xpOnPomodoro();
bool xpCelebrating();
// fine della fase: passa alla successiva (e continua a contare). Vero se e' cambiata.
static bool poUpdate() {
  if (poState != 1 || (int32_t)(millis() - poEndAt) < 0) return false;
  if (poPhase == 0) {
    xpOnPomodoro();                       // un pomodoro in piu': XP, obiettivo e badge
    poDone++;
    poLoad(poDone >= 4 ? 2 : 1);
  } else {
    if (poPhase == 2) poDone = 0;
    poLoad(0);
  }
  poEndAt = millis() + poLeft;
  return true;
}
static void poToggle() {
  if (poState == 1) {
    poLeft = poRemain();
    poState = 2;
  } else {
    poEndAt = millis() + poLeft;
    poState = 1;
  }
}

static void poSegDraw(int i, uint16_t col) {
  const float a0 = i * 0.0174533f, a1 = (i + 1.8f) * 0.0174533f;
  const float s0 = sinf(a0), c0 = cosf(a0), s1 = sinf(a1), c1 = cosf(a1);
  const int ox0 = PCX + (int)lroundf(s0 * PRO), oy0 = PCY - (int)lroundf(c0 * PRO);
  const int ox1 = PCX + (int)lroundf(s1 * PRO), oy1 = PCY - (int)lroundf(c1 * PRO);
  const int ix0 = PCX + (int)lroundf(s0 * PRI), iy0 = PCY - (int)lroundf(c0 * PRI);
  const int ix1 = PCX + (int)lroundf(s1 * PRI), iy1 = PCY - (int)lroundf(c1 * PRI);
  tft.fillTriangle(ox0, oy0, ox1, oy1, ix1, iy1, col);
  tft.fillTriangle(ox0, oy0, ix1, iy1, ix0, iy0, col);
}

static void poRing(bool full) {
  const uint32_t tot = poDur(poPhase);
  const uint32_t rem = poRemain();
  int seg = tot ? (int)((tot - (rem > tot ? tot : rem)) * 360ULL / tot) : 0;
  if (seg > 360) seg = 360;
  const uint16_t col = poCol();
  if (full || poSeg < 0 || seg < poSeg) {
    tft.fillCircle(PCX, PCY, PRO, C_TRACK);
    tft.fillCircle(PCX, PCY, PRI, C_BG);
    for (int i = 0; i < seg; i++) poSegDraw(i, col);
  } else {
    for (int i = poSeg; i < seg; i++) poSegDraw(i, col);
  }
  poSeg = seg;
}

static void poTime() {
  const uint32_t s = (poRemain() + 999) / 1000;
  char b[12];
  snprintf(b, sizeof b, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(136);
  tft.setTextColor(poState == 2 ? RGB565(255, 200, 60) : C_TEXT, C_BG);
  tft.drawString(b, PCX, PCY + 2, 7);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

static void poLabels() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(112);                 // resta dentro l'anello (non copre il cerchio)
  tft.setTextColor(poCol(), C_BG);
  const char* lb = poPhase == 0 ? "LAVORO" : (poPhase == 1 ? "PAUSA" : "PAUSA LUNGA");
  tft.drawString(lb, PCX, PCY - 44, tft.textWidth(lb, 4) <= 112 ? 4 : 2);
  tft.setTextPadding(100);
  const char* st = poState == 0 ? "PREMI AVANTI" : (poState == 1 ? "IN CORSO" : (poBlink ? "" : "IN PAUSA"));
  tft.setTextColor(poState == 2 ? RGB565(255, 200, 60) : C_DIM, C_BG);
  tft.drawString(st, PCX, PCY + 40, 2);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

static void poDots() {
  for (int i = 0; i < 4; i++) {
    const int x = PCX + (int)lroundf((i - 1.5f) * 20);
    tft.fillCircle(x, PCY + 66, 6, C_BG);
    if (i < poDone) tft.fillCircle(x, PCY + 66, 6, RGB565(255, 86, 64));
    else tft.drawCircle(x, PCY + 66, 6, C_TRACK);
  }
}

static void poFooter() {
  tft.fillRect(0, 222, 240, 18, C_BG);
  drawStr(poState == 0 ? "Avanti: inizia - 2 tocchi: XP" : "Avanti pausa - Indietro azzera", 120, 223, 2, 1, C_DIM, C_BG);
}

void pomoFull() {
  poUpdate();
  tft.fillScreen(C_BG);
  drawStr("POMODORO", 120, 0, 2, 1, C_ACC2, C_BG);
  poSeg = -1;
  poRing(true);
  poTime();
  poLabels();
  poDots();
  poFooter();
  poLastSec = (int)((poRemain() + 999) / 1000);
}

void pomoTick() {
  if (poUpdate()) {                       // fase conclusa: lampeggia nel colore della nuova fase
    for (int k = 0; k < 3; k++) {
      tft.fillScreen(poCol());
      delay(110);
      pomoFull();
      delay(110);
    }
    if (xpCelebrating()) { funcIdx = 1; needFull = true; }   // obiettivo, badge o livello: Bibi festeggia nella schermata XP
    return;
  }
  const int sec = (int)((poRemain() + 999) / 1000);
  if (sec != poLastSec) {
    poLastSec = sec;
    poTime();
    poRing(false);
  }
  if (poState == 2 && millis() - poBlinkAt >= 500) {   // "IN PAUSA" lampeggia
    poBlinkAt = millis();
    poBlink = !poBlink;
    poLabels();
  }
}

// icona del menu: pomodoro
void iconPomodoro(int cx, int cy) {
  tft.fillCircle(cx, cy + 8, 42, RGB565(235, 60, 45));
  tft.fillCircle(cx - 15, cy - 2, 8, RGB565(255, 125, 105));
  const uint16_t lf = RGB565(50, 190, 90);
  tft.fillTriangle(cx, cy - 26, cx - 24, cy - 28, cx - 10, cy - 16, lf);
  tft.fillTriangle(cx, cy - 26, cx + 24, cy - 28, cx + 10, cy - 16, lf);
  tft.fillTriangle(cx, cy - 26, cx - 8, cy - 14, cx + 8, cy - 14, lf);
  tft.fillRect(cx - 2, cy - 40, 5, 14, RGB565(30, 140, 70));
}

// ------------------------------------------------------------ TIMER ---------
// Conto alla rovescia a ore, minuti e secondi (valore iniziale dal web, predefinito 0:10:00).
static uint8_t tmState = 0;      // 0 pronto, 1 in corso, 2 in pausa, 3 scaduto
static uint32_t tmEndAt = 0, tmLeft = 0, tmBlinkAt = 0;
static int tmLastSec = -1, tmBarPx = -1;
static bool tmBlink = false;

static uint32_t tmTotalMs() {
  uint32_t s = (uint32_t)cfg.tmH * 3600UL + (uint32_t)cfg.tmM * 60UL + cfg.tmS;
  if (!s) s = 600;
  return s * 1000UL;
}
void tmReset() {
  tmState = 0;
  tmLeft = tmTotalMs();
  tmLastSec = -1;
  tmBarPx = -1;
}
static uint32_t tmRemain() {
  if (tmState == 1) {
    const int32_t d = (int32_t)(tmEndAt - millis());
    return d > 0 ? (uint32_t)d : 0;
  }
  return tmState == 3 ? 0 : tmLeft;
}
static void tmToggle() {
  if (tmState == 1) {
    tmLeft = tmRemain();
    tmState = 2;
  } else if (tmState == 3) {
    tmReset();
  } else {
    tmEndAt = millis() + tmLeft;
    tmState = 1;
  }
}

static uint16_t tmColor() {                        // verde -> arancio -> rosso man mano che il tempo finisce
  const uint32_t tot = tmTotalMs(), rem = tmRemain();
  if (tmState == 3) return RGB565(255, 86, 64);
  if (rem * 2 > tot) return RGB565(70, 220, 140);
  if (rem * 5 > tot) return RGB565(255, 200, 60);
  return RGB565(255, 86, 64);
}

static void tmTime() {
  const uint32_t s = (tmRemain() + 999) / 1000;
  char b[16];
  if (tmTotalMs() >= 3600000UL) snprintf(b, sizeof b, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  else snprintf(b, sizeof b, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  const int font = tft.textWidth(b, 7) <= 232 ? 7 : 6;
  uint16_t col = C_TEXT;
  if (tmState == 2) col = RGB565(255, 200, 60);
  if (tmState == 3) col = tmBlink ? RGB565(255, 86, 64) : C_DIM;
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(236);
  tft.setTextColor(col, C_BG);
  tft.drawString(b, 120, 92, font);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

static void tmBar(bool full) {                     // barra di avanzamento (tempo rimasto)
  const int X = 20, Y = 138, W = 200, H = 14;
  const uint32_t tot = tmTotalMs(), rem = tmRemain();
  int px = (int)((uint64_t)W * (rem > tot ? tot : rem) / tot);
  if (!full && px == tmBarPx) return;
  tmBarPx = px;
  tft.fillRoundRect(X, Y, W, H, 7, C_TRACK);
  if (px > 0) tft.fillRoundRect(X, Y, px < 8 ? 8 : px, H, 7, tmColor());
}

static void tmStatus() {
  const char* st = tmState == 0 ? "PRONTO" : (tmState == 1 ? "IN CORSO" : (tmState == 2 ? (tmBlink ? "" : "IN PAUSA") : (tmBlink ? "TEMPO SCADUTO!" : "")));
  uint16_t col = C_DIM;
  if (tmState == 2) col = RGB565(255, 200, 60);
  if (tmState == 3) col = RGB565(255, 86, 64);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(236);
  tft.setTextColor(col, C_BG);
  tft.drawString(st, 120, 188, 4);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

static void tmFooter() {
  tft.fillRect(0, 218, 240, 22, C_BG);
  drawStr(tmState == 0 ? "Avanti: avvia" : (tmState == 3 ? "Avanti: azzera" : "Avanti pausa - 2 tocchi azzera"), 120, 222, 2, 1, C_DIM, C_BG);
}

void timerFull() {
  if (tmState == 1 && (int32_t)(millis() - tmEndAt) >= 0) tmState = 3;   // scaduto mentre eri altrove
  tft.fillScreen(C_BG);
  drawStr("TIMER", 120, 6, 4, 1, C_ACC, C_BG);
  tmBarPx = -1;
  tmTime();
  tmBar(true);
  char b[28];
  const uint32_t tot = tmTotalMs() / 1000;
  snprintf(b, sizeof b, "Impostato: %lu:%02lu:%02lu", (unsigned long)(tot / 3600), (unsigned long)((tot / 60) % 60), (unsigned long)(tot % 60));
  drawStr(b, 120, 160, 2, 1, C_DIM, C_BG);
  tmStatus();
  tmFooter();
  tmLastSec = (int)((tmRemain() + 999) / 1000);
}

void timerTick() {
  if (tmState == 1 && (int32_t)(millis() - tmEndAt) >= 0) {       // tempo scaduto: lampeggia a schermo intero
    tmState = 3;
    tmBlink = true;
    for (int k = 0; k < 3; k++) {
      tft.fillScreen(RGB565(255, 86, 64));
      delay(110);
      timerFull();
      delay(110);
    }
    tmBlinkAt = millis();
    return;
  }
  const int sec = (int)((tmRemain() + 999) / 1000);
  if (sec != tmLastSec) {
    tmLastSec = sec;
    tmTime();
    tmBar(false);
  }
  if ((tmState == 2 || tmState == 3) && millis() - tmBlinkAt >= 500) {   // "IN PAUSA" / "TEMPO SCADUTO" lampeggiano
    tmBlinkAt = millis();
    tmBlink = !tmBlink;
    tmStatus();
    if (tmState == 3) tmTime();
  }
}

// icona del menu: clessidra
void iconTimer(int cx, int cy) {
  const uint16_t glass = c565(mixRGB(th->bg, 0x9FD8FF, 80)), sand = RGB565(255, 196, 64), fr = RGB565(220, 224, 236);
  tft.fillTriangle(cx - 30, cy - 38, cx + 30, cy - 38, cx, cy, glass);
  tft.fillTriangle(cx - 30, cy + 38, cx + 30, cy + 38, cx, cy, glass);
  tft.fillTriangle(cx - 20, cy - 32, cx + 20, cy - 32, cx, cy - 6, sand);      // sabbia in alto
  tft.fillTriangle(cx - 24, cy + 38, cx + 24, cy + 38, cx, cy + 14, sand);     // mucchio in basso
  tft.drawFastVLine(cx, cy - 6, 22, sand);                                      // filo di sabbia
  tft.fillRoundRect(cx - 36, cy - 46, 72, 8, 3, fr);
  tft.fillRoundRect(cx - 36, cy + 38, 72, 8, 3, fr);
}

// ------------------------------------------------------- OROLOGI NEL MONDO -----
// Quinto quadrante: l'ora di 4 citta scelte dal web. Le regole dell'ora legale sono calcolate qui (nessun database dei fusi).
struct WcCity { const char* name; int16_t std; uint8_t rule; };   // rule: 0 nessuna, 1 UE, 2 USA, 3 Australia sud, 4 Nuova Zelanda
static const WcCity WC_CITY[] = {
  {"Roma", 60, 1}, {"Londra", 0, 1}, {"Lisbona", 0, 1}, {"Atene", 120, 1}, {"Istanbul", 180, 0}, {"Mosca", 180, 0},
  {"Dubai", 240, 0}, {"Karachi", 300, 0}, {"Mumbai", 330, 0}, {"Bangkok", 420, 0}, {"Pechino", 480, 0}, {"Hong Kong", 480, 0},
  {"Singapore", 480, 0}, {"Tokyo", 540, 0}, {"Seul", 540, 0}, {"Sydney", 600, 3}, {"Auckland", 720, 4}, {"Honolulu", -600, 0},
  {"Anchorage", -540, 2}, {"Los Angeles", -480, 2}, {"Denver", -420, 2}, {"Chicago", -360, 2}, {"New York", -300, 2},
  {"Toronto", -300, 2}, {"Citta Messico", -360, 0}, {"Bogota", -300, 0}, {"Lima", -300, 0}, {"Sao Paulo", -180, 0},
  {"Buenos Aires", -180, 0}, {"Reykjavik", 0, 0}, {"Nairobi", 180, 0}, {"Johannesburg", 120, 0}, {"Lagos", 60, 0}};
static const int WC_N = (int)(sizeof(WC_CITY) / sizeof(WC_CITY[0]));

static int wcIdx(int i) {                      // citta scelta nella posizione i (con predefiniti: Roma, New York, Tokyo, Sydney)
  static const uint8_t DEF[4] = {0, 22, 13, 15};
  const uint8_t v = cfg.wc[i & 3];
  return v < WC_N ? v : DEF[i & 3];
}

static int32_t daysFromCivil(int y, int m, int d) {   // giorni dal 1970-01-01
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}
static int wcLastSun(int y, int m) {
  static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const int d = dim[m - 1];
  return d - dowOf(y, m, d);
}
static int wcNthSun(int y, int m, int n) { return 1 + (7 - dowOf(y, m, 1)) % 7 + 7 * (n - 1); }

static int wcOffset(const WcCity& c, time_t utc) {   // scostamento da UTC in minuti, con ora legale
  int off = c.std;
  if (!c.rule) return off;
  struct tm g;
  gmtime_r(&utc, &g);
  const int y = g.tm_year + 1900;
  const int64_t u = (int64_t)utc / 60;
  bool dst = false;
  int64_t st, en;
  switch (c.rule) {
    case 1:
      st = (int64_t)daysFromCivil(y, 3, wcLastSun(y, 3)) * 1440 + 60;
      en = (int64_t)daysFromCivil(y, 10, wcLastSun(y, 10)) * 1440 + 60;
      dst = u >= st && u < en;
      break;
    case 2:
      st = (int64_t)daysFromCivil(y, 3, wcNthSun(y, 3, 2)) * 1440 + 120 - c.std;
      en = (int64_t)daysFromCivil(y, 11, wcNthSun(y, 11, 1)) * 1440 + 120 - (c.std + 60);
      dst = u >= st && u < en;
      break;
    case 3:
      st = (int64_t)daysFromCivil(y, 10, wcNthSun(y, 10, 1)) * 1440 + 120 - c.std;
      en = (int64_t)daysFromCivil(y, 4, wcNthSun(y, 4, 1)) * 1440 + 180 - (c.std + 60);
      dst = u >= st || u < en;
      break;
    default:
      st = (int64_t)daysFromCivil(y, 9, wcLastSun(y, 9)) * 1440 + 120 - c.std;
      en = (int64_t)daysFromCivil(y, 4, wcNthSun(y, 4, 1)) * 1440 + 180 - (c.std + 60);
      dst = u >= st || u < en;
      break;
  }
  return dst ? off + 60 : off;
}

static int wcKey[4] = {-1, -1, -1, -1};

static void wcDraw(int i, bool full) {
  const WcCity& c = WC_CITY[wcIdx(i)];
  const time_t utc = time(nullptr);
  const int off = wcOffset(c, utc);
  const time_t lt = utc + (time_t)off * 60;
  struct tm g;
  gmtime_r(&lt, &g);
  const int key = g.tm_hour * 100 + g.tm_min + wcIdx(i) * 10000;
  if (!full && key == wcKey[i]) return;
  wcKey[i] = key;
  const int X = 5 + (i & 1) * 118, Y = 22 + (i >> 1) * 108, W = 112, H = 104;
  const bool day = g.tm_hour >= 7 && g.tm_hour < 19;
  const uint16_t bg = day ? C_CARD : c565(mixRGB(th->card, th->bg, 150));
  tft.fillRoundRect(X, Y, W, H, 10, bg);
  tft.drawRoundRect(X, Y, W, H, 10, c565(mixRGB(th->card, th->a1, 110)));
  drawStr(c.name, X + W / 2, Y + 8, 2, 1, C_ACC2, bg);
  char b[16];
  int h = g.tm_hour;
  const bool pm = h >= 12;
  if (cfg.fmt12) { h %= 12; if (h == 0) h = 12; snprintf(b, sizeof b, "%d:%02d", h, g.tm_min); }
  else snprintf(b, sizeof b, "%02d:%02d", h, g.tm_min);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(day ? C_TEXT : c565(mixRGB(th->text, th->dim, 150)), bg);
  tft.drawString(b, X + W / 2 - (cfg.fmt12 ? 10 : 0), Y + 44, 4);
  tft.setTextDatum(TL_DATUM);
  if (cfg.fmt12) drawStr(pm ? "PM" : "AM", X + W - 12, Y + 38, 2, 2, C_ACC, bg);
  snprintf(b, sizeof b, "%s %d", GIORNI[g.tm_wday], g.tm_mday);
  drawStr(b, X + W / 2, Y + 68, 2, 1, C_DIM, bg);
  const int dl = off - wcOffset(WC_CITY[0], utc);          // differenza rispetto all'ora di Roma
  if (dl == 0) snprintf(b, sizeof b, "ora locale");
  else if (dl % 60 == 0) snprintf(b, sizeof b, "%+d h", dl / 60);
  else snprintf(b, sizeof b, "%+d:%02d h", dl / 60, abs(dl) % 60);
  drawStr(b, X + W / 2, Y + 86, 2, 1, dl == 0 ? C_ACC : C_DIM, bg);
  // sole (giorno) o mezzaluna (notte) nell'angolo
  const int ix = X + W - 14, iy = Y + 14;
  if (day) {
    tft.fillCircle(ix, iy, 4, RGB565(255, 200, 60));
  } else {
    tft.fillCircle(ix, iy, 5, RGB565(225, 230, 255));
    tft.fillCircle(ix + 3, iy - 2, 5, bg);
  }
}

void wcFull() {
  tft.fillScreen(C_BG);
  drawStr("OROLOGI NEL MONDO", 120, 3, 2, 1, C_ACC2, C_BG);
  for (int i = 0; i < 4; i++) wcKey[i] = -1;
  if (time(nullptr) < 1700000000) {
    drawStr("Sincronizzo l'ora...", 120, 112, 2, 1, C_DIM, C_BG);
    return;
  }
  for (int i = 0; i < 4; i++) wcDraw(i, true);
}

void wcTick() {
  static uint32_t last = 0;
  if (millis() - last < 500) return;
  last = millis();
  if (time(nullptr) < 1700000000) return;
  const bool first = wcKey[0] == -1;
  for (int i = 0; i < 4; i++) wcDraw(i, first);
}

// ---------------------------------------------------------- POMODORO: XP -------
// Ogni pomodoro completato vale 25 XP; raggiungere l'obiettivo del giorno vale 50 XP in piu'.
struct XpData {
  uint32_t xp = 0, total = 0;
  uint16_t badges = 0, streak = 0;
  int32_t lastDay = 0, goalDay = 0;
  uint8_t wk[7] = {0, 0, 0, 0, 0, 0, 0};          // pomodori degli ultimi 7 giorni (0 = oggi)
};
static XpData xs;
static bool xpLoaded = false, xpDirty = false;
static bool xpCelPending = false, xpCelActive = false;
static char xpCelText[40] = "";
static uint32_t xpCelStart = 0, xpCelNext = 0;
static uint16_t xpCelFrame = 0;
static int xpShown = -1;

static const char* const XP_BADGE[8] = {"Primo pomodoro", "Dieci pomodori", "Maratoneta", "Leggenda", "Obiettivo!", "Serie di 3", "Serie di 7", "Livello 5"};
static const char* const XP_RANK[7] = {"Cucciolo", "Curioso", "Studioso", "Concentrato", "Maestro", "Guru", "Leggenda"};

static void xpLoad() {
  if (xpLoaded) return;
  xpLoaded = true;
  Preferences pf;
  pf.begin("nmtv", true);
  xs.xp = pf.getUInt("xpx", 0);
  xs.total = pf.getUInt("xpt", 0);
  xs.badges = pf.getUShort("xpb", 0);
  xs.streak = pf.getUShort("xps", 0);
  xs.lastDay = pf.getInt("xpd", 0);
  xs.goalDay = pf.getInt("xpq", 0);
  if (pf.getBytes("xpw", xs.wk, 7) != 7) memset(xs.wk, 0, 7);
  pf.end();
}
static void xpSave() {
  Preferences pf;
  pf.begin("nmtv", false);
  pf.putUInt("xpx", xs.xp);
  pf.putUInt("xpt", xs.total);
  pf.putUShort("xpb", xs.badges);
  pf.putUShort("xps", xs.streak);
  pf.putInt("xpd", xs.lastDay);
  pf.putInt("xpq", xs.goalDay);
  pf.putBytes("xpw", xs.wk, 7);
  pf.end();
}
void xpReset() {
  xs = XpData();
  xpLoaded = true;
  xpSave();
  xpDirty = true;
  xpCelPending = false;
  xpCelActive = false;
}
static int xpLevel(uint32_t xp) {
  int l = 1;
  while (xp >= 50UL * l * l && l < 99) l++;
  return l;
}
static int32_t xpToday() {
  struct tm t;
  if (!getTm(t)) return 0;
  return daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}
static void xpRoll(int32_t today) {                // scorre la settimana se e' cambiato il giorno
  if (!today) return;
  if (xs.lastDay == 0) xs.lastDay = today;
  if (today > xs.lastDay) {
    const int32_t dl = today - xs.lastDay;
    if (dl >= 7) {
      memset(xs.wk, 0, 7);
    } else {
      for (int k = 6; k >= 0; k--) xs.wk[k] = (k >= dl) ? xs.wk[k - dl] : 0;
    }
    xs.lastDay = today;
  }
  if (xs.goalDay && xs.goalDay < today - 1) xs.streak = 0;   // ieri l'obiettivo e' mancato: la serie riparte
}
static int xpStreakNow() {
  const int32_t today = xpToday();
  return (xs.goalDay && today && xs.goalDay >= today - 1) ? xs.streak : 0;
}

// chiamata a ogni pomodoro di lavoro concluso
void xpOnPomodoro() {
  xpLoad();
  const int lvOld = xpLevel(xs.xp);
  const int32_t today = xpToday();
  const uint16_t oldB = xs.badges;
  char msg[40] = "";
  xs.total++;
  xs.xp += 25;
  if (today) {
    xpRoll(today);
    if (xs.wk[0] < 250) xs.wk[0]++;
    if (xs.wk[0] >= cfg.xpGoal && xs.goalDay != today) {
      xs.streak = (xs.goalDay == today - 1) ? (uint16_t)(xs.streak + 1) : 1;
      xs.goalDay = today;
      xs.xp += 50;
      xs.badges |= 1u << 4;
      snprintf(msg, sizeof msg, "Obiettivo raggiunto!");
    }
  }
  const int lvNew = xpLevel(xs.xp);
  if (xs.total >= 1) xs.badges |= 1u << 0;
  if (xs.total >= 10) xs.badges |= 1u << 1;
  if (xs.total >= 50) xs.badges |= 1u << 2;
  if (xs.total >= 100) xs.badges |= 1u << 3;
  if (xpStreakNow() >= 3) xs.badges |= 1u << 5;
  if (xpStreakNow() >= 7) xs.badges |= 1u << 6;
  if (lvNew >= 5) xs.badges |= 1u << 7;
  const uint16_t gained = xs.badges & ~oldB;
  if (!msg[0] && gained) {
    for (int i = 0; i < 8; i++)
      if ((gained >> i) & 1u) { snprintf(msg, sizeof msg, "Badge: %s", XP_BADGE[i]); break; }
  }
  if (lvNew > lvOld) snprintf(msg, sizeof msg, "Livello %d raggiunto!", lvNew);
  xpSave();
  xpDirty = true;
  if (msg[0]) {
    snprintf(xpCelText, sizeof xpCelText, "%s", msg);
    xpCelPending = true;
  }
}
bool xpCelebrating() { return xpCelPending || xpCelActive; }
void xpEndCelebration() {
  xpCelPending = false;
  xpCelActive = false;
}

static void xpBadgeIcon(int cx, int cy, int i, bool on) {
  if (!on) {
    tft.drawCircle(cx, cy, 10, C_TRACK);
    return;
  }
  static const uint32_t COL[8] = {0xFF6B5A, 0xFFB347, 0x6CC7FF, 0xFFD54A, 0x3DDC84, 0xFF4FA0, 0xB68CFF, 0x00E5FF};
  const uint16_t c = c565(COL[i & 7]);
  tft.fillCircle(cx, cy, 10, c);
  tft.drawCircle(cx, cy, 10, C_TEXT);
  tft.fillCircle(cx, cy, 4, C_BG);
  tft.fillCircle(cx - 3, cy - 3, 2, C_TEXT);
}

void xpFull() {
  xpLoad();
  const int32_t today = xpToday();
  if (today) xpRoll(today);
  tft.fillScreen(C_BG);
  const int lv = xpLevel(xs.xp);
  char b[40];
  snprintf(b, sizeof b, "LIVELLO %d", lv);
  drawStr(b, 120, 2, 4, 1, C_ACC, C_BG);
  drawStr(XP_RANK[lv > 7 ? 6 : lv - 1], 120, 30, 2, 1, C_DIM, C_BG);
  // barra XP verso il livello successivo
  const uint32_t lo = 50UL * (lv - 1) * (lv - 1), hi = 50UL * lv * lv;
  const int W = 200;
  tft.fillRoundRect(20, 50, W, 14, 7, C_TRACK);
  const int px = (int)((uint64_t)W * (xs.xp - lo) / (hi - lo));
  if (px > 0) tft.fillRoundRect(20, 50, px < 8 ? 8 : px, 14, 7, C_ACC);
  snprintf(b, sizeof b, "%lu / %lu XP", (unsigned long)xs.xp, (unsigned long)hi);
  drawStr(b, 120, 68, 2, 1, C_TEXT, C_BG);
  // obiettivo di oggi
  const int goal = cfg.xpGoal, done = xs.wk[0];
  snprintf(b, sizeof b, "Oggi %d/%d", done, goal);
  drawStr(b, 8, 90, 2, 0, done >= goal ? RGB565(70, 220, 140) : C_TEXT, C_BG);
  for (int i = 0; i < goal && i < 12; i++) {
    const int x = 232 - (goal > 12 ? 12 : goal) * 11 + i * 11;
    if (i < done) tft.fillRoundRect(x, 92, 8, 12, 2, done >= goal ? RGB565(70, 220, 140) : RGB565(255, 86, 64));
    else tft.drawRoundRect(x, 92, 8, 12, 2, C_TRACK);
  }
  snprintf(b, sizeof b, "Serie %d g", xpStreakNow());
  drawStr(b, 8, 108, 2, 0, xpStreakNow() ? RGB565(255, 170, 60) : C_DIM, C_BG);
  snprintf(b, sizeof b, "Totale %lu", (unsigned long)xs.total);
  drawStr(b, 232, 108, 2, 2, C_DIM, C_BG);
  // statistiche della settimana
  int mx = goal;
  for (int k = 0; k < 7; k++) if (xs.wk[k] > mx) mx = xs.wk[k];
  struct tm t;
  const int wd = getTm(t) ? t.tm_wday : 0;
  static const char DL[8] = "DLMMGVS";
  const int base = 184, hmax = 40;
  for (int k = 0; k < 7; k++) {
    const int day = 6 - k;                             // colonna k = giorno (6-k) fa: l'ultima e' oggi
    const int x = 15 + k * 30;
    const int v = xs.wk[day];
    const int hgt = v * hmax / mx;
    tft.fillRoundRect(x, base - 3, 22, 3, 1, C_TRACK);
    if (v > 0) {
      tft.fillRoundRect(x, base - (hgt < 4 ? 4 : hgt), 22, hgt < 4 ? 4 : hgt, 3, day == 0 ? C_ACC : C_ACC2);
      char nb[6];
      snprintf(nb, sizeof nb, "%d", v);
      drawStr(nb, x + 11, base - (hgt < 4 ? 4 : hgt) - 16, 2, 1, C_TEXT, C_BG);
    }
    const char l[2] = {DL[(wd - day + 7) % 7], 0};
    drawStr(l, x + 11, 188, 2, 1, day == 0 ? C_ACC : C_DIM, C_BG);
  }
  const int gy = base - goal * hmax / mx;
  for (int x = 10; x < 230; x += 8) tft.drawFastHLine(x, gy, 4, c565(mixRGB(th->bg, th->a1, 120)));
  // badge
  for (int i = 0; i < 8; i++) xpBadgeIcon(24 + i * 27, 222, i, (xs.badges >> i) & 1u);
  xpShown = (int)xs.total;
  xpDirty = false;
}

static void xpCelDraw(bool full) {
  if (full) {
    tft.fillScreen(C_BG);
    drawStr("BRAVO!", 120, 3, 4, 1, C_ACC, C_BG);
    tft.fillRect(0, 196, 240, 44, C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString(xpCelText, 120, 208, tft.textWidth(xpCelText, 4) <= 232 ? 4 : 2);
    tft.setTextDatum(TL_DATUM);
    char b[40];
    snprintf(b, sizeof b, "%lu XP - livello %d", (unsigned long)xs.xp, xpLevel(xs.xp));
    drawStr(b, 120, 224, 2, 1, C_DIM, C_BG);
  }
  const bibi::Scene sc = {bibi::ST_PARTY, bibi::SKY_PARTY, 0};
  tft.setSwapBytes(true);
  bibi::drawFrame(sc, xpCelFrame, th->bg, bbCol, bbRow, 8, 31);
}

void xpTick() {
  if (xpCelPending && !xpCelActive) {              // inizia la festa di Bibi
    xpCelPending = false;
    xpCelActive = true;
    xpCelStart = millis();
    xpCelFrame = 0;
    xpCelDraw(true);
    xpCelNext = millis() + 120;
    return;
  }
  if (xpCelActive) {
    if (millis() - xpCelStart > 9000UL) {
      xpCelActive = false;
      xpFull();
      return;
    }
    if ((int32_t)(millis() - xpCelNext) >= 0) {
      xpCelNext = millis() + 120;
      xpCelFrame++;
      xpCelDraw(false);
    }
    return;
  }
  if (xpDirty) xpFull();
}

// ------------------------------------------------------------- BIBI AI ---------
// Assistente AI: scrivi a Bibi dall'interfaccia web; sul display pensa, risponde e cambia espressione.
static uint32_t aiSeen = 0xFFFFFFFF, aiNextFrame = 0;
static uint16_t aiFrame = 0;
static int aiView = 0, aiQuick = 0;
static bool aiWasBusy = false;
static String aiLastKey;

static const char* const AI_QUICK[4] = {
  "Commenta il meteo di oggi e di domani.",
  "Come sta l'aria oggi? Commenta qualita dell'aria e UV.",
  "Commenta l'andamento delle criptovalute.",
  "Dammi un consiglio per la giornata in base a meteo e orario."};

static void aiScene(uint8_t mood, bool talk, bibi::Scene& sc) {
  using namespace bibi;
  const bool night = nightActive();
  sc.variant = 0;
  if (talk && (mood == 0 || mood == 1)) { sc.st = ST_TALK; sc.sky = night ? SKY_NIGHT : SKY_DAY; return; }
  switch (mood) {
    case 1: sc.st = ST_HAPPY; sc.sky = SKY_DAY; sc.variant = 3; break;
    case 2: sc.st = ST_PARTY; sc.sky = SKY_PARTY; break;
    case 3: sc.st = ST_WOW; sc.sky = SKY_DAY; break;
    case 4: sc.st = ST_SAD; sc.sky = SKY_CLOUD; break;
    case 5: sc.st = ST_THINK; sc.sky = SKY_DAY; break;
    case 6: sc.st = ST_SLEEP; sc.sky = SKY_NIGHT; break;
    case 7: sc.st = ST_COLD; sc.sky = SKY_CLOUD; break;
    case 8: sc.st = ST_HOT; sc.sky = SKY_DAY; break;
    case 9: sc.st = ST_RAIN; sc.sky = SKY_RAIN; break;
    case 10: sc.st = ST_SNOW; sc.sky = SKY_SNOW; break;
    case 11: sc.st = ST_STORM; sc.sky = SKY_RAIN; break;
    case 12: sc.st = ST_PUMP; sc.sky = SKY_PUMP; break;
    case 13: sc.st = ST_CRASH; sc.sky = SKY_CRASH; break;
    case 14: sc.st = ST_MASK; sc.sky = SKY_SMOG; break;
    default: sc.st = ST_HAPPY; sc.sky = night ? SKY_NIGHT : SKY_DAY; sc.variant = (uint8_t)(aiFrame / 40 % 2); break;
  }
}

// testo ASCII su 2 righe (font 2, 232 px)
static void aiWrap(const String& utf8, String& l1, String& l2) {
  char buf[220];
  noteCopy(buf, sizeof buf, utf8.c_str());
  l1 = "";
  l2 = "";
  String cur;
  bool second = false;
  char* save = nullptr;
  for (char* w = strtok_r(buf, " ", &save); w; w = strtok_r(nullptr, " ", &save)) {
    String t = cur.length() ? cur + " " + w : String(w);
    if (tft.textWidth(t, 2) <= 232) { cur = t; continue; }
    if (!second) {
      l1 = cur;
      second = true;
      cur = w;
    } else {
      l2 = cur;
      while (l2.length() > 3 && tft.textWidth(l2 + "...", 2) > 232) l2 = l2.substring(0, l2.length() - 1);
      l2 += "...";
      return;
    }
  }
  if (!second) l1 = cur;
  else l2 = cur;
}

// quale messaggio mostrare adesso: ritorna scena, righe e sottotitolo
static void aiCompose(bibi::Scene& sc, String& l1, String& l2, String& sub) {
  String shown;
  uint8_t mood = 0;
  bool talk = false, thinking = false;
  sub = "";
  xSemaphoreTake(mtx, portMAX_DELAY);
  const uint8_t st = aiState;
  const String err = aiErr;
  if (st != 0) {
    thinking = true;
  } else if (err.length()) {
    shown = err;
  } else if (aiView > 0) {
    int n = 0;
    for (int i = aiHistN - 1; i >= 0; i--)
      if (!aiHist[i].user && ++n == aiView + 1) {
        String lg;
        aiParse(aiHist[i].raw, mood, shown, lg);
        break;
      }
    sub = "Risposta precedente";
  } else if (aiShort.length()) {
    shown = aiShort;
    mood = aiMood;
    talk = millis() - aiReplyAt < 2500UL;
  } else if (aiDailyText.length() && aiDailyKey == aiDayKey()) {
    shown = aiDailyText;
    mood = aiDailyMood;
    sub = "Frase del giorno";
  }
  xSemaphoreGive(mtx);
  if (thinking) {
    sc.st = bibi::ST_THINK;
    sc.sky = bibi::SKY_DAY;
    sc.variant = 0;
    l1 = "Bibi sta pensando...";
    l2 = "";
    return;
  }
  if (err.length()) {
    sc.st = bibi::ST_SAD;
    sc.sky = bibi::SKY_CLOUD;
    sc.variant = 0;
    aiWrap(err, l1, l2);
    sub = "Controlla Bibi AI nel web";
    return;
  }
  if (!shown.length()) {
    sc.st = bibi::ST_HAPPY;
    sc.sky = nightActive() ? bibi::SKY_NIGHT : bibi::SKY_DAY;
    sc.variant = 1;
    l1 = "Scrivimi dal web!";
    l2 = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
    sub = "Avanti: un commento di Bibi";
    return;
  }
  aiScene(mood, talk, sc);
  aiWrap(shown, l1, l2);
}

static void aiDrawText(const String& l1, const String& l2, const String& sub) {
  tft.fillRect(0, 196, 240, 44, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  if (l2.length() || !sub.length()) {
    tft.drawString(l1, 120, 205, 2);
    if (l2.length()) tft.drawString(l2, 120, 223, 2);
  } else {
    tft.drawString(l1, 120, 205, 2);
  }
  if (sub.length() && !l2.length()) {
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString(sub, 120, 224, 2);
  } else if (sub.length()) {
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString(sub, 232, 190, 1);
  }
  tft.setTextDatum(TL_DATUM);
}

static void aiRender(bool full) {
  bibi::Scene sc;
  String l1, l2, sub;
  aiCompose(sc, l1, l2, sub);
  const String key = l1 + "|" + l2 + "|" + sub;
  if (full || key != aiLastKey) {
    aiLastKey = key;
    aiDrawText(l1, l2, sub);
  }
  tft.setSwapBytes(true);
  bibi::drawFrame(sc, aiFrame, th->bg, bbCol, bbRow, 8, 31);
}

void aiFullScreen() {
  tft.fillScreen(C_BG);
  drawStr("BIBI AI", 120, 3, 4, 1, C_ACC, C_BG);
  aiFrame = 0;
  aiSeen = aiVer;
  aiLastKey = "";
  aiRender(true);
  aiNextFrame = millis() + 120;
}

void aiTickScreen() {
  const uint32_t ms = millis();
  if (aiVer != aiSeen) {                         // e' arrivata una risposta (o un errore)
    aiSeen = aiVer;
    if (aiWasBusy && aiState == 0) aiView = 0;
  }
  aiWasBusy = aiState != 0;
  if ((int32_t)(ms - aiNextFrame) < 0) return;
  aiNextFrame = ms + 120;
  aiFrame++;
  aiRender(false);
}

void aiGesture(Gesture g) {
  if (g == G_SINGLE) {                           // un commento rapido di Bibi, a rotazione
    if (aiState == 0) {
      aiView = 0;
      aiAsk(AI_QUICK[aiQuick], 1);
      aiQuick = (aiQuick + 1) % 4;
    }
  } else if (g == G_DOUBLE) {                    // rilegge le risposte precedenti
    int n = 0;
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < aiHistN; i++) if (!aiHist[i].user) n++;
    xSemaphoreGive(mtx);
    aiView = n > 1 ? (aiView + 1) % n : 0;
  }
}

// icona del menu: il viso di Bibi con il fumetto "AI"
void iconBibiAi(int cx, int cy) {
  iconBibi(cx - 6, cy + 8);
  tft.fillRoundRect(cx + 10, cy - 62, 54, 30, 9, C_ACC);
  tft.fillTriangle(cx + 18, cy - 34, cx + 32, cy - 34, cx + 16, cy - 24, C_ACC);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_BG, C_ACC);
  tft.drawString("AI", cx + 37, cy - 47, 4);
  tft.setTextDatum(TL_DATUM);
}

// ----------------------------------------------------------- RADAR VOLI --------
static const int FR_CX = 120, FR_CY = 112, FR_R = 82;
static Plane fp[FL_MAX];
static int fpN = 0, fpSel = 0;
static uint32_t flSeen = 0xFFFFFFFF, flNextFrame = 0;
static int flAng = 0;
static int16_t flPx[FL_MAX], flPy[FL_MAX];
static bool flDrawn[FL_MAX];
static int flPrevAng = -1;
static bool flFirst = true;

static void flSync() {
  char sel[9] = "";
  if (fpSel >= 0 && fpSel < fpN) strcpy(sel, fp[fpSel].cs);
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < flShN; i++) fp[i] = flSh[i];
  fpN = flShN;
  xSemaphoreGive(mtx);
  fpSel = 0;
  for (int i = 0; i < fpN; i++)
    if (sel[0] && !strcmp(fp[i].cs, sel)) { fpSel = i; break; }
  strcpy(gFlWant, fpN ? fp[fpSel].cs : "");
}

static void flXY(const Plane& p, uint32_t now, int& x, int& y) {
  const float dt = (now - p.t) / 1000.0f;
  const float km = p.gs * dt / 3600.0f, a = p.trk * 0.0174533f;
  const float lat = p.lat + km * cosf(a) / 110.57f;
  const float lon = p.lon + km * sinf(a) / (111.32f * cosf(cfg.lat * 0.0174533f));
  const float dx = (lon - cfg.lon) * 111.32f * cosf(cfg.lat * 0.0174533f), dy = (lat - cfg.lat) * 110.57f;
  x = FR_CX + (int)lroundf(dx / cfg.flKm * FR_R);
  y = FR_CY - (int)lroundf(dy / cfg.flKm * FR_R);
}

static uint16_t flColor(float altM) {
  return altM < 3000 ? RGB565(70, 220, 140) : (altM < 8000 ? RGB565(255, 200, 60) : RGB565(120, 205, 255));
}

static void flTitle() {
  tft.fillRect(0, 0, 240, 17, C_BG);
  drawStr("RADAR VOLI", 6, 0, 2, 0, C_ACC2, C_BG);
  char b[32];
  if (fpN) snprintf(b, sizeof b, "%d aerei - %d km", fpN, (int)cfg.flKm);
  else snprintf(b, sizeof b, "%d km", (int)cfg.flKm);
  drawStr(b, 234, 0, 2, 2, C_DIM, C_BG);
}

static void flPanel() {
  tft.fillRect(0, 195, 240, 45, C_BG);
  tft.drawFastHLine(8, 195, 224, C_TRACK);
  if (fpN == 0) {
    String s;
    if (!cfg.geo) s = "Attendo la posizione...";
    else if (flStat == 2) s = "Dati voli non disponibili";
    else if (flStat == 0) s = "Cerco aerei...";
    else s = "Nessun aereo entro " + String((int)cfg.flKm) + " km";
    drawStr(s, 120, 210, 2, 1, C_DIM, C_BG);
    return;
  }
  const Plane& p = fp[fpSel];
  char alt[14], spd[14];
  snprintf(alt, sizeof alt, "%d m", (int)lroundf(p.alt));
  snprintf(spd, sizeof spd, "%d km/h", (int)lroundf(p.gs));
  drawStr(p.cs, 8, 198, 4, 0, C_TEXT, C_BG);
  const int fa = strW(p.cs, 4) + strW(alt, 4) > 222 ? 2 : 4;
  drawStr(alt, 232, fa == 4 ? 198 : 204, fa, 2, flColor(p.alt), C_BG);
  String rt;
  if (p.rt == 1) {
    rt = p.org[0] ? String(p.org) + " > " + p.dst : String("a ") + p.dst;
    if (p.city[0]) rt += String(" ") + p.city;
  } else if (p.rt == 0) {
    rt = "Cerco la rotta...";
  } else {
    rt = "Rotta sconosciuta";
  }
  while (rt.length() > 4 && strW(rt, 2) > 146) rt = rt.substring(0, rt.length() - 1);
  drawStr(rt, 8, 223, 2, 0, p.rt == 1 ? C_ACC : C_DIM, C_BG);
  drawStr(spd, 232, 223, 2, 2, C_DIM, C_BG);
}

static void flRings() {
  const uint16_t r1 = c565(mixRGB(th->bg, th->a1, 150)), r2 = c565(mixRGB(th->bg, th->a1, 70));
  tft.drawFastHLine(FR_CX - FR_R, FR_CY, 2 * FR_R + 1, r2);
  tft.drawFastVLine(FR_CX, FR_CY - FR_R, 2 * FR_R + 1, r2);
  tft.drawCircle(FR_CX, FR_CY, FR_R / 3, r2);
  tft.drawCircle(FR_CX, FR_CY, FR_R * 2 / 3, r2);
  tft.drawCircle(FR_CX, FR_CY, FR_R, r1);
  tft.drawCircle(FR_CX, FR_CY, FR_R + 1, r1);
  tft.fillCircle(FR_CX, FR_CY, 3, C_ACC);
}

static void flSweep(int ang, bool erase) {
  for (int k = 0; k < 5; k++) {
    const float a = (ang - k * 5) * 0.0174533f;
    const int x = FR_CX + (int)lroundf(sinf(a) * (FR_R - 1)), y = FR_CY - (int)lroundf(cosf(a) * (FR_R - 1));
    tft.drawLine(FR_CX, FR_CY, x, y, erase ? C_BG : c565(mixRGB(th->bg, th->a1, (uint8_t)(255 - k * 50))));
  }
}

static void flPlaneDraw(int i, uint32_t now) {
  int x, y;
  flXY(fp[i], now, x, y);
  const int dx = x - FR_CX, dy = y - FR_CY;
  if (dx * dx + dy * dy > FR_R * FR_R) { flDrawn[i] = false; return; }
  const float a = fp[i].trk * 0.0174533f;
  const float sx = sinf(a), cy_ = -cosf(a);
  const uint16_t col = flColor(fp[i].alt);
  tft.fillTriangle(x + (int)lroundf(sx * 6), y + (int)lroundf(cy_ * 6),
                   x + (int)lroundf(-sx * 3 - cy_ * 3), y + (int)lroundf(-cy_ * 3 + sx * 3),
                   x + (int)lroundf(-sx * 3 + cy_ * 3), y + (int)lroundf(-cy_ * 3 - sx * 3), col);
  if (i == fpSel) {
    tft.drawCircle(x, y, 9, C_TEXT);
    drawStr(fp[i].cs, x < FR_CX ? x + 12 : x - 12, y - 7, 1, x < FR_CX ? 0 : 2, C_TEXT, C_BG);
  }
  flPx[i] = (int16_t)x;
  flPy[i] = (int16_t)y;
  flDrawn[i] = true;
}

static void flFrame() {
  const uint32_t now = millis();
  if (flPrevAng >= 0) flSweep(flPrevAng, true);
  for (int i = 0; i < FL_MAX; i++)
    if (flDrawn[i]) {
      tft.fillCircle(flPx[i], flPy[i], 10, C_BG);
      if (i == fpSel) tft.fillRect(flPx[i] - 52, flPy[i] - 8, 104, 10, C_BG);
      flDrawn[i] = false;
    }
  flRings();
  for (int i = 0; i < fpN; i++) flPlaneDraw(i, now);
  flAng = (flAng + 6) % 360;
  flSweep(flAng, false);
  flPrevAng = flAng;
}

void flightFull() {
  gFlOpen = true;
  tft.fillScreen(C_BG);
  flSync();
  flSeen = flVer;
  for (int i = 0; i < FL_MAX; i++) flDrawn[i] = false;
  flPrevAng = -1;
  flTitle();
  flPanel();
  flFrame();
  flNextFrame = millis() + 80;
}

void flightTick() {
  gFlOpen = true;
  if (flVer != flSeen) {
    flSeen = flVer;
    flSync();
    flTitle();
    flPanel();
  }
  if ((int32_t)(millis() - flNextFrame) < 0) return;
  flNextFrame = millis() + 80;
  flFrame();
}

void flightStep(int d) {
  if (fpN < 2) return;
  fpSel = (fpSel + d + fpN) % fpN;
  strcpy(gFlWant, fp[fpSel].cs);
  flPanel();
}

// icona del menu: radar con un aereo
void iconFlight(int cx, int cy) {
  tft.drawCircle(cx, cy, 52, C_ACC);
  tft.drawCircle(cx, cy, 34, c565(mixRGB(th->bg, th->a1, 140)));
  tft.drawCircle(cx, cy, 16, c565(mixRGB(th->bg, th->a1, 110)));
  tft.drawFastHLine(cx - 52, cy, 105, c565(mixRGB(th->bg, th->a1, 90)));
  tft.drawFastVLine(cx, cy - 52, 105, c565(mixRGB(th->bg, th->a1, 90)));
  tft.fillTriangle(cx, cy, cx + 50, cy - 14, cx + 38, cy - 34, c565(mixRGB(th->bg, th->a1, 90)));   // raggio del radar
  tft.drawLine(cx, cy, cx + 50, cy - 14, C_ACC);
  // aereo visto dall'alto
  const uint16_t pc = RGB565(255, 230, 120);
  const int px = cx - 20, py = cy + 14;
  tft.fillRoundRect(px - 2, py - 14, 5, 28, 2, pc);
  tft.fillTriangle(px, py - 3, px - 17, py + 9, px + 17, py + 9, pc);
  tft.fillTriangle(px, py + 8, px - 8, py + 16, px + 8, py + 16, pc);
  tft.fillCircle(cx + 28, cy - 30, 3, RGB565(255, 86, 64));
}

// ------------------------------------------------------ Interfaccia web -----
void handleGesture(Gesture g);   // definita piu' sotto

bool otaOk = false, otaBegun = false, otaFirst = false;
String otaErr = "";

void otaScreen(const char* l1, const char* l2) {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  drawStr("Aggiornamento", 120, 60, 4, 1, C_ACC, C_BG);
  drawStr(l1, 120, 110, 4, 1, C_TEXT, C_BG);
  drawStr(l2, 120, 150, 2, 1, C_DIM, C_BG);
}

String minToStr(uint16_t m) {
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", m / 60, m % 60);
  return String(b);
}

String buildPage(bool ap) {
  String h;
  h.reserve(22000);
  h += R"HTML(<!DOCTYPE html><html lang='it'><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'><title>Small TV Evolution Pro</title><style>
:root{--bg:#05060f;--card:#0e1230;--a1:#00e5ff;--a2:#ff2e93;--dim:#8b95bd}
*{box-sizing:border-box}
body{font-family:system-ui,sans-serif;background:var(--bg);color:#fff;margin:0 auto;padding:16px 16px 96px;max-width:480px}
.lg{text-align:center;margin:2px 0 12px}.lg img{width:100%;max-width:300px;height:auto;mix-blend-mode:lighten}
label{display:block;margin:14px 0 4px;color:var(--dim);font-size:14px}
input,select{width:100%;padding:12px;border-radius:10px;border:1px solid #2a3470;background:#0b1030;color:#fff;font-size:16px}
input[type=range]{padding:0;height:30px}
button.main{margin-top:22px;width:100%;padding:14px;border:0;border-radius:10px;background:var(--a1);color:#001018;font-size:17px;font-weight:700}
button.danger{margin-top:14px;width:100%;padding:14px;border:1.5px solid #ff5d6c;border-radius:10px;background:transparent;color:#ff8791;font-size:16px;font-weight:700}
.c{display:flex;align-items:center;gap:10px;margin-top:14px}.c input{width:auto}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.coins{display:grid;grid-template-columns:1fr 1fr;gap:8px}.coins label{display:flex;margin:0;align-items:center;gap:8px;background:#0b1030;border:1px solid #2a3470;border-radius:10px;padding:9px;color:#fff;font-size:14px}.coins input{width:auto}
.thumbs{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:10px}.tb{position:relative;aspect-ratio:1}.tb img{width:100%;height:100%;object-fit:cover;border-radius:8px;display:block;background:#000}.tb button{position:absolute;top:2px;right:2px;width:24px;height:24px;border-radius:12px;border:0;background:rgba(0,0,0,.7);color:#fff;font-size:16px;line-height:1;padding:0}
.mini{width:auto;padding:8px 14px;border:1px solid #2a3470;border-radius:10px;background:#0b1030;color:#fff;font-size:14px;margin-right:8px}
small{color:#7d8db3;display:block;margin-top:6px}
.nav{position:sticky;top:0;z-index:5;background:var(--bg);padding:8px 0 10px;margin-bottom:6px}
.keys{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}
.keys button{height:58px;border-radius:14px;border:1.5px solid var(--a1);background:var(--card);color:var(--a1);display:flex;align-items:center;justify-content:center}
.keys button:nth-child(2){border-color:var(--a2);color:var(--a2)}
.keys button:active{background:#18204a}
.st{text-align:center;color:var(--dim);font-size:13px;margin-top:8px;min-height:18px}
.card{background:var(--card);border:1px solid #2a3470;border-radius:12px;padding:14px;margin-top:22px}
.card h2{margin:0 0 6px;font-size:17px;color:var(--a1)}
progress{width:100%;height:12px;margin-top:12px}
.bar{position:fixed;left:0;right:0;bottom:0;padding:12px 16px calc(12px + env(safe-area-inset-bottom));background:linear-gradient(0deg,var(--bg) 70%,transparent)}
.bar div{max-width:480px;margin:0 auto;display:grid;grid-template-columns:1.2fr 1fr;gap:10px}
.bar button{padding:14px;border-radius:12px;font-size:16px;font-weight:700;border:0}
.bar .p{background:var(--a1);color:#001018}.bar .s{background:#1a2250;color:#fff;border:1px solid #2a3470}
#toast{position:fixed;left:50%;bottom:92px;transform:translateX(-50%);background:#000d;border:1px solid #2a3470;padding:10px 18px;border-radius:99px;opacity:0;transition:.3s;pointer-events:none;white-space:nowrap}
#toast.show{opacity:1}
</style></head><body><div class='lg'><img src='/logo.jpg' alt='Small TV Evolution Pro'></div>)HTML";

  if (!ap) {
    h += R"HTML(<div class='nav'><div class='keys'>
<button type='button' onclick="k('prev')" aria-label='Indietro (doppio click)'><svg viewBox='0 0 24 24' width='30' height='30'><path d='M15 5l-7 7 7 7' fill='none' stroke='currentColor' stroke-width='2.6' stroke-linecap='round' stroke-linejoin='round'/></svg></button>
<button type='button' onclick="k('enter')" aria-label='Entra / Esci (pressione lunga)'><svg viewBox='0 0 24 24' width='30' height='30'><path d='M10 4H5v16h5M13 8l4 4-4 4M8 12h9' fill='none' stroke='currentColor' stroke-width='2.4' stroke-linecap='round' stroke-linejoin='round'/></svg></button>
<button type='button' onclick="k('next')" aria-label='Avanti (click)'><svg viewBox='0 0 24 24' width='30' height='30'><path d='M9 5l7 7-7 7' fill='none' stroke='currentColor' stroke-width='2.6' stroke-linecap='round' stroke-linejoin='round'/></svg></button>
</div><div class='st' id='st'>&nbsp;</div></div>)HTML";
  }

  h += "<form id='cfg' method='POST' action='/save'>";
  h += "<label>Rete WiFi</label>";
  if (ap && netOpts.length()) {
    h += "<select name='ssid'><option value=''>-- scegli una rete --</option>";
    h += netOpts;
    h += "</select><label>oppure scrivi il nome della rete</label>";
  }
  h += "<input name='ssid_manual' autocapitalize='off' placeholder='" + htmlEsc(cfg.ssid) + "'>";
  h += "<label>Password WiFi</label><input name='pass' type='password' placeholder='lascia vuoto per non cambiarla'>";
  h += "<label>Citt&agrave;</label><input name='cityq' value='" + htmlEsc(cfg.cityq) + "'>";
  h += "<small>Scrivi il nome (es. Roma, Chieti, Milano). Le coordinate si trovano da sole.</small>";
  h += "<label>Formato ora</label><select name='fmt'><option value='24'";
  if (!cfg.fmt12) h += " selected";
  h += ">24 ore</option><option value='12'";
  if (cfg.fmt12) h += " selected";
  h += ">12 ore (AM/PM)</option></select>";
  h += "<label>Tema colori</label><select name='theme'>";
  for (int i = 0; i < THEME_COUNT; i++) {
    h += "<option value='" + String(i) + "'";
    if (i == cfg.theme) h += " selected";
    h += ">" + String(THEME_NAME[i]) + "</option>";
  }
  h += "</select>";
  if (!ap) {
    h += "<label>Schermata all'avvio (e subito con Aggiorna)</label><select name='ss'><option value='0'";
    if (cfg.startSel == 0) h += " selected";
    h += ">Menu (predefinito)</option>";
    int n = 0;
    for (int m = 0; m < NMENU; m++)
      for (int f = 0; f < funcCount(m); f++) {
        n++;
        h += "<option value='" + String(n) + "'";
        if (cfg.startSel == n) h += " selected";
        h += ">" + String(menuName(m)) + " - " + funcName(m, f) + "</option>";
      }
    h += "</select><small>Scegli un orologio (o un'altra funzione): all'accensione il dispositivo entra direttamente l&igrave;. Premendo Aggiorna ci va subito.</small>";
  }

  if (!ap) {
    h += "<div class='card' style='margin-top:18px'><h2>Salvaschermo</h2><input type='hidden' name='ssf' value='1'>";
    h += "<div class='c' style='margin-top:6px'><input type='checkbox' name='sso' id='sso'";
    if (cfg.ssOn) h += " checked";
    h += "><label for='sso' style='margin:0;color:#fff'>Attiva il salvaschermo</label></div>";
    h += "<small>Dopo un po' di inattivit&agrave; il dispositivo mostra a giro le funzioni scelte qui sotto. Un tocco al pulsante lo ferma e torna dov'eri.</small>";
    h += "<label>Parte dopo <b id='ssmv' style='color:#fff'>" + String(cfg.ssMin) + " min</b> di inattivit&agrave;</label>";
    h += "<input type='range' name='ssm' id='ssm' min='1' max='60' step='1' value='" + String(cfg.ssMin) + "' oninput=\"$('ssmv').textContent=this.value+' min'\">";
    h += "<label>Secondi per ogni funzione <b id='sstv' style='color:#fff'>" + String(cfg.ssSec) + " s</b></label>";
    h += "<input type='range' name='sst' id='sst' min='1' max='60' step='1' value='" + String(cfg.ssSec) + "' oninput=\"$('sstv').textContent=this.value+' s'\">";
    h += "<small>Se scegli una sola funzione resta quella; se ne scegli pi&ugrave; di una vanno a giro.</small>";
    h += "<label style='margin-top:10px'>Funzioni da mostrare</label><div class='coins'>";
    int sn = 0;
    for (int m = 0; m < NMENU; m++)
      for (int f = 0; f < funcCount(m); f++) {
        h += "<label><input type='checkbox' name='sf" + String(sn) + "'";
        if ((cfg.ssMask >> sn) & 1u) h += " checked";
        h += ">" + String(menuName(m)) + (funcCount(m) > 1 ? String(" - ") + funcName(m, f) : String("")) + "</label>";
        sn++;
      }
    h += "</div></div>";
  }

  if (!ap) {
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Cripto</label>";
    h += "<small style='margin:0 0 8px'>Scegli le criptovalute da mostrare a giro nella schermata Cripto (prezzi in USDT).</small><div class='coins'>";
    for (int i = 0; i < CR_COUNT; i++) {
      h += "<label><input type='checkbox' name='c" + String(i) + "'";
      if ((cfg.cMask >> i) & 1u) h += " checked";
      h += ">" + String(COINS[i].sym) + " <span style='color:#7d8db3;font-size:12px'>" + COINS[i].name + "</span></label>";
    }
    h += "</div><div style='margin-top:10px'><button type='button' class='mini' onclick='ca(true)'>Tutte</button><button type='button' class='mini' onclick='ca(false)'>Nessuna</button></div>";
    h += "<label>Secondi per ogni cripto <b id='csv' style='color:#fff'>" + String(cfg.cSec) + " s</b></label>";
    h += "<input type='range' name='cs' id='cs' min='1' max='60' step='1' value='" + String(cfg.cSec) + "' oninput=\"$('csv').textContent=this.value+' s'\">";
    h += "<small>Col tocco avanti/indietro cambi cripto subito, senza aspettare.</small>";
    h += "<label>Intervallo delle candele</label><select name='ci'>";
    static const char* const CI_LAB[4] = {"5 minuti", "15 minuti", "1 ora", "4 ore"};
    for (int i = 0; i < 4; i++) {
      h += "<option value='" + String(i) + "'";
      if (i == cfg.cInt) h += " selected";
      h += String(">") + CI_LAB[i] + "</option>";
    }
    h += "</select>";
  }

  if (!ap) {
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Foto</label>";
    h += "<label>Secondi per ogni foto <b id='phsv' style='color:#fff'>" + String(cfg.pSec) + " s</b></label>";
    h += "<input type='range' name='phs' id='phs' min='1' max='60' step='1' value='" + String(cfg.pSec) + "' oninput=\"$('phsv').textContent=this.value+' s'\">";
    h += "<small>Col tocco avanti/indietro cambi foto subito, senza aspettare.</small>";
    h += "<label>Come vengono caricate le foto</label><select name='phf' id='phf'><option value='0'";
    if (cfg.pFit == 0) h += " selected";
    h += ">Adattate automaticamente (riempiono lo schermo)</option><option value='1'";
    if (cfg.pFit == 1) h += " selected";
    h += ">Intere, con bordi neri</option></select><small>Vale per le foto che carichi da ora in poi: quelle gi&agrave; caricate restano come sono. Il caricamento &egrave; nella sezione &laquo;Foto caricate&raquo; qui sotto.</small>";
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Pomodoro (minuti)</label><div class='row' style='grid-template-columns:1fr 1fr 1fr'>";
    h += "<div><label for='pw'>Lavoro</label><input type='number' name='pw' id='pw' min='1' max='99' value='" + String(cfg.pomW) + "'></div>";
    h += "<div><label for='pb'>Pausa</label><input type='number' name='pb' id='pb' min='1' max='99' value='" + String(cfg.pomB) + "'></div>";
    h += "<div><label for='pl'>Lunga</label><input type='number' name='pl' id='pl' min='1' max='99' value='" + String(cfg.pomL) + "'></div></div>";
    h += "<small>Dopo 4 pomodori di lavoro arriva la pausa lunga.</small>";
    h += "<label>Obiettivo giornaliero (pomodori al giorno)</label><input type='number' name='xpg' min='1' max='12' inputmode='numeric' value='" + String(cfg.xpGoal) + "'>";
    h += "<small>Ogni pomodoro completato vale 25 XP, l'obiettivo del giorno 50 XP in pi&ugrave;. Livelli, badge e statistiche sono nella seconda funzione del Pomodoro (a timer fermo: doppio tocco).</small>";
    h += "<button type='button' class='mini' style='margin-top:10px' onclick='xpr()'>Azzera XP e statistiche</button>";
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Timer</label><input type='hidden' name='tmf' value='1'>";
    h += "<div class='row' style='grid-template-columns:1fr 1fr 1fr'>";
    h += "<div><label>Ore</label><input type='number' name='tmh' min='0' max='23' inputmode='numeric' value='" + String(cfg.tmH) + "'></div>";
    h += "<div><label>Minuti</label><input type='number' name='tmm' min='0' max='59' inputmode='numeric' value='" + String(cfg.tmM) + "'></div>";
    h += "<div><label>Secondi</label><input type='number' name='tms' min='0' max='59' inputmode='numeric' value='" + String(cfg.tmS) + "'></div></div>";
    h += "<small>Tempo da cui parte il conto alla rovescia della schermata Timer (predefinito 0 ore, 10 minuti, 0 secondi).</small>";
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Bibi AI</label><input type='hidden' name='aif' value='1'>";
    h += "<small style='margin:0 0 8px'>L'intelligenza artificiale gira su un servizio esterno: il dispositivo manda solo la domanda con meteo, aria e cripto. Per scrivere a Bibi usa la chat qui sotto.</small>";
    h += "<label>Servizio AI</label><select name='aip'><option value='1'";
    if (cfg.aiProv == 1) h += " selected";
    h += ">Compatibile OpenAI (indirizzo a scelta)</option><option value='0'";
    if (cfg.aiProv == 0) h += " selected";
    h += ">Claude (Anthropic)</option></select>";
    h += "<label>Indirizzo del servizio (solo compatibili OpenAI, deve iniziare con https)</label><input name='aiu' autocapitalize='off' value='" + htmlEsc(cfg.aiUrl) + "'>";
    h += "<label>Modello</label><input name='aim' autocapitalize='off' value='" + htmlEsc(cfg.aiModel) + "'>";
    h += "<label>Chiave API</label><input name='aik' type='password' autocomplete='off' placeholder='" + String(cfg.aiKey.length() ? "chiave salvata: lascia vuoto per non cambiarla" : "incolla qui la chiave (se serve)") + "'>";
    h += "<small>Con Claude la chiave &egrave; obbligatoria (modello predefinito claude-haiku-5-5). L'indirizzo predefinito &egrave; un servizio gratuito senza chiave: puoi sostituirlo con un altro compatibile OpenAI.</small>";
    h += "<div class='c'><input type='checkbox' name='aid' id='aid'";
    if (cfg.aiDaily) h += " checked";
    h += "><label for='aid' style='margin:0;color:#fff'>Una frase nuova ogni giorno</label></div>";
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Radar voli</label><select name='flk'>";
    static const uint8_t FLK[5] = {25, 50, 100, 150, 200};
    for (int i = 0; i < 5; i++) {
      h += "<option value='" + String(FLK[i]) + "'";
      if (cfg.flKm == FLK[i]) h += " selected";
      h += ">Raggio " + String(FLK[i]) + " km</option>";
    }
    h += "</select><small>Mostra gli aerei in volo vicino alla citt&agrave; impostata (dati ADS-B aperti, nessuna chiave).</small>";
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Orologi nel mondo (Quadranti)</label>";
    for (int i = 0; i < 4; i++) {
      h += "<select name='wc" + String(i) + "' style='margin-top:6px'>";
      for (int c = 0; c < WC_N; c++) {
        h += "<option value='" + String(c) + "'";
        if (wcIdx(i) == c) h += " selected";
        h += ">" + String(WC_CITY[c].name) + "</option>";
      }
      h += "</select>";
    }
    h += "<small>Le quattro citt&agrave; del quinto quadrante. L'ora legale &egrave; calcolata in automatico.</small>";
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Countdown (fino a 3 eventi)</label><input type='hidden' name='cdf' value='1'>";
    for (int i = 0; i < 3; i++) {
      char ds[12] = "";
      if (cfg.cd[i].m) snprintf(ds, sizeof ds, "%04u-%02u-%02u", (unsigned)cfg.cd[i].y, (unsigned)cfg.cd[i].m, (unsigned)cfg.cd[i].d);
      const String n = String(i);
      h += "<div class='row' style='grid-template-columns:1.3fr 1fr;margin-top:8px'><input type='text' name='c" + n + "n' maxlength='16' placeholder='Nome evento' value='" + htmlEsc(cfg.cd[i].name) + "'>";
      h += "<input type='date' name='c" + n + "d' value='" + String(ds) + "'></div>";
      h += "<div class='c' style='margin-top:4px'><input type='checkbox' name='c" + n + "y' id='c" + n + "y' value='1'";
      if (cfg.cd[i].yearly) h += " checked";
      h += "><label for='c" + n + "y' style='margin:0;color:#fff'>Si ripete ogni anno (compleanni, Natale...)</label></div>";
    }
    h += "<small>Un evento senza data o gi&agrave; passato non viene mostrato. Col tocco avanti/indietro passi da un evento all'altro.</small>";
    h += "<label style='margin-top:22px;color:var(--a1);font-size:16px'>Post-it (fino a 3 note)</label>";
    for (int i = 0; i < 3; i++) {
      const String n = String(i);
      h += "<input type='text' name='t" + n + "' maxlength='40' style='margin-top:8px' placeholder='Testo della nota (max 40 caratteri)' value='" + htmlEsc(cfg.nt[i].text) + "'>";
      h += "<div class='c' style='margin-top:4px'><input type='checkbox' name='t" + n + "o' id='t" + n + "o' value='1'";
      if (cfg.nt[i].on) h += " checked";
      h += "><label for='t" + n + "o' style='margin:0;color:#fff'>Nota " + String(i + 1) + " attiva</label></div>";
    }
    h += "<small>Emoticon riconosciute: :) ;) :D :( :P :O &lt;3 8) :'( ^_^ &mdash; diventano faccine disegnate. Le lettere accentate sono scritte con l'apostrofo (e').</small>";
  }

  h += "<label>Luminosit&agrave; <b id='brv' style='color:#fff'>" + String(cfg.bright) + "%</b></label>";
  h += "<input type='range' name='br' id='br' min='5' max='100' step='5' value='" + String(cfg.bright) + "' oninput=\"sv('br','brv')\">";

  h += "<div class='card' style='margin-top:18px'><h2>Modalit&agrave; notturna</h2>";
  h += "<div class='c' style='margin-top:6px'><input type='checkbox' name='nm' id='nm'";
  if (cfg.nightOn) h += " checked";
  h += "><label for='nm' style='margin:0;color:#fff'>Abbassa la luminosit&agrave; di notte</label></div>";
  h += "<small>nella fascia oraria scelta qui sotto</small>";
  h += "<div class='row'><div><label for='ns'>Dalle</label><input type='time' name='ns' id='ns' value='" + minToStr(cfg.nStart) + "'></div>";
  h += "<div><label for='ne'>Alle</label><input type='time' name='ne' id='ne' value='" + minToStr(cfg.nEnd) + "'></div></div>";
  h += "<label>Luminosit&agrave; notturna <b id='nbv' style='color:#fff'>" + String(cfg.nBright) + "%</b></label>";
  h += "<input type='range' name='nb' id='nb' min='0' max='100' step='1' value='" + String(cfg.nBright) + "' oninput=\"sv('nb','nbv')\">";
  h += "<small>0% = schermo spento durante la notte</small></div>";

  h += "<div class='c'><input type='checkbox' name='inv' id='inv'";
  if (cfg.inv) h += " checked";
  h += "><label for='inv' style='margin:0'>Inverti i colori (se lo schermo sembra un negativo)</label></div>";
  h += "<div class='c'><input type='checkbox' name='rot' id='rot'";
  if (cfg.rot) h += " checked";
  h += "><label for='rot' style='margin:0'>Ruota lo schermo di 180&deg;</label></div>";
  if (ap) h += "<button class='main' type='submit'>Salva e riavvia</button>";
  h += "</form>";

  if (!ap) {
    h += R"HTML(<div class='card'><h2>Chat con Bibi</h2>
<small style='margin-top:0'>Scrivi a Bibi: sul display si mette a pensare e poi risponde con la sua espressione. Ricorda gli ultimi scambi.</small>
<div id='bchat' style='max-height:280px;overflow:auto;margin-top:10px'></div>
<div class='row' style='grid-template-columns:1fr auto;margin-top:8px'><input id='bq' maxlength='160' placeholder='Bibi, come sar&agrave; il tempo domani?' onkeydown="if(event.key==='Enter')bsend()"><button type='button' class='mini' style='margin:0' onclick='bsend()'>Invia</button></div>
<div class='st' id='bst'>&nbsp;</div>
<button type='button' class='mini' onclick='bclear()'>Cancella la memoria</button><button type='button' class='mini' onclick='bdaily()'>Frase del giorno</button></div>
<div class='card'><h2>Foto caricate <span id='phn' style='color:var(--dim);font-size:14px'></span></h2>
<small style='margin-top:0'>Le foto vengono portate a 240&times;240 e compresse qui nel browser (8-15 KB ciascuna). Massimo 20.</small>
<div class='thumbs' id='thumbs'></div>
<input type='file' id='phfile' accept='image/*' multiple style='margin-top:12px'>
<button class='main' type='button' id='phadd' onclick='addPh()'>Carica le foto</button>
<div class='st' id='phm'>&nbsp;</div><small id='phfree'></small></div>)HTML";
    h += R"HTML(<div class='card'><h2>Aggiornamento firmware (OTA)</h2>
<small style='margin-top:0'>Scegli il file <b>nmtv-firmware-app.bin</b> (non quello &laquo;completo&raquo;) e premi Aggiorna. Il dispositivo si riavvia da solo.</small>
<input type='file' id='fw' accept='.bin' style='margin-top:12px'>
<button class='main' type='button' onclick='ota()'>Aggiorna firmware</button>
<progress id='pg' value='0' max='100'></progress><div class='st' id='om'>&nbsp;</div></div>
<div class='card' style='border-color:#6b2a35'><h2 style='color:#ff8791'>Ripristino di fabbrica</h2>
<small style='margin-top:0'>Cancella tutte le impostazioni e la rete WiFi: il dispositivo riparte dalla configurazione iniziale (rete NM-TV-Setup).</small>
<button class='danger' type='button' onclick='fact()'>Ripristina le impostazioni di fabbrica</button></div>)HTML";
  }
  h += "<small style='margin-top:18px'>Firmware ";
  h += FW_VERSION;
  h += "</small><div id='toast'></div>";

  if (!ap) {
    h += R"HTML(<div class='bar'><div><button class='p' id='ba' type='button' onclick='apply()'>Aggiorna</button><button class='s' type='button' onclick='rst()'>Riavvia</button></div></div>)HTML";
  }

  h += R"HTML(<script>
function $(i){return document.getElementById(i)}
function sv(i,o){$(o).textContent=$(i).value+'%'}
function toast(t){var e=$('toast');e.textContent=t;e.className='show';setTimeout(function(){e.className=''},2400)}
</script>)HTML";

  if (!ap) {
    h += R"HTML(<script>
function k(b){fetch('/tasto?b='+b).then(function(r){return r.text()}).then(function(t){$('st').textContent=t}).catch(function(){})}
function s(){fetch('/stato').then(function(r){return r.text()}).then(function(t){$('st').textContent=t}).catch(function(){})}
setInterval(s,3000);s();
function apply(){var b=$('ba');b.disabled=true;
fetch('/apply',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData($('cfg'))).toString()})
.then(function(r){return r.text()}).then(function(t){toast(t=='OK'?'Aggiornato sul dispositivo':t);b.disabled=false;s()})
.catch(function(){toast('Connessione interrotta');b.disabled=false})}
function rst(){if(confirm('Riavviare il dispositivo?\n\nLe modifiche non applicate con Aggiorna andranno perse.')){
fetch('/riavvia',{method:'POST'}).catch(function(){});document.body.innerHTML='<h2 style="font-family:system-ui;padding:30px;color:#e8eefc">Riavvio in corso</h2><p style="font-family:system-ui;padding:0 30px;color:#8b95bd">Ricarica la pagina tra qualche secondo.</p>'}}
var PHN=0,PHMAX=20;
function phm(t){$('phm').textContent=t}
function phLoad(){fetch('/foto/elenco',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){
PHN=j.photos.length;PHMAX=j.max;var b=$('thumbs');b.innerHTML='';
j.photos.forEach(function(p){var d=document.createElement('div');d.className='tb';var im=document.createElement('img');im.src='/foto/img?i='+p.i+'&v='+j.rev;d.appendChild(im);
var x=document.createElement('button');x.type='button';x.textContent='×';x.onclick=function(){delPh(p.i)};d.appendChild(x);b.appendChild(d)});
$('phn').textContent=PHN+' / '+PHMAX;
$('phfree').textContent=(j.fs?('Spazio libero: '+Math.round(j.free/1024)+' KB'):'Memoria foto non disponibile')+((j.fs&&j.total<400000)?' - memoria ridotta: per 20 foto flasha una volta via cavo il file completo':'');
$('phadd').disabled=PHN>=PHMAX||!j.fs}).catch(function(){phm('Impossibile leggere le foto')})}
function toJpg(file,fill){return new Promise(function(ok,ko){var u=URL.createObjectURL(file),im=new Image();
im.onload=function(){URL.revokeObjectURL(u);var sw=im.naturalWidth,sh=im.naturalHeight;if(!sw||!sh){ko(new Error('immagine vuota'));return}
var s=fill?Math.max(240/sw,240/sh):Math.min(240/sw,240/sh),w=Math.round(sw*s),h=Math.round(sh*s);
var cur=im,cw=sw,ch=sh;
while(cw/2>=w&&ch/2>=h){var t=document.createElement('canvas');t.width=Math.ceil(cw/2);t.height=Math.ceil(ch/2);var g=t.getContext('2d');g.imageSmoothingQuality='high';g.drawImage(cur,0,0,t.width,t.height);cur=t;cw=t.width;ch=t.height}
var c=document.createElement('canvas');c.width=c.height=240;var g2=c.getContext('2d');g2.fillStyle='#000';g2.fillRect(0,0,240,240);g2.imageSmoothingQuality='high';g2.drawImage(cur,(240-w)/2,(240-h)/2,w,h);
var qs=[.92,.85,.78,.7,.62,.55,.48,.4,.33,.27],i=0,best=null;
(function next(){if(i>=qs.length){if(best&&best.size<=36000)ok(best);else ko(new Error('foto troppo complessa'));return}
c.toBlob(function(b){if(b&&b.size<=15000){ok(b);return}if(b)best=b;i++;next()},'image/jpeg',qs[i])})()};
im.onerror=function(){URL.revokeObjectURL(u);ko(new Error('immagine non leggibile'))};im.src=u})}
function addPh(){var fs=Array.prototype.slice.call($('phfile').files);if(!fs.length){phm('Scegli prima le foto');return}
var fill=$('phf').value!=='1',b=$('phadd');b.disabled=true;var n=0;
(function one(i){if(i>=fs.length||PHN+n>=PHMAX){b.disabled=false;$('phfile').value='';phm(n?(n+(n>1?' foto aggiunte':' foto aggiunta')):'Nessuna foto aggiunta');phLoad();return}
phm('Preparo '+fs[i].name+' ('+(i+1)+'/'+fs.length+')...');
toJpg(fs[i],fill).then(function(bl){phm('Invio '+fs[i].name+' ('+Math.round(bl.size/1024)+' KB)...');var fd=new FormData();fd.append('photo',bl,'photo.jpg');return fetch('/foto',{method:'POST',body:fd})})
.then(function(r){return r.json()}).then(function(j){if(!j.ok)throw new Error(j.err||'errore');n++;one(i+1)})
.catch(function(e){b.disabled=false;phm('Errore con '+fs[i].name+': '+e.message);phLoad()})})(0)}
function delPh(i){if(!confirm('Eliminare questa foto?'))return;fetch('/foto/del?i='+i,{method:'POST'}).then(phLoad).catch(phLoad)}
phLoad();
function bload(){fetch('/bibi/chat',{cache:'no-store'}).then(function(r){return r.json()}).then(function(j){var b=$('bchat');b.innerHTML='';
function add(u,t,m){var d=document.createElement('div');d.style.cssText='margin:6px 0;padding:8px 10px;border-radius:10px;background:'+(u?'#18204a':'#0b1030')+';text-align:'+(u?'right':'left');d.textContent=(u?'':'Bibi'+(m?' ('+m+')':'')+': ')+t;b.appendChild(d)}
if(j.daily&&!j.log.length)add(false,j.daily,'frase del giorno');
j.log.forEach(function(x){add(x.u,x.t,x.m)});if(j.busy&&j.pending)add(true,j.pending);b.scrollTop=b.scrollHeight;
$('bst').textContent=j.busy?'Bibi sta pensando...':(j.err||'\u00a0');if(j.busy)setTimeout(bload,1500)}).catch(function(){})}
function bsend(){var q=$('bq').value.trim();if(!q)return;$('bq').value='';
fetch('/bibi/ask',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'q='+encodeURIComponent(q)}).then(function(r){return r.text()}).then(function(t){if(t!='OK')toast(t);bload()}).catch(function(){toast('Connessione interrotta')})}
function bdaily(){fetch('/bibi/ask',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'q='+encodeURIComponent('Dimmi la tua frase del giorno.')}).then(function(r){return r.text()}).then(function(t){if(t!='OK')toast(t);bload()})}
function bclear(){if(confirm('Cancellare la memoria della conversazione?'))fetch('/bibi/clear',{method:'POST'}).then(bload)}
function xpr(){if(confirm('Azzerare XP, livelli, badge e statistiche del Pomodoro?'))fetch('/xp/reset',{method:'POST'}).then(function(){toast('XP azzerati')})}
bload();
function ca(v){document.querySelectorAll('.coins input').forEach(function(e){e.checked=v})}
function fact(){if(confirm('Ripristinare le impostazioni di fabbrica?\n\nVerranno cancellate tutte le impostazioni e la rete WiFi: il dispositivo ripartira dalla configurazione iniziale (rete NM-TV-Setup).')){
fetch('/factory',{method:'POST'}).then(function(){document.body.innerHTML='<h2 style="font-family:system-ui;padding:30px;color:#e8eefc">Ripristino in corso</h2><p style="font-family:system-ui;padding:0 30px;color:#8b95bd">Il dispositivo si riavvia. Tra qualche secondo collegati alla rete NM-TV-Setup e apri 192.168.4.1 per configurarlo.</p>'}).catch(function(){})}}
function ota(){var f=$('fw').files[0];if(!f){alert('Scegli prima il file .bin');return}
var fd=new FormData();fd.append('firmware',f,f.name);var x=new XMLHttpRequest();x.open('POST','/update');
x.upload.onprogress=function(e){if(e.lengthComputable){$('pg').value=e.loaded/e.total*100}};
x.onload=function(){$('om').textContent=(x.responseText=='OK')?'Aggiornamento completato: il dispositivo si riavvia...':'Errore: '+x.responseText};
x.onerror=function(){$('om').textContent='Connessione interrotta (se la barra era al 100% il dispositivo si sta riavviando)'};
$('om').textContent='Caricamento in corso, non scollegare il dispositivo...';x.send(fd)}
</script>)HTML";
  }
  h += "</body></html>";
  return h;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", buildPage(WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA));
}

// "HH:MM" <-> minuti dalla mezzanotte
bool strToMin(const String& s, uint16_t& out) {
  if (s.length() < 5 || s[2] != ':') return false;
  int hh = s.substring(0, 2).toInt(), mm = s.substring(3, 5).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  out = (uint16_t)(hh * 60 + mm);
  return true;
}

struct Changes {
  bool wifi = false, city = false;
};

// legge i campi del modulo di configurazione (usato da "Salva e riavvia" e da "Aggiorna")
Changes readArgs() {
  Changes ch;
  String ssid = server.arg("ssid_manual");
  ssid.trim();
  if (!ssid.length()) {
    ssid = server.arg("ssid");
    ssid.trim();
  }
  String pass = server.arg("pass");
  if (ssid.length() && ssid != cfg.ssid) {
    cfg.ssid = ssid;
    cfg.pass = pass;
    ch.wifi = true;
  } else if (pass.length()) {
    cfg.pass = pass;
    ch.wifi = true;
  }
  String c = server.arg("cityq");
  c.trim();
  if (c.length() && c != cfg.cityq) {
    cfg.cityq = c;
    cfg.geo = false;
    ch.city = true;
  }
  cfg.fmt12 = server.arg("fmt") == "12";
  int t = server.arg("theme").toInt();
  cfg.theme = (t >= 0 && t < THEME_COUNT) ? (uint8_t)t : 0;
  cfg.inv = server.hasArg("inv");
  cfg.rot = server.hasArg("rot");
  if (server.hasArg("br")) {
    int b = server.arg("br").toInt();
    cfg.bright = (uint8_t)(b < 5 ? 5 : (b > 100 ? 100 : b));
  }
  if (server.hasArg("ss")) {
    int v = server.arg("ss").toInt();
    cfg.startSel = (uint8_t)((v < 0 || v > selTotal()) ? 0 : v);
  }
  if (server.hasArg("cs")) {                 // sezione Cripto presente nel modulo
    int v = server.arg("cs").toInt();
    cfg.cSec = (uint8_t)(v < 1 ? 1 : (v > 60 ? 60 : v));
    int ci = server.arg("ci").toInt();
    if (ci >= 0 && ci <= 3) cfg.cInt = (uint8_t)ci;
    uint32_t m = 0;
    for (int i = 0; i < CR_COUNT; i++)
      if (server.hasArg("c" + String(i))) m |= (1u << i);
    cfg.cMask = m ? m : 1u;                  // almeno una cripto
  }
  if (server.hasArg("phs")) {                // sezioni Foto e Pomodoro presenti nel modulo
    int v = server.arg("phs").toInt();
    cfg.pSec = (uint8_t)(v < 1 ? 1 : (v > 60 ? 60 : v));
    cfg.pFit = server.arg("phf") == "1" ? 1 : 0;
    int w = server.arg("pw").toInt(), b = server.arg("pb").toInt(), l = server.arg("pl").toInt();
    cfg.pomW = (uint8_t)(w < 1 ? 1 : (w > 99 ? 99 : w));
    cfg.pomB = (uint8_t)(b < 1 ? 1 : (b > 99 ? 99 : b));
    cfg.pomL = (uint8_t)(l < 1 ? 1 : (l > 99 ? 99 : l));
  }
  if (server.hasArg("tmf")) {                // sezione Timer presente nel modulo
    int th_ = server.arg("tmh").toInt(), tm_ = server.arg("tmm").toInt(), ts_ = server.arg("tms").toInt();
    th_ = th_ < 0 ? 0 : (th_ > 23 ? 23 : th_);
    tm_ = tm_ < 0 ? 0 : (tm_ > 59 ? 59 : tm_);
    ts_ = ts_ < 0 ? 0 : (ts_ > 59 ? 59 : ts_);
    if (th_ + tm_ + ts_ == 0) { tm_ = 10; }  // zero non ha senso: torna al predefinito
    cfg.tmH = (uint8_t)th_;
    cfg.tmM = (uint8_t)tm_;
    cfg.tmS = (uint8_t)ts_;
  }
  if (server.hasArg("xpg")) {
    int g = server.arg("xpg").toInt();
    cfg.xpGoal = (uint8_t)(g < 1 ? 1 : (g > 12 ? 12 : g));
  }
  if (server.hasArg("aif")) {                // sezioni Bibi AI, Radar voli e Orologi nel mondo presenti nel modulo
    const uint8_t oldProv = cfg.aiProv;
    cfg.aiProv = server.arg("aip").toInt() == 0 ? 0 : 1;
    String u = server.arg("aiu"), m = server.arg("aim"), k = server.arg("aik");
    u.trim();
    m.trim();
    k.trim();
    cfg.aiUrl = (u.length() && u.length() < 200) ? u : String("https://text.pollinations.ai/openai");
    cfg.aiModel = m.length() < 70 ? m : m.substring(0, 70);
    if (k.length() && k.length() < 200) cfg.aiKey = k;
    if (cfg.aiProv == 0 && (cfg.aiModel.length() == 0 || cfg.aiModel == "openai")) cfg.aiModel = "claude-haiku-5-5";
    if (cfg.aiProv == 1 && oldProv == 0 && cfg.aiModel.startsWith("claude")) cfg.aiModel = "openai";
    cfg.aiDaily = server.hasArg("aid");
    const int fk = server.arg("flk").toInt();
    cfg.flKm = (uint8_t)((fk >= 10 && fk <= 250) ? fk : 50);
    for (int i = 0; i < 4; i++) {
      const int v = server.arg("wc" + String(i)).toInt();
      cfg.wc[i] = (uint8_t)((v >= 0 && v < WC_N) ? v : 255);
    }
  }
  if (server.hasArg("cdf")) {                // sezioni Countdown e Post-it presenti nel modulo
    for (int i = 0; i < 3; i++) {
      const String n = String(i);
      noteCopy(cfg.cd[i].name, sizeof cfg.cd[i].name, server.arg("c" + n + "n").c_str());
      cdSetDate(cfg.cd[i], server.arg("c" + n + "d"));
      cfg.cd[i].yearly = server.hasArg("c" + n + "y");
      noteCopy(cfg.nt[i].text, sizeof cfg.nt[i].text, server.arg("t" + n).c_str());
      cfg.nt[i].on = server.hasArg("t" + n + "o");
    }
  }
  if (server.hasArg("ssf")) {                // sezione Salvaschermo presente nel modulo
    cfg.ssOn = server.hasArg("sso");
    int mn = server.arg("ssm").toInt(), sc = server.arg("sst").toInt();
    cfg.ssMin = (uint8_t)(mn < 1 ? 1 : (mn > 60 ? 60 : mn));
    cfg.ssSec = (uint8_t)(sc < 1 ? 1 : (sc > 60 ? 60 : sc));
    uint32_t mk = 0;
    for (int i = 0; i < selTotal() && i < 32; i++)
      if (server.hasArg("sf" + String(i))) mk |= (1u << i);
    cfg.ssMask = mk ? mk : 1u;               // almeno una funzione
  }
  cfg.nightOn = server.hasArg("nm");
  uint16_t m;
  if (strToMin(server.arg("ns"), m)) cfg.nStart = m;
  if (strToMin(server.arg("ne"), m)) cfg.nEnd = m;
  if (server.hasArg("nb")) {
    int b = server.arg("nb").toInt();
    cfg.nBright = (uint8_t)(b < 0 ? 0 : (b > 100 ? 100 : b));
  }
  return ch;
}

void handleSave() {
  readArgs();
  saveCfg();
  server.send(200, "text/html; charset=utf-8",
              String("<!DOCTYPE html><html lang='it'><head><meta charset='utf-8'>"
                     "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
                     "<body style='font-family:system-ui;background:#05060f;color:#e8eefc;padding:30px'>"
                     "<h2>Salvato!</h2><p>Il dispositivo si riavvia...</p></body></html>"));
  delay(1500);
  ESP.restart();
}

uint32_t wifiReconnectAt = 0;

// "Aggiorna": salva e applica subito le opzioni, senza riavviare
void handleApply() {
  if (inSplash || gPortal) {
    server.send(200, "text/plain; charset=utf-8", "Attendi la fine dell'avvio");
    return;
  }
  const uint8_t oldSel = cfg.startSel;
  Changes ch = readArgs();
  saveCfg();
  if (poState == 0) poReset();               // timer fermo: ricarica i tempi nuovi
  if (tmState == 0) tmReset();
  int sm, sf;
  if (cfg.startSel != oldSel && selToMF(cfg.startSel, sm, sf)) {   // scelta una funzione: ci va subito
    menuIdx = sm;
    funcIdx = sf;
    inFunc = true;
  }
  applyTheme(cfg.theme);
  tft.setRotation(cfg.rot ? 2 : 0);
  tft.invertDisplay(cfg.inv);
  applyBrightness(true);
  if (ch.wifi) wifiReconnectAt = millis() + 600;   // dopo aver risposto al browser
  needFull = true;
  server.send(200, "text/plain; charset=utf-8", "OK");
}

// ---- foto: elenco, anteprima, caricamento, eliminazione ----
static File upFile;
static int upSlot = -1;
static size_t upBytes = 0;
static bool upOk = false;
static const char* upErr = "errore sconosciuto";

static void upAbort(const char* why) {
  if (upFile) upFile.close();
  if (upSlot >= 0) {
    char p[16];
    phPath(p, sizeof p, upSlot);
    LittleFS.remove(p);
  }
  upSlot = -1;
  upOk = false;
  upErr = why;
}

// riceve la foto a blocchi (multipart) e la scrive direttamente nella memoria
void handlePhotoUpload() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    upOk = false;
    upErr = "errore sconosciuto";
    upBytes = 0;
    upSlot = -1;
    if (upFile) upFile.close();
    if (!fsOk) {
      upErr = "memoria foto non disponibile";
      return;
    }
    char p[16];
    for (int i = 0; i < PH_MAX; i++) {
      phPath(p, sizeof p, i);
      if (!LittleFS.exists(p)) {
        upSlot = i;
        break;
      }
    }
    if (upSlot < 0) {
      upErr = "limite di 20 foto raggiunto";
      return;
    }
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < 24UL * 1024UL) {
      upSlot = -1;
      upErr = "memoria del dispositivo piena";
      return;
    }
    phPath(p, sizeof p, upSlot);
    upFile = LittleFS.open(p, "w");
    if (!upFile) {
      upSlot = -1;
      upErr = "scrittura non riuscita";
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!upFile) return;
    if (upBytes == 0 && (u.currentSize < 2 || u.buf[0] != 0xFF || u.buf[1] != 0xD8)) {
      upAbort("formato non valido");
      return;
    }
    if (upBytes + u.currentSize > PH_MAX_BYTES) {
      upAbort("foto troppo grande");
      return;
    }
    if (upFile.write(u.buf, u.currentSize) != u.currentSize) {
      upAbort("memoria del dispositivo piena");
      return;
    }
    upBytes += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END) {
    if (!upFile) return;
    upFile.close();
    if (upBytes < 200) {
      upAbort("file non valido");
      return;
    }
    upOk = true;
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    upAbort("invio interrotto");
  }
}

void handlePhotoDone() {
  server.sendHeader("Cache-Control", "no-store");
  if (upOk) {
    phScan();
    phRev++;
    char b[40];
    snprintf(b, sizeof b, "{\"ok\":true,\"i\":%d}", upSlot);
    server.send(200, "application/json", b);
  } else {
    server.send(400, "application/json", String("{\"ok\":false,\"err\":\"") + upErr + "\"}");
  }
  upOk = false;
  upSlot = -1;
}

void handlePhotoList() {
  String s;
  s.reserve(120 + phN * 24);
  s += "{\"max\":";
  s += PH_MAX;
  s += ",\"rev\":";
  s += (unsigned long)phRev;
  s += ",\"fs\":";
  s += fsOk ? "true" : "false";
  s += ",\"free\":";
  s += fsOk ? (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()) : 0UL;
  s += ",\"total\":";
  s += fsOk ? (unsigned long)LittleFS.totalBytes() : 0UL;
  s += ",\"photos\":[";
  for (int k = 0; k < phN; k++) {
    if (k) s += ',';
    s += "{\"i\":";
    s += (int)phSlots[k];
    s += '}';
  }
  s += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handlePhotoImg() {
  const int i = server.arg("i").toInt();
  char p[16];
  phPath(p, sizeof p, i);
  if (!fsOk || i < 0 || i >= PH_MAX || !LittleFS.exists(p)) {
    server.send(404, "text/plain", "non trovata");
    return;
  }
  File f = LittleFS.open(p, "r");
  server.sendHeader("Cache-Control", "max-age=86400");
  server.streamFile(f, "image/jpeg");
  f.close();
}

void handlePhotoDel() {
  const int i = server.arg("i").toInt();
  if (fsOk && i >= 0 && i < PH_MAX) {
    char p[16];
    phPath(p, sizeof p, i);
    LittleFS.remove(p);
    phScan();
    phRev++;
  }
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleBibiChat() {
  JsonDocument d;
  JsonArray log = d["log"].to<JsonArray>();
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < aiHistN; i++) {
    JsonObject o = log.add<JsonObject>();
    o["u"] = aiHist[i].user;
    if (aiHist[i].user) {
      o["t"] = aiHist[i].raw;
    } else {
      uint8_t mood;
      String sh, lg;
      aiParse(aiHist[i].raw, mood, sh, lg);
      o["t"] = lg;
      o["m"] = AI_MOODS[mood];
    }
  }
  d["busy"] = aiState != 0;
  if (aiState != 0) d["pending"] = aiQ;
  d["err"] = aiErr;
  d["daily"] = (aiDailyKey && aiDailyKey == aiDayKey()) ? aiDailyText : String("");
  xSemaphoreGive(mtx);
  String out;
  serializeJson(d, out);
  server.send(200, "application/json; charset=utf-8", out);
}
void handleBibiAsk() {
  String q = server.arg("q");
  q.trim();
  if (q.length() > 160) q = q.substring(0, 160);
  if (!q.length()) { server.send(200, "text/plain; charset=utf-8", "Scrivi qualcosa a Bibi"); return; }
  if (!aiAsk(q, 0)) { server.send(200, "text/plain; charset=utf-8", "Bibi sta ancora rispondendo: aspetta un attimo"); return; }
  server.send(200, "text/plain; charset=utf-8", "OK");
}
void handleBibiClear() {
  aiClearHist();
  server.send(200, "text/plain; charset=utf-8", "OK");
}
void handleXpReset() {
  xpReset();
  needFull = true;
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleRestart() {
  server.send(200, "text/plain; charset=utf-8", "OK");
  delay(300);
  ESP.restart();
}

bool factoryPending = false;
uint32_t factoryAt = 0;

void handleFactory() {
  server.send(200, "text/plain; charset=utf-8", "OK");
  factoryPending = true;
  factoryAt = millis() + 800;
}

// ripristino di fabbrica: cancella impostazioni e rete WiFi, poi riparte dal portale di configurazione
void doFactory() {
  gPortal = true;
  tft.fillScreen(C_BG);
  drawStr("RIPRISTINO", 120, 96, 4, 1, C_TEXT, C_BG);
  drawStr("in corso...", 120, 136, 2, 1, C_DIM, C_BG);
  prefs.begin("nmtv", false);
  prefs.clear();
  prefs.end();
  if (fsOk) LittleFS.format();           // cancella anche le foto
  WiFi.disconnect(true, true);
  delay(400);
  ESP.restart();
}

// i tre tasti della pagina web: indietro = doppio click, entra/esci = pressione lunga, avanti = click
void handleKey() {
  String b = server.arg("b");
  if (!inSplash && !gPortal) {
    if (b == "prev") handleGesture(G_DOUBLE);
    else if (b == "enter") handleGesture(G_LONG);
    else if (b == "next") handleGesture(G_SINGLE);
  }
  server.send(200, "text/plain; charset=utf-8", stateText());
}

void handleState() {
  server.send(200, "text/plain; charset=utf-8", stateText());
}

// aggiornamento firmware da browser
void handleUpdateUpload() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    otaOk = true;
    otaBegun = false;
    otaFirst = true;
    otaErr = "";
    gPortal = true;                 // ferma il task dati durante l'aggiornamento
    if (u.filename.indexOf("completo") >= 0) {
      otaOk = false;
      otaErr = "usa il file nmtv-firmware-app.bin, non quello completo";
    } else {
      otaScreen("Caricamento...", "Non scollegare");
      Serial.printf("OTA: %s\n", u.filename.c_str());
      if (Update.begin(UPDATE_SIZE_UNKNOWN)) otaBegun = true;
      else { otaOk = false; otaErr = "spazio insufficiente"; }
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (otaOk) {
      if (otaFirst) {
        otaFirst = false;
        if (u.buf[0] != 0xE9) { otaOk = false; otaErr = "file non valido"; }
      }
      if (otaOk && Update.write(u.buf, u.currentSize) != u.currentSize) {
        otaOk = false;
        otaErr = "scrittura fallita";
      }
    }
  } else if (u.status == UPLOAD_FILE_END) {
    if (otaOk) {
      if (!Update.end(true)) { otaOk = false; otaErr = "immagine non valida"; }
    } else if (otaBegun) {
      Update.abort();
    }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (otaBegun) Update.abort();
    otaOk = false;
    otaErr = "interrotto";
  }
}

void handleUpdateDone() {
  bool ok = otaOk && !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain; charset=utf-8", ok ? String("OK") : (otaErr.length() ? otaErr : String("errore")));
  if (ok) {
    otaScreen("Fatto!", "Riavvio...");
    delay(800);
    ESP.restart();
  } else {
    gPortal = false;
    needFull = true;
  }
}

bool srvUp = false;

void handleLogo() {
  server.sendHeader("Cache-Control", "public, max-age=86400");
  server.send_P(200, "image/jpeg", (PGM_P)LOGO_WEB_JPG, LOGO_WEB_JPG_LEN);
}

void startServer() {
  if (srvUp) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/tasto", HTTP_GET, handleKey);
  server.on("/stato", HTTP_GET, handleState);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/apply", HTTP_POST, handleApply);
  server.on("/factory", HTTP_POST, handleFactory);
  server.on("/riavvia", HTTP_POST, handleRestart);
  server.on("/foto", HTTP_POST, handlePhotoDone, handlePhotoUpload);
  server.on("/logo.jpg", HTTP_GET, handleLogo);
  server.on("/foto/elenco", HTTP_GET, handlePhotoList);
  server.on("/foto/img", HTTP_GET, handlePhotoImg);
  server.on("/foto/del", HTTP_POST, handlePhotoDel);
  server.on("/bibi/chat", HTTP_GET, handleBibiChat);
  server.on("/bibi/ask", HTTP_POST, handleBibiAsk);
  server.on("/bibi/clear", HTTP_POST, handleBibiClear);
  server.on("/xp/reset", HTTP_POST, handleXpReset);
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
  srvUp = true;
}

// ------------------------------------------------------ Portale WiFi --------
void runPortal() {
  gPortal = true;   // ferma il task dati mentre il portale e' attivo
  tft.fillScreen(C_BG);
  drawStr("Configurazione", 120, 14, 4, 1, C_ACC, C_BG);
  drawStr("Cerco le reti WiFi...", 120, 70, 2, 1, C_DIM, C_BG);

  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  netOpts = "";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length() || netOpts.indexOf("'" + htmlEsc(s) + "'") >= 0) continue;
    netOpts += "<option value='" + htmlEsc(s) + "'>" + htmlEsc(s) + "</option>";
  }
  WiFi.softAP(AP_NAME);
  delay(300);
  dns.start(53, "*", WiFi.softAPIP());
  startServer();

  tft.fillScreen(C_BG);
  drawStr("Configurazione", 120, 14, 4, 1, C_ACC, C_BG);
  drawStr("1. Collegati al WiFi", 120, 64, 2, 1, C_TEXT, C_BG);
  drawStr(AP_NAME, 120, 86, 4, 1, C_ACC2, C_BG);
  drawStr("2. Apri il browser su", 120, 134, 2, 1, C_TEXT, C_BG);
  drawStr("192.168.4.1", 120, 156, 4, 1, C_ACC2, C_BG);
  drawStr("Imposta rete, citta e ora", 120, 204, 2, 1, C_DIM, C_BG);

  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    delay(5);
  }
}

// ----------------------------------------------------- Gestione schermate ---
void drawFull() {
  gCryptoShow = (inFunc && menuIdx == M_CRIPTO) ? crCoin() : -1;   // dice al task dati cosa serve
  if (!inFunc) {
    menuFull();
  } else if (menuIdx == 0) {
    if (funcIdx == 0) meteoFull();
    else if (funcIdx == 1) previsioniFull();
    else graficoFull();
  } else if (menuIdx == 1) {
    if (funcIdx == 0) airFull();
    else healthFull();
  } else if (menuIdx == M_LUNA) {
    moonFull();
  } else if (menuIdx == M_ISS) {
    issFull();
  } else if (menuIdx == M_FLIGHT) {
    flightFull();
  } else if (menuIdx == M_BAI) {
    aiFullScreen();
  } else if (menuIdx == M_CRIPTO) {
    cryptoFull();
  } else if (menuIdx == M_TIMER) {
    timerFull();
  } else if (menuIdx == M_FRAT) {
    fractalFull();
  } else if (menuIdx == M_COUNT) {
    countdownFull();
  } else if (menuIdx == M_CITA) {
    quoteFull();
  } else if (menuIdx == M_NUM) {
    luckyFull();
  } else if (menuIdx == M_POST) {
    noteFull();
  } else if (menuIdx == M_BIBI) {
    bibiFull();
  } else if (menuIdx == M_FOTO) {
    photoFull();
  } else if (menuIdx == M_QUAD) {
    clockFull(funcIdx);
  } else if (menuIdx == M_POMO) {
    if (funcIdx == 1) xpFull();
    else pomoFull();
  } else {
    sysFull();
  }
}

void handleGesture(Gesture g) {
  if (!inFunc) {
    if (g == G_SINGLE) menuIdx = (menuIdx + 1) % NMENU;
    else if (g == G_DOUBLE) menuIdx = (menuIdx + NMENU - 1) % NMENU;
    else if (g == G_LONG) {
      inFunc = true;
      funcIdx = 0;
    }
  } else if (menuIdx == M_LUNA || menuIdx == M_ISS) {   // schermate di sola visione: la pressione lunga torna al menu
    if (g != G_LONG) return;
    inFunc = false;
  } else if (menuIdx == M_CRIPTO) {     // Cripto: avanti/indietro cambiano moneta a prescindere dal tempo
    if (g == G_SINGLE) crStep(1);
    else if (g == G_DOUBLE) crStep(-1);
    else if (g == G_LONG) inFunc = false;
  } else if (menuIdx == M_FRAT || menuIdx == M_COUNT || menuIdx == M_CITA || menuIdx == M_NUM || menuIdx == M_POST) {
    if (g == G_LONG) {
      inFunc = false;                                // pressione lunga: torna al menu
    } else {                                         // 1 tocco = avanti, 2 tocchi = indietro
      const int d = (g == G_SINGLE) ? 1 : -1;
      if (menuIdx == M_FRAT) fractalStep(d);
      else if (menuIdx == M_COUNT) countdownStep(d);
      else if (menuIdx == M_CITA) quoteStep(d);
      else if (menuIdx == M_NUM) luckyStep(d);
      else noteStep(d);
    }
  } else if (menuIdx == M_FLIGHT) {     // Radar voli: avanti/indietro scelgono l'aereo, pressione lunga = menu
    if (g == G_SINGLE) flightStep(1);
    else if (g == G_DOUBLE) flightStep(-1);
    else if (g == G_LONG) { inFunc = false; gFlOpen = false; }
  } else if (menuIdx == M_BAI) {        // Bibi AI: avanti = commento di Bibi, indietro = risposte precedenti, lunga = menu
    if (g == G_LONG) inFunc = false;
    else aiGesture(g);
  } else if (menuIdx == M_BIBI) {       // Bibigotchi: avanti/indietro scorrono gli stati di Bibi
    if (g == G_SINGLE) bibiStep(1);
    else if (g == G_DOUBLE) bibiStep(-1);
    else if (g == G_LONG) inFunc = false;
  } else if (menuIdx == M_FOTO) {       // Foto: avanti/indietro cambiano foto a prescindere dal tempo
    if (g == G_SINGLE) phStep(1);
    else if (g == G_DOUBLE) phStep(-1);
    else if (g == G_LONG) inFunc = false;
  } else if (menuIdx == M_TIMER) {      // Timer: avanti = avvia/pausa/riprendi, 2 tocchi = valore iniziale, pressione lunga = menu
    if (g == G_SINGLE) tmToggle();
    else if (g == G_DOUBLE) tmReset();
    else if (g == G_LONG) inFunc = false;
  } else if (menuIdx == M_POMO && funcIdx == 1) {   // Livelli e XP: ogni gesto chiude la festa; poi si torna al timer o al menu
    const bool cel = xpCelebrating();
    xpEndCelebration();
    if (g == G_LONG) inFunc = false;
    else if (!cel) funcIdx = 0;
  } else if (menuIdx == M_POMO) {       // Pomodoro: avanti = avvia/pausa/riprendi, indietro = azzera (a timer fermo: Livelli e XP)
    if (g == G_SINGLE) poToggle();
    else if (g == G_DOUBLE) {
      if (poState == 0) funcIdx = 1;
      else poReset();
    } else if (g == G_LONG) {
      if (poState == 0) inFunc = false;   // gia' azzerato: torna al menu
      else poReset();                     // altrimenti azzera
    }
  } else {
    int n = funcCount(menuIdx);
    if (g == G_SINGLE) funcIdx = (funcIdx + 1) % n;
    else if (g == G_DOUBLE) funcIdx = (funcIdx + n - 1) % n;
    else if (g == G_LONG) inFunc = false;
  }
  needFull = true;
}

void updateScreen() {
  if (needFull) {
    needFull = false;
    syncData();
    drawFull();
    return;
  }
  bool changed = syncData();
  if (!inFunc) {
#ifdef TOUCH_DEBUG
    static uint32_t dbgAt = 0;
    if (millis() - dbgAt > 150) {
      dbgAt = millis();
      char b[48];
      snprintf(b, sizeof b, "T:%u base:%.0f on:%u %s", touchRaw, touchBase, thrOn, tState ? "PREMUTO" : "");
      tft.fillRect(0, 224, 240, 16, C_BG);
      drawStr(b, 120, 224, 2, 1, tState ? C_ACC2 : C_DIM, C_BG);
    }
#endif
  } else if (menuIdx == 0 && funcIdx == 0) {
    meteoTick(changed);
  } else if (menuIdx == M_LUNA) {
    moonTick();
  } else if (menuIdx == M_ISS) {
    issTick();
  } else if (menuIdx == M_FLIGHT) {
    flightTick();
  } else if (menuIdx == M_BAI) {
    aiTickScreen();
  } else if (menuIdx == M_CRIPTO) {
    cryptoTick();
  } else if (menuIdx == M_TIMER) {
    timerTick();
  } else if (menuIdx == M_FRAT) {
    fractalTick();
  } else if (menuIdx == M_BIBI) {
    bibiTick();
  } else if (menuIdx == M_FOTO) {
    photoTick();
  } else if (menuIdx == M_QUAD) {
    clockTick(funcIdx, changed);
  } else if (menuIdx == M_POMO) {
    if (funcIdx == 1) xpTick();
    else pomoTick();
  } else if (menuIdx == M_SIS) {
    sysTick();
  } else if (changed) {
    drawFull();
  }
}

// -------------------------------------------------------------- setup -------
void setup() {
  Serial.begin(115200);
  delay(50);
  loadCfg();
  if (cfg.mig < 7) {                       // dalla 1.0.7 Cripto e' la terza schermata: sposta la scelta salvata
    if (cfg.startSel >= 6) cfg.startSel++;
    cfg.mig = 7;
    saveCfg();
  }
  if (cfg.mig < 8) {                       // dalla 1.0.8 ci sono Foto (dopo Cripto) e Pomodoro (prima di Sistema)
    if (cfg.startSel >= 7 && cfg.startSel <= 10) cfg.startSel++;      // Quadranti
    else if (cfg.startSel == 11) cfg.startSel = 13;                   // Sistema
    cfg.mig = 8;
    saveCfg();
  }
  if (cfg.mig < 9) {                       // dalla 1.0.9 c'e' Bibigotchi (dopo Cripto): le funzioni successive scalano di uno
    if (cfg.startSel >= 7) cfg.startSel++;
    cfg.mig = 9;
    saveCfg();
  }
  if (cfg.mig < 10) {                      // dalla 2.0.0 ci sono Fasi Lunari e Radar ISS (dopo Aria): da Cripto in poi scalano di due
    if (cfg.startSel >= 6) cfg.startSel += 2;
    cfg.mig = 10;
    saveCfg();
  }
  if (cfg.mig < 11) {                      // dalla 2.0.1: Frattali prima di Pomodoro e quattro schermate prima di Sistema
    if (cfg.startSel == 16) cfg.startSel = 21;         // Sistema
    else if (cfg.startSel == 15) cfg.startSel = 16;    // Pomodoro
    cfg.mig = 11;
    saveCfg();
  }
  if (cfg.mig < 12) {                      // dalla 2.0.3 c'e' il Timer dopo Pomodoro: le funzioni successive scalano di uno
    if (cfg.startSel >= 17) cfg.startSel++;
    cfg.ssMask = (cfg.ssMask & 0xFFFFu) | ((cfg.ssMask >> 16) << 17);   // anche la scelta del salvaschermo
    cfg.mig = 12;
    saveCfg();
  }
  if (cfg.mig < 13) {                      // dalla 2.0.4: Radar Voli, Bibi AI, un quadrante e una funzione Pomodoro in piu'
    auto mp = [](int o) -> int {           // vecchio numero di funzione (1..22) -> nuovo (1..26)
      if (o <= 7) return o;
      if (o == 8) return 9;
      if (o == 9) return 10;
      if (o == 10) return 12;
      if (o <= 14) return o + 2;
      if (o == 15) return 18;
      if (o == 16) return 19;
      if (o == 17) return 21;
      return o + 4;
    };
    if (cfg.startSel >= 1) cfg.startSel = (uint8_t)mp(cfg.startSel);
    uint32_t nm = 0;
    for (int i = 0; i < 22; i++)
      if ((cfg.ssMask >> i) & 1u) nm |= 1u << (mp(i + 1) - 1);
    cfg.ssMask = nm ? nm : 1u;
    cfg.mig = 13;
    saveCfg();
  }
  applyTheme(cfg.theme);

  Serial.printf("\n=== Small TV Evolution Pro v%s ===\n", FW_VERSION);
  pinMode(PIN_PANEL_PWR, OUTPUT);
  digitalWrite(PIN_PANEL_PWR, LOW);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BL, 0);
  setBacklight(0);
  delay(300);                       // il pannello ha bisogno di tempo dopo l'accensione

  tft.init();
  tft.setRotation(cfg.rot ? 2 : 0);
  tft.invertDisplay(cfg.inv);
  tft.fillScreen(C_BG);
  applyBrightness(true);
  Serial.println("Display ok");

  mtx = xSemaphoreCreateMutex();
  fsOk = LittleFS.begin(true);          // memoria interna per le foto (si formatta al primo avvio)
  phScan();
  poReset();
  tmReset();
  xpLoad();

  if (cfg.ssid.length() == 0) runPortal();   // primo avvio: portale di configurazione

  inSplash = true;
  splashStart = millis();
  splashStatic();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("nmtv");
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  configTzTime(TZ_ROME, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  calibrateTouch();
  startServer();
  xTaskCreatePinnedToCore(dataTask, "dati", 20480, nullptr, 1, nullptr, 0);

  menuIdx = 0;
  inFunc = false;
  needFull = true;
  warmUntil = millis() + 6000;
}

// ------------------------------------------------------------ SALVASCHERMO -----
// Dopo ssMin minuti senza tocchi mostra a giro le funzioni scelte; un tocco lo ferma e ripristina la schermata di prima.
static bool ssActive = false, ssSavedIn = false, ssSwallow = false;
static int ssSavedM = 0, ssSavedF = 0, ssPos = -1;
static uint32_t ssNextAt = 0, ssSwallowUntil = 0, ssLastAct = 0, ssSeenAct = 0;

static int ssCount() {
  int n = 0;
  for (int i = 0; i < selTotal() && i < 32; i++)
    if ((cfg.ssMask >> i) & 1u) n++;
  return n;
}

static void ssAdvance() {                          // passa alla funzione scelta successiva
  const int tot = selTotal();
  for (int k = 1; k <= tot; k++) {
    const int i = (ssPos + k + tot) % tot;
    if (i < 32 && ((cfg.ssMask >> i) & 1u)) {
      int m, f;
      if (!selToMF(i + 1, m, f)) continue;
      ssPos = i;
      menuIdx = m;
      funcIdx = f;
      inFunc = true;
      needFull = true;
      break;
    }
  }
  ssNextAt = millis() + (uint32_t)cfg.ssSec * 1000UL;
}

static void ssStop() {
  ssActive = false;
  menuIdx = ssSavedM;
  funcIdx = ssSavedF;
  inFunc = ssSavedIn;
  needFull = true;
  ssLastAct = millis();
}

static void ssTick() {
  if (lastTouchAct != ssSeenAct) {                 // un tocco: e' attivita'
    ssSeenAct = lastTouchAct;
    ssLastAct = millis();
    if (ssActive) {
      ssStop();
      ssSwallow = true;                            // il gesto che ne segue serve solo a uscire
      ssSwallowUntil = millis() + 2500;
    }
  }
  if (!cfg.ssOn) {
    if (ssActive) ssStop();
    return;
  }
  if (!ssActive) {
    if (!gPortal && !otaBegun && millis() - ssLastAct >= (uint32_t)cfg.ssMin * 60000UL) {
      ssActive = true;
      ssSavedIn = inFunc;
      ssSavedM = menuIdx;
      ssSavedF = funcIdx;
      ssPos = -1;
      ssAdvance();
    }
  } else if (ssCount() > 1 && (int32_t)(millis() - ssNextAt) >= 0) {
    ssAdvance();
  }
}

void loop() {
  static bool mdnsOk = false;
  if (!mdnsOk && WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("nmtv")) MDNS.addService("http", "tcp", 80);
    mdnsOk = true;
  }
  server.handleClient();

  static uint32_t brAt = 0;
  if (millis() - brAt > 1000) {   // luminosita' giorno/notte
    brAt = millis();
    applyBrightness(false);
  }
  if (wifiReconnectAt && (int32_t)(millis() - wifiReconnectAt) >= 0) {
    wifiReconnectAt = 0;
    WiFi.disconnect();
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  }
  if (factoryPending && (int32_t)(millis() - factoryAt) >= 0) doFactory();

  if (inSplash) {                 // titolo e indirizzo IP: non toccare il pulsante
    touchPressed();               // intanto il sensore si assesta
    splashTick();
    delay(10);
    return;
  }

  gIssOpen = inFunc && menuIdx == M_ISS;   // il task dati aggiorna l'ISS solo se la schermata e' aperta
  gFlOpen = inFunc && menuIdx == M_FLIGHT; // idem per i voli
  if (!(inFunc && menuIdx == M_POMO && funcIdx == 0)) poUpdate();   // il Pomodoro avanza anche se guardi altro
  ssTick();
  Gesture g = readGesture();
  if (ssSwallow && g != G_NONE && g != G_VLONG) {   // gesto dell'uscita dal salvaschermo: non va eseguito
    ssSwallow = false;
    g = G_NONE;
  }
  if (ssSwallow && (int32_t)(millis() - ssSwallowUntil) >= 0) ssSwallow = false;
  if (g == G_VLONG) {
    runPortal();                // non ritorna
  } else if (g != G_NONE) {
    Serial.printf("Gesto: %d\n", (int)g);
    handleGesture(g);
  }
  updateScreen();
  delay(10);
}
