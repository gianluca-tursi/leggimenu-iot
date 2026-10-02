/*
 * leggimenu - radar + luce sul Freenove ESP32 (WROOM-32E)
 *
 * Conta chi si siede al tavolo e accende un LED per persona.
 *
 * Il problema vero non e' contare: e' NON contare i mobili. L'LD2450 non e'
 * un sensore di movimento, e' un radar che insegue: quando aggancia una
 * riflessione forte la tiene per sempre, anche se e' un muro. Misurato sul
 * banco, un bersaglio fisso a 94 cm dava velocita' zero in 226 frame su 226.
 * Una persona seduta no: respira, si assesta, e ogni tanto una velocita'
 * diversa da zero la produce. Il filtro qui sotto vive su questa differenza.
 *
 * COLLEGAMENTI
 *   Freenove 5V    ->  5V  del radar        Freenove 5V   ->  VCC dello stick
 *   Freenove GND   ->  GND del radar        Freenove GND  ->  GND dello stick
 *   Freenove 16    ->  TX  del radar        Freenove 13   ->  IN  dello stick
 *
 *   ./scripts/flash-freenove.sh firmware/radar_luce
 *   ./scripts/radar-live.sh
 */

#include <Arduino.h>
#include <string.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <LittleFS.h>

// --- registro a bordo ---------------------------------------------------
// Il prototipo deve poter scendere in sala col power bank, senza Mac. Scrive
// tutto nella flash interna; al ritorno si rilegge da USB con:
//     ./scripts/scarica-log.sh
//
// Registro anche i bersagli SCARTATI, con il motivo. Cosi' se il filtro
// sbaglia posso ritararlo sui dati veri invece che a intuito, senza dover
// rifare la prova.
static const char  *FILE_LOG   = "/registro.csv";
static const size_t LIMITE_LOG = 1200000;   // lascio respiro alla partizione
static const int    PIN_TASTO  = 0;         // il tasto BOOT gia' sulla scheda

File     reg;
char     bufLog[1024];
size_t   nBufLog = 0;
uint32_t ultimoFlush = 0;
bool     logAttivo = false;
int      marcatori = 0;
bool     tastoPrima = true;
uint32_t ultimoTasto = 0;

// --- radar --------------------------------------------------------------
static const int PIN_RX = 16;
static const int PIN_TX = 17;            // verso l'RX del radar: per ora non
                                         // gli scriviamo, ma il filo c'e' e
                                         // serve per configurare il modulo
static const uint32_t BAUD_RADAR = 256000;
static const uint8_t HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t FOOTER[2] = {0x55, 0xCC};
static const size_t  LUNG = 30;

uint8_t buf[256];
size_t  usati = 0;
uint32_t byteTot = 0, frameTot = 0, ultimoFrame = 0, ultimaDiag = 0;
static const uint32_t GUASTO_MS = 3000;

// --- filo verso il pannello e-ink ---------------------------------------
// Il pannello non ha sensori: gli dico io quanti coperti mostrare. Uso un
// UART dedicato invece del WiFi perche' e' immediato, non dipende dal router
// e si legge con un monitor seriale quando qualcosa non torna.
//   Freenove 25 -> CrowPanel IO18   |   Freenove 26 <- CrowPanel IO17
// (16 e 17 sono del radar, 13 dei LED: qui non si puo' passare)
//   e soprattutto GND in comune fra le due schede.
static const int PIN_TX_PANNELLO = 25;
static const int PIN_RX_PANNELLO = 26;

// --- stato del tavolo ---------------------------------------------------
// Il tavolo si APRE da solo quando arriva qualcuno, ma non si chiude da solo:
// al ristorante la gente si alza per andare in bagno, e uno schermo che torna
// a "prenotato" mentre stanno mangiando sarebbe una figuraccia. Lo libera il
// cameriere dalla dashboard, a pasto finito.
bool    tavoloAperto = false;
uint8_t copertiTavolo = 0;

// Dopo "il pasto e' finito" ignoro il radar per un po'. Senza questa pausa il
// tavolo si riapre all'istante sul cameriere che sparecchia, ed e' come se il
// tasto non funzionasse. Il valore si regola dalla dashboard e resta in NVS.
Preferences impostazioni;
uint32_t pausaMs   = 5000;
uint32_t liberatoA = 0;        // 0 = nessuna pausa in corso

// Il pannello puo' riavviarsi in qualunque momento - per un caricamento, per un
// contatto sul filo dei 3,3V - e ripartirebbe da "prenotato" mentre la cena e'
// in corso. Io mando lo stato solo quando cambia, quindi non se ne accorgerebbe
// nessuno. Percio' lo ripeto a intervalli: il pannello ridisegna solo se e'
// davvero diverso da quello che ha su, quindi ripeterlo non costa niente.
static const uint32_t RIPETI_STATO_MS = 15000;
uint32_t ultimoAnnuncio = 0;

// --- il tavolo ----------------------------------------------------------
// Oltre questa distanza non e' piu' il nostro tavolo: e' la sala, il tavolo
// accanto, il cameriere che passa. Non ci interessa.
static const int PORTATA_CM = 90;

// --- filtro arredamento -------------------------------------------------
// Tengo un'ancora per ogni punto dove ho visto qualcosa. Se in quel punto per
// un po' non si muove niente, e' un mobile e lo escludo dal conteggio.
static const int MAX_ANCORE   = 10;
static const int TOLLERANZA_CM = 25;   // entro questo raggio e' "lo stesso oggetto"

// Quanto deve durare il moto per essere creduto. Un singolo campione a
// velocita' non nulla puo' essere rumore; mezzo secondo di seguito no.
static const uint32_t MOTO_VERO_MS = 400;

// Dopo il reset do per scontato che il tavolo sia vuoto: tutto quello che sta
// fermo un attimo e' arredamento. Passata la taratura divento prudente, perche'
// da li' in poi chi sta fermo potrebbe essere un cliente.
static const uint32_t TARATURA_MS = 5000;
static const uint32_t FERMO_SUBITO_MS = 1200;    // durante la taratura
static const uint32_t FERMO_DOPO_MS   = 30000;   // a regime

struct Ancora {
  int x, y;                 // cm, posizione di riposo
  uint32_t ultimoMoto;      // ultima volta che li' si e' mosso qualcosa
  uint32_t inizioMoto;      // da quando dura il moto in corso (0 = fermo)
  uint32_t visto;
  bool viva;
};
Ancora ancore[MAX_ANCORE];
uint32_t inizioTaratura = 0;

// --- luce ---------------------------------------------------------------
#define PIN_LED  13
#define NUM_LED   8

// 8 LED a piena potenza chiedono 480 mA, e dalla stessa USB alimentiamo anche
// radar ed ESP32. Non alzarlo senza alimentazione separata.
#define MAX_LUM  70
#define CALDO_R 255
#define CALDO_G 140
#define CALDO_B  40

Adafruit_NeoPixel led(NUM_LED, PIN_LED, NEO_GRB + NEO_KHZ800);

// Il conteggio istantaneo balla: due persone vicine ogni tanto si fondono.
// Accendo sul massimo degli ultimi secondi, cosi' la luce non sfarfalla.
// --- animazione ---------------------------------------------------------
// Una strisciata di LED ogni volta che cambia qualcosa: si vede con la coda
// dell'occhio anche da chi non sta guardando il display.
//
// E' a macchina a stati, NON a delay(): durante l'animazione il radar continua
// a mandare 30 byte ogni 100 ms, e se restassimo fermi un secondo il buffer
// seriale traboccherebbe e perderemmo frame.
static const uint32_t PASSO_MS = 45;      // 8 LED = 360 ms a giro
int      animGiri = 0;
int      animPasso = 0;
uint8_t  animDopo = 0;                    // quanti restano accesi alla fine
uint32_t animUltimo = 0;

static const uint32_t FINESTRA_MS = 3000;
struct Campione { uint32_t t; uint8_t n; };
Campione storico[64];
int nStorico = 0;
int accesiOra = -1;

static int coord(uint16_t g) { int v = g & 0x7FFF; return (g & 0x8000) ? v : -v; }

static void accendi(uint8_t quante);

/* Lancia <giri> strisciate, poi lascia accesi <restano> LED. */
static void avviaStrisciata(int giri, uint8_t restano) {
  animGiri = giri; animPasso = 0; animDopo = restano; animUltimo = 0;
}

/* Da chiamare a ogni giro di loop(): avanza di un passo se e' ora. */
static void aggiornaLuce() {
  if (animGiri <= 0) return;
  if (millis() - animUltimo < PASSO_MS) return;
  animUltimo = millis();
  disegnaScia(animPasso);
  if (++animPasso >= NUM_LED + 3) {       // +3: lascio uscire la coda
    animPasso = 0;
    if (--animGiri <= 0) accendi(animDopo);
  }
}

// ------------------------------------------------------------------ luce
static void registra(uint8_t n) {
  if (nStorico >= (int)(sizeof(storico) / sizeof(storico[0]))) {
    memmove(storico, storico + 1, sizeof(storico) - sizeof(storico[0]));
    nStorico--;
  }
  storico[nStorico++] = {millis(), n};
}

static uint8_t stimaPersone() {
  uint32_t ora = millis();
  int scrivi = 0;
  uint8_t massimo = 0;
  for (int i = 0; i < nStorico; i++) {
    if (ora - storico[i].t <= FINESTRA_MS) {
      storico[scrivi++] = storico[i];
      if (storico[i].n > massimo) massimo = storico[i].n;
    }
  }
  nStorico = scrivi;
  return massimo;
}

static void disegnaScia(int pos) {
  for (int i = 0; i < NUM_LED; i++) {
    int d = pos - i;                      // la coda resta indietro e sfuma
    int f = (d == 0) ? 255 : (d == 1) ? 70 : (d == 2) ? 18 : 0;
    led.setPixelColor(i, led.Color(CALDO_R * MAX_LUM / 255 * f / 255,
                                   CALDO_G * MAX_LUM / 255 * f / 255,
                                   CALDO_B * MAX_LUM / 255 * f / 255));
  }
  led.show();
}

static void accendi(uint8_t quante) {
  if (quante > NUM_LED) quante = NUM_LED;
  uint32_t c = led.Color(CALDO_R * MAX_LUM / 255,
                         CALDO_G * MAX_LUM / 255,
                         CALDO_B * MAX_LUM / 255);
  for (int i = 0; i < NUM_LED; i++) led.setPixelColor(i, i < quante ? c : 0);
  led.show();
}

// ------------------------------------------------------- filtro arredamento
static void azzeraAncore() {
  for (int i = 0; i < MAX_ANCORE; i++) ancore[i].viva = false;
  inizioTaratura = millis();
  nStorico = 0;
}

static uint32_t sogliaFermo() {
  bool inTaratura = (millis() - inizioTaratura) < TARATURA_MS;
  return inTaratura ? FERMO_SUBITO_MS : FERMO_DOPO_MS;
}

/* Aggiorna l'ancora di questo bersaglio e dice se e' arredamento. */
static bool arredamento(int x, int y, int v) {
  uint32_t ora = millis();

  int scelta = -1, libera = -1;
  for (int i = 0; i < MAX_ANCORE; i++) {
    if (!ancore[i].viva) { if (libera < 0) libera = i; continue; }
    int dx = x - ancore[i].x, dy = y - ancore[i].y;
    if (dx * dx + dy * dy <= TOLLERANZA_CM * TOLLERANZA_CM) { scelta = i; break; }
  }

  if (scelta < 0) {                      // roba nuova: la do per viva
    if (libera < 0) return false;
    ancore[libera] = {x, y, ora, 0, ora, true};
    return false;
  }

  Ancora &a = ancore[scelta];
  a.visto = ora;

  // Si e' mosso? O per velocita' misurata, o perche' e' scivolato via dal
  // punto di riposo. Ma lo credo solo se dura.
  int dx = x - a.x, dy = y - a.y;
  bool inMoto = (v != 0) || (dx * dx + dy * dy > (TOLLERANZA_CM / 2) * (TOLLERANZA_CM / 2));

  if (inMoto) {
    if (!a.inizioMoto) a.inizioMoto = ora;
    if (ora - a.inizioMoto >= MOTO_VERO_MS) {
      a.ultimoMoto = ora;                // e' vivo: l'ancora lo segue
      a.x = x; a.y = y;
    }
  } else {
    a.inizioMoto = 0;
  }

  return (ora - a.ultimoMoto) > sogliaFermo();
}

static int quantiFissi() {
  uint32_t ora = millis(), soglia = sogliaFermo();
  int n = 0;
  for (int i = 0; i < MAX_ANCORE; i++)
    if (ancore[i].viva && (ora - ancore[i].ultimoMoto) > soglia) n++;
  return n;
}

// ---------------------------------------------------------------- registro
static void logRiga(const char *s) {
  if (!logAttivo) return;
  size_t l = strlen(s);
  if (l >= sizeof(bufLog)) return;
  if (nBufLog + l >= sizeof(bufLog)) {       // svuoto prima di traboccare
    reg.write((const uint8_t *)bufLog, nBufLog);
    nBufLog = 0;
  }
  memcpy(bufLog + nBufLog, s, l);
  nBufLog += l;
}

static void svuotaLog() {
  if (!logAttivo || !nBufLog) return;
  reg.write((const uint8_t *)bufLog, nBufLog);
  nBufLog = 0;
  reg.flush();
  if (reg.size() > LIMITE_LOG) {
    logAttivo = false;
    Serial.println("[registro] PIENO: smetto di scrivere. Scarica e cancella.");
  }
}

static void apriLog() {
  if (!LittleFS.begin(true)) {
    Serial.println("[registro] flash non montabile: registro DISATTIVO");
    return;
  }
  reg = LittleFS.open(FILE_LOG, "a");
  if (!reg) { Serial.println("[registro] non apribile: registro DISATTIVO"); return; }
  logAttivo = true;
  char r[96];
  snprintf(r, sizeof(r), "#AVVIO,%lu\n", (unsigned long)millis());
  logRiga(r);
  svuotaLog();
  Serial.printf("[registro] attivo, %u byte gia\' scritti\n", (unsigned)reg.size());
}

static void marcatore() {
  marcatori++;
  char r[96];
  snprintf(r, sizeof(r), "#MARCA,%lu,%d\n", (unsigned long)millis(), marcatori);
  logRiga(r);
  svuotaLog();
  Serial.printf(">>> segnalibro %d\n", marcatori);
  for (int k = 0; k < 2; k++) {             // due lampi bianchi: e\' andato
    for (int i = 0; i < NUM_LED; i++) led.setPixelColor(i, led.Color(60, 60, 60));
    led.show(); delay(90);
    accendi(accesiOra < 0 ? 0 : accesiOra); delay(90);
  }
}

static void riversaLog() {
  svuotaLog();
  reg.close();
  File f = LittleFS.open(FILE_LOG, "r");
  Serial.println("---INIZIO REGISTRO---");
  if (f) {
    uint8_t b[256];
    while (f.available()) Serial.write(b, f.read(b, sizeof(b)));
    f.close();
  }
  Serial.println("\n---FINE REGISTRO---");
  reg = LittleFS.open(FILE_LOG, "a");
}

static void cancellaLog() {
  nBufLog = 0;
  reg.close();
  LittleFS.remove(FILE_LOG);
  reg = LittleFS.open(FILE_LOG, "a");
  marcatori = 0;
  logAttivo = (bool)reg;
  Serial.println(">>> registro cancellato");
}

// ------------------------------------------------------------------ frame
static void esamina(const uint8_t *f) {
  frameTot++;
  ultimoFrame = millis();

  int persone = 0;
  char riga[240];  int k = 0;      // per la seriale: solo le persone
  char csv[200];   int c = 0;      // per il registro: tutto, anche gli scarti

  for (int s = 0; s < 3; s++) {
    const uint8_t *b = f + 4 + s * 8;
    uint16_t gx = b[0] | (b[1] << 8), gy = b[2] | (b[3] << 8), gv = b[4] | (b[5] << 8);
    if (!gx && !gy && !gv) continue;

    int x = coord(gx) / 10, y = coord(gy) / 10, v = coord(gv);   // cm, cm, cm/s
    float dist = sqrtf((float)x * x + (float)y * y);

    char stato;
    if (dist > PORTATA_CM)             stato = 'X';   // fuori dal tavolo
    else if (arredamento(x, y, v))     stato = 'F';   // mobile
    else                             { stato = 'P'; persone++; }

    c += snprintf(csv + c, sizeof(csv) - c, ";%d:%d:%d:%c", x, y, v, stato);

    if (stato == 'P')
      k += snprintf(riga + k, sizeof(riga) - k,
                    "  [%d] x=%dcm y=%dcm  dist=%.1fcm  v=%dcm/s\n",
                    persone, x, y, dist, v);
  }

  registra(persone);

  char testa[64];
  snprintf(testa, sizeof(testa), "%lu,%d,%d,%d",
           (unsigned long)millis(), persone, accesiOra < 0 ? 0 : accesiOra,
           quantiFissi());
  logRiga(testa);
  if (c) logRiga(csv);
  logRiga("\n");

  Serial.printf("bersagli: %d\n", persone);
  if (persone) Serial.print(riga);
}

/* Un comando di una lettera sola, dalla dashboard o dal monitor seriale. */
static void eseguiComando(char c) {
  switch (c) {
    case 'R':
      azzeraAncore();
      Serial.println(">>> reset: ritaro l'arredamento, lascia il sensore libero");
      break;

    case 'F':                                  // il pasto e' finito
      liberatoA = millis();
      tavoloAperto = false;
      copertiTavolo = 0;
      accesiOra = 0;
      animGiri = 0;
      accendi(0);
      azzeraAncore();                          // il tavolo e' sgombro: ritaro
      Serial1.println("L");
      Serial.printf(">>> tavolo liberato: pannello a \"prenotato\", LED spenti"
                    " (radar ignorato per %lu s)\n", (unsigned long)(pausaMs / 1000));
      break;

    case 'P':                                  // il pannello e' vivo?
      Serial1.println("?");
      Serial.println(">>> chiesto al pannello se c'e'; se non risponde entro"
                     " un secondo, il filo o il suo firmware non vanno");
      break;

    case 'Q':                                  // rimetti il QR del menu
      Serial1.println("Q");
      Serial.println(">>> QR del menu rimesso sul pannello");
      break;

    case 'Y':                                  // abbinamento del vino
      Serial1.println("Y");
      avviaStrisciata(3, accesiOra < 0 ? 0 : accesiOra);
      Serial.println(">>> vino proposto sul pannello");
      break;

    case 'Z':                                  // inizio di un ordine nuovo
    case 'X':                                  // mostra l'ordine sul pannello
      Serial1.println(c == 'Z' ? "Z" : "X");
      if (c == 'X') {
        avviaStrisciata(2, accesiOra < 0 ? 0 : accesiOra);
        Serial.println(">>> ordine mandato al pannello");
      }
      break;

    case 'W':                                  // qualcuno ha inquadrato il QR
      Serial1.println("W");
      avviaStrisciata(2, accesiOra < 0 ? 0 : accesiOra);   // strisciata di richiamo
      Serial.println(">>> QR inquadrato: pannello sulla schermata WiFi");
      break;

    case 'M': marcatore();   break;
    case 'L': riversaLog();  break;
    case 'C': cancellaLog(); break;
    case 'S':
      Serial.printf("[registro] %u byte, %d segnalibri, %s | tavolo %s\n",
                    (unsigned)reg.size(), marcatori,
                    logAttivo ? "attivo" : "FERMO",
                    tavoloAperto ? "APERTO" : "libero");
      break;
  }
}

/* Una riga dalla dashboard o dal monitor seriale. */
static void eseguiRiga(const char *r) {
  // "X:testo" -> lo giro al pannello cosi' com'e'
  if (r[1] == ':' && strchr("NOGUQVBC", r[0])) {
    Serial1.println(r);
    // La Q porta la chiave del WiFi: la inoltro ma non la scrivo nei log, che
    // finiscono nel terminale e nelle registrazioni.
    if (r[0] == 'Q') Serial.println(">>> al pannello: Q: (rete wifi)");
    else             Serial.printf(">>> al pannello: %s\n", r);
    return;
  }
  // "A:<secondi>" -> la pausa dopo la liberazione, la gestisco io
  if (r[0] == 'A' && r[1] == ':') {
    long sec = atol(r + 2);
    if (sec < 0) sec = 0;
    if (sec > 600) sec = 600;                // oltre dieci minuti e' un errore
    pausaMs = (uint32_t)sec * 1000UL;
    impostazioni.putUInt("pausa", pausaMs);
    Serial.printf("[pausa] %lu\n", (unsigned long)sec);
    return;
  }
  if (r[1] == 0) { eseguiComando(toupper(r[0])); return; }
  Serial.printf("[comando] non capisco: %s\n", r);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== leggimenu - radar + luce (Freenove) ===");
  Serial.printf("radar IO%d @ %lu baud | %d LED su IO%d | portata %d cm\n",
                PIN_RX, (unsigned long)BAUD_RADAR, NUM_LED, PIN_LED, PORTATA_CM);
  Serial.println("premi il tasto \"Nuova prova\" A TAVOLO VUOTO per ritarare.");

  led.begin();
  led.setBrightness(255);
  for (int i = 0; i < NUM_LED; i++) { accendi(i + 1); delay(60); }
  accendi(0);

  impostazioni.begin("leggimenu", false);
  pausaMs = impostazioni.getUInt("pausa", 5000);
  Serial.printf("[pausa] %lu\n", (unsigned long)(pausaMs / 1000));

  pinMode(PIN_TASTO, INPUT_PULLUP);
  apriLog();
  azzeraAncore();
  Serial1.begin(115200, SERIAL_8N1, PIN_RX_PANNELLO, PIN_TX_PANNELLO);
  Serial1.println("L");                      // parto dicendogli "tavolo libero"
  Serial.printf("pannello: parlo su IO%d, ascolto su IO%d\n",
                PIN_TX_PANNELLO, PIN_RX_PANNELLO);

  Serial2.setRxBufferSize(1024);
  Serial2.begin(BAUD_RADAR, SERIAL_8N1, PIN_RX, PIN_TX);
  Serial.println("tasto BOOT = segnalibro | da USB: L scarica, C cancella, S stato");
}

void loop() {
  // Ogni comando e' una riga. Prima distinguevo "lettera singola subito" da
  // "riga di testo", e bastava dimenticare una lettera nell'elenco perche' il
  // parser mangiasse mezzo comando: e' successo con G: e l'ospite diventava
  // "nluca". Una regola sola non si puo' dimenticare a meta'.
  static char cmdBuf[80]; static size_t cmdN = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdN) { cmdBuf[cmdN] = 0; eseguiRiga(cmdBuf); cmdN = 0; }
      continue;
    }
    if (cmdN < sizeof(cmdBuf) - 1) cmdBuf[cmdN++] = c;
  }

  while (Serial2.available()) {
    if (usati >= sizeof(buf)) usati = 0;
    buf[usati++] = Serial2.read();
    byteTot++;
    if (usati < LUNG) continue;
    for (size_t i = 0; i + LUNG <= usati; i++) {
      if (memcmp(buf + i, HEADER, 4) || memcmp(buf + i + LUNG - 2, FOOTER, 2)) continue;
      esamina(buf + i);
      size_t resto = usati - (i + LUNG);
      memmove(buf, buf + i + LUNG, resto);
      usati = resto;
      break;
    }
  }

  bool vivo = ultimoFrame && (millis() - ultimoFrame <= GUASTO_MS);
  uint8_t quante = vivo ? stimaPersone() : 0;

  // Il conteggio puo' solo salire finche' il tavolo e' aperto: se qualcuno si
  // sporge o esce dal cono per un attimo non e' che i coperti diminuiscono.
  bool inPausa = liberatoA && (millis() - liberatoA < pausaMs);
  if (quante > 0 && quante > copertiTavolo && !inPausa) {
    tavoloAperto = true;
    copertiTavolo = quante;
    accesiOra = quante;
    avviaStrisciata(quante, quante);              // tanti giri quante persone
    Serial1.printf("T:%u\n", quante);
    ultimoAnnuncio = millis();
    Serial.printf(">>> tavolo aperto: %u coperti (pannello avvisato)\n", quante);
  }
  if (millis() - ultimoAnnuncio > RIPETI_STATO_MS) {
    ultimoAnnuncio = millis();
    if (tavoloAperto) Serial1.printf("T:%u\n", copertiTavolo);
    else              Serial1.println("L");
  }

  aggiornaLuce();

  if (millis() - ultimoFlush > 2000) {
    ultimoFlush = millis();
    svuotaLog();
  }

  if (millis() - ultimaDiag > 2000) {
    ultimaDiag = millis();
    bool inTaratura = (millis() - inizioTaratura) < TARATURA_MS;
    // Lo stato del tavolo va ripetuto anche qui, non solo quando cambia: la
    // cassa aperta a meta' servizio altrimenti mostrerebbe un tavolo libero
    // mentre la gente sta mangiando.
    Serial.printf("[fissi] ignorati=%d taratura=%s tavolo=%d\n",
                  quantiFissi(), inTaratura ? "si" : "no",
                  tavoloAperto ? copertiTavolo : 0);
    Serial.printf("[diagnosi] radar=%s byte ricevuti: %lu | frame validi: %lu\n",
                  vivo ? "ok" : "MUTO",
                  (unsigned long)byteTot, (unsigned long)frameTot);
  }
}
