/*
 * leggimenu - il pannello del tavolo (CrowPanel ESP32-S3 4.2", 400x300)
 *
 * Non ha sensori: ascolta il nodo radar sul filo seriale e disegna quello che
 * gli viene detto. La divisione e' voluta - il radar deve girare a 10 Hz senza
 * mai fermarsi, e un refresh e-ink blocca la scheda per due secondi buoni.
 *
 * ALIMENTAZIONE
 *   Prende i 3,3V dal Freenove sul pettine. Quindi: MAI l'USB collegato qui
 *   mentre c'e' quel filo, altrimenti i due regolatori si spingono contro.
 *   Per ricaricare il firmware, stacca prima il filo 3V3.
 *
 * FILO CON IL FREENOVE (nodo radar)
 *   Freenove GND  <-> CrowPanel GND    <- indispensabile, e' il riferimento
 *   Freenove 25    ->  CrowPanel IO18  (io ascolto qui)
 *   Freenove 26   <-   CrowPanel IO17  (ritorno)
 *
 * PROTOCOLLO (righe di testo, apposta: si legge con un monitor seriale)
 *   T:<n>   tavolo aperto con n coperti
 *   L       tavolo libero
 *   ?       dimmi che stai bene
 *
 * Carica con:  RIAPRI=0 PORTA_OVERRIDE=<porta> ./scripts/flash.sh firmware/pannello42
 */

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

extern "C" {
#include "qrgen.h"
}

// --- cosa mostrare ------------------------------------------------------
// Quanti byte entrano in ogni versione a correzione MEDIA.
//
// Questa tabella non e' un vezzo: qrcode_initText() restituisce "tutto bene"
// ANCHE quando il testo non ci sta, scrive oltre lo spazio e produce un codice
// corrotto che sembra perfetto a vederlo. Ci ho perso mezza giornata: il
// pannello disegnava una versione 1 da 14 byte con dentro 28 caratteri, e i
// telefoni giustamente non la leggevano. Non fidarsi del valore di ritorno.
static const int VERSIONE_QR_MAX = 6;
static const int CAPIENZA[] = {0, 14, 26, 42, 62, 84, 106};

/* La versione piu' piccola in cui il testo ci sta davvero. 0 = non ci sta. */
static int versionePer(const char *testo) {
  int n = strlen(testo);
  for (int v = 1; v <= VERSIONE_QR_MAX; v++)
    if (n <= CAPIENZA[v]) return v;
  return 0;
}

// Nome e orario arrivano dalla dashboard e restano nella NVS: se il tavolo si
// spegne e si riaccende deve ritrovarsi la sua prenotazione, non i valori di
// fabbrica. Sono buffer, non costanti, apposta.
char URL_MENU[64]    = "https://leggimenu.it";
char QR_WIFI[80]     = "";                   // "WIFI:T:WPA;S:rete;P:chiave;;"
char NOME_TAVOLO[24] = "Tavolo 7";
char ORARIO[28]      = "riservato ore 20.00";
char OSPITE[24]      = "Gianluca";
Preferences memoria;

static void ricorda() {
  memoria.putString("nome",   NOME_TAVOLO);
  memoria.putString("orario", ORARIO);
  memoria.putString("ospite", OSPITE);
  memoria.putString("url",    URL_MENU);
  memoria.putString("wifi",   QR_WIFI);      // contiene la chiave: non stamparla
}

static void rileggi() {
  memoria.getString("nome",   NOME_TAVOLO, sizeof(NOME_TAVOLO));
  memoria.getString("orario", ORARIO,      sizeof(ORARIO));
  memoria.getString("ospite", OSPITE,      sizeof(OSPITE));
  memoria.getString("url",    URL_MENU,    sizeof(URL_MENU));
  memoria.getString("wifi",   QR_WIFI,     sizeof(QR_WIFI));
}

// --- pin del pannello (serigrafati sul retro della scheda) --------------
#define PIN_PWR   7        // alimentazione del pannello: senza, BUSY non risponde
#define PIN_BUSY 48
#define PIN_RES  47
#define PIN_DC   46
#define PIN_CS   45
#define PIN_CLK  12
#define PIN_MOSI 11

// --- tasti sulla scheda -------------------------------------------------
// Mappa verificata premendoli uno per uno sulla scheda (26/09/2026), perche'
// documentazione affidabile non ne ho trovata:
//
//   IO1        EXIT
//   IO2        MENU
//   IO4/5/6    il comando centrale, che oltre a premere si muove su e giu':
//              tre segnali, quindi serviranno per scorrere un menu sul pannello.
//              Quale dei tre sia "su" e quale "giu'" non l'ho ancora separato.
//
// Li sorveglio tutti e stampo chi si muove: quando serviranno gli altri, il log
// dice gia' quale pin e' stato toccato.
static const int TASTI[] = {1, 2, 4, 5, 6};
static const int N_TASTI = sizeof(TASTI) / sizeof(TASTI[0]);
static int  PIN_MENU = 2;              // ipotesi: si corregge col log alla mano
bool     tastoGiu[N_TASTI] = {false};
uint32_t ultimoTasto = 0;

// --- filo verso il nodo radar -------------------------------------------
static const int PIN_RX_NODO = 18;
static const int PIN_TX_NODO = 17;

// Il pannello e' quello serigrafato sul flat: SE0420Q0N4-FPC-A. La classe
// giusta e' questa - con GYE042A87, che sta negli esempi in giro per la rete,
// il BUSY non risponde mai e l'init muore in "Busy Timeout!".
GxEPD2_BW<GxEPD2_420_SE0420NQ04, GxEPD2_420_SE0420NQ04::HEIGHT>
    epd(GxEPD2_420_SE0420NQ04(PIN_CS, PIN_DC, PIN_RES, PIN_BUSY));

// L'e-ink non si puo' ridisegnare a raffica: ogni refresh completo costa due
// secondi e consuma. Sotto questo intervallo ignoro i cambi.
static const uint32_t MIN_FRA_DISEGNI_MS = 4000;

// Uso un valore impossibile come coperti per dire "mostra la schermata WiFi":
// cosi' passa dalla stessa macchina di ridisegno, con lo stesso freno dei 4
// secondi, invece di avere un percorso tutto suo che scavalca i controlli.
static const int MOSTRA_WIFI   = -7;
static const int MOSTRA_ORDINE = -8;
static const int MOSTRA_VINO   = -9;
static const int NIENTE        = -1;   // nessuna richiesta in sospeso

// L'ordine arrivato dal telefono. Otto righe bastano per un tavolo: se ne
// ordinano di piu' le ultime non si vedrebbero comunque sullo schermo.
static const int MAX_RIGHE = 8;
char ordine[MAX_RIGHE][28];
int  nOrdine = 0;

// Da che parte stanno i tasti fisici rispetto allo schermo in verticale.
// Se la freccia punta dalla parte sbagliata, e' l'unica riga da cambiare.
static const bool TASTI_A_DESTRA = true;

// La schermata del WiFi e' un suggerimento, non una destinazione: passati questi
// secondi il tavolo torna al suo QR del menu da solo, senza che nessuno debba
// dirglielo. Se restasse li', il QR del menu sparirebbe per il resto della cena.
// QUANTO RESTANO LE SCHERMATE SPECIALI prima di tornare al QR del menu.
// Sono i due numeri da cambiare se durano troppo o troppo poco.
static const uint32_t DURATA_WIFI_MS   = 30000;   // 30 s
static const uint32_t DURATA_ORDINE_MS = 60000;   // 60 s
static const uint32_t DURATA_VINO_MS   = 45000;   // 45 s

char vinoNome[40]   = "";
char vinoMotivo[40] = "";

int      copertiDopo = 1;      // a cosa tornare quando la speciale finisce
uint32_t specialeDa  = 0;      // da quando e' sullo schermo (0 = nessuna)

static bool eSpeciale(int c) {
  return c == MOSTRA_WIFI || c == MOSTRA_ORDINE || c == MOSTRA_VINO;
}
static uint32_t durataSpeciale(int c) {
  if (c == MOSTRA_ORDINE) return DURATA_ORDINE_MS;
  if (c == MOSTRA_VINO)   return DURATA_VINO_MS;
  return DURATA_WIFI_MS;
}

bool     forzaRidisegno  = false;   // testo cambiato: ridisegna anche a parita' di coperti
int      copertiMostrati = -1;      // -1 = niente disegnato ancora
int      copertiChiesti  = NIENTE;
uint32_t ultimoDisegno   = 0;

/* Scrive una riga centrata in orizzontale e restituisce dove finisce. */
static int centrata(const char *testo, const GFXfont *font, int y) {
  int16_t x1, y1; uint16_t w, h;
  epd.setFont(font);
  epd.getTextBounds(testo, 0, y, &x1, &y1, &w, &h);
  epd.setCursor((epd.width() - (int)w) / 2, y);
  epd.print(testo);
  return y + h;
}

/* Il QR centrato dentro una fascia alta <budget> px che parte da <yAlto>.
   Il budget comprende la ZONA DI SILENZIO: lo standard QR vuole 4 moduli di
   bianco tutto attorno, e senza quella i telefoni vedono il codice ma non lo
   agganciano. Prima ne lasciavo 8 pixel fissi, cioe' meno della meta'. */
static void disegnaQR(const char *testo, int yAlto, int budget) {
  QRCode qr;
  uint8_t dati[qrcode_getBufferSize(VERSIONE_QR_MAX)];
  int v = versionePer(testo);
  if (!v || qrcode_initText(&qr, dati, v, ECC_MEDIUM, testo) != 0 || !qr.size) {
    Serial.printf("[qr] testo troppo lungo: %d caratteri\n", strlen(testo));
    centrata("QR KO", &FreeMonoBold18pt7b, yAlto + 40);
    return;
  }

  int scala = budget / (qr.size + 8);        // +8 = i 4 moduli per lato
  if (scala < 1) scala = 1;
  const int lato    = qr.size * scala;
  const int margine = 4 * scala;
  const int x = (epd.width() - lato) / 2;
  const int y = yAlto + margine;

  epd.fillRect(x - margine, y - margine,
               lato + 2 * margine, lato + 2 * margine, GxEPD_WHITE);
  for (uint8_t r = 0; r < qr.size; r++)
    for (uint8_t c = 0; c < qr.size; c++)
      if (qrcode_getModule(&qr, c, r))
        epd.fillRect(x + c * scala, y + r * scala, scala, scala, GxEPD_BLACK);

  Serial.printf("[qr] versione %d, %d moduli, scala %d, lato %d px\n",
                v, qr.size, scala, lato);
}

// Il pannello e' in verticale: 300 px di larghezza per 400 di altezza.
static void schermoLibero() {
  centrata(NOME_TAVOLO, &FreeMonoBold24pt7b, 150);
  centrata(ORARIO,      &FreeMono12pt7b,     200);
  centrata(OSPITE,      &FreeMonoBold24pt7b, 260);
}

static void schermoBenvenuto() {
  centrata("Benvenuto", &FreeMonoBold18pt7b, 42);
  centrata(OSPITE,      &FreeMonoBold24pt7b, 82);

  disegnaQR(URL_MENU, 100, 240);           // con la zona di silenzio: 100..322

  centrata("Inquadra il QR code",  &FreeMono12pt7b, 352);
  centrata("per accedere al menu", &FreeMono12pt7b, 380);
}

static void schermoWifi() {
  centrata("Connessione",  &FreeMonoBold18pt7b, 48);
  centrata("piu' veloce?", &FreeMonoBold18pt7b, 84);

  if (QR_WIFI[0]) disegnaQR(QR_WIFI, 100, 230);      // arriva a 330
  else            centrata("(rete non impostata)", &FreeMono12pt7b, 200);

  centrata("Inquadra per entrare", &FreeMono12pt7b, 358);
  centrata("nel WiFi del locale",  &FreeMono12pt7b, 386);
}

static void schermoOrdine() {
  centrata("Ordine inviato", &FreeMonoBold18pt7b, 52);
  centrata("in cucina",      &FreeMonoBold18pt7b, 88);
  epd.drawFastHLine(30, 106, epd.width() - 60, GxEPD_BLACK);

  int y = 140;
  epd.setFont(&FreeMono9pt7b);
  for (int i = 0; i < nOrdine && y < 320; i++) {
    epd.setCursor(24, y);
    epd.print(ordine[i]);
    y += 26;
  }
  if (!nOrdine) centrata("(nessuna portata)", &FreeMono9pt7b, 160);

  epd.drawFastHLine(30, 332, epd.width() - 60, GxEPD_BLACK);
  centrata("premi MENU per",    &FreeMono12pt7b, 362);
  centrata("tornare al codice", &FreeMono12pt7b, 388);

  // La freccia punta al tasto fisico: lo schermo non e' touch, quindi
  // "clicca qui" non esiste e indicare il pulsante e' l'unica cosa onesta.
  const int y0 = 372;
  if (TASTI_A_DESTRA) {
    epd.fillTriangle(epd.width() - 8, y0, epd.width() - 26, y0 - 11,
                     epd.width() - 26, y0 + 11, GxEPD_BLACK);
  } else {
    epd.fillTriangle(8, y0, 26, y0 - 11, 26, y0 + 11, GxEPD_BLACK);
  }
}

/* Spezza un testo in due righe senza tagliare le parole a meta'. */
static void dueRighe(const char *testo, int larghezza, int y1, int y2,
                     const GFXfont *font) {
  int n = strlen(testo);
  if (n <= larghezza) { centrata(testo, font, y1); return; }
  int taglio = larghezza;
  while (taglio > 0 && testo[taglio] != ' ') taglio--;
  if (!taglio) taglio = larghezza;
  char a[48], b[48];
  snprintf(a, sizeof(a), "%.*s", taglio, testo);
  snprintf(b, sizeof(b), "%s", testo + taglio + 1);
  centrata(a, font, y1);
  centrata(b, font, y2);
}

static void schermoVino() {
  centrata("Con questi piatti", &FreeMono12pt7b, 60);
  centrata("consigliamo",       &FreeMono12pt7b, 88);

  // Il nome del vino e' la cosa che deve leggersi da lontano.
  dueRighe(vinoNome, 13, 150, 192, &FreeMonoBold18pt7b);

  epd.drawFastHLine(60, 232, epd.width() - 120, GxEPD_BLACK);
  dueRighe(vinoMotivo, 25, 272, 298, &FreeMono12pt7b);

  centrata("chiedilo al cameriere", &FreeMono9pt7b, 360);
}

static void disegna(int coperti) {
  epd.setRotation(1);                       // 400x300 -> 300x400, verticale
  epd.setTextColor(GxEPD_BLACK);
  epd.setFullWindow();
  epd.firstPage();
  do {
    epd.fillScreen(GxEPD_WHITE);
    if (coperti == MOSTRA_VINO)      schermoVino();
    else if (coperti == MOSTRA_ORDINE) schermoOrdine();
    else if (coperti == MOSTRA_WIFI) schermoWifi();
    else if (coperti > 0)       schermoBenvenuto();
    else                        schermoLibero();
  } while (epd.nextPage());
  epd.hibernate();

  copertiMostrati = coperti;
  specialeDa = eSpeciale(coperti) ? millis() : 0;
  forzaRidisegno = false;
  ultimoDisegno = millis();
  Serial.printf("[pannello] disegnato: %d coperti\n", coperti);
}

/* Una riga arrivata dal nodo radar. */
static void comando(const char *r) {
  // Col nodo spento il filo resta flottante e raccoglie disturbi: arrivano byte
  // casuali che sembrano righe. Finche' e' rumore illeggibile fa solo sporcizia
  // nei log, ma prima o poi il caso produce una sequenza tipo "N:" e mi
  // riscrive il nome del tavolo. Quindi accetto solo testo stampabile.
  for (const char *c = r; *c; c++)
    if (*c < 32 || *c > 126) return;

  if (r[0] == 'T' && r[1] == ':') {
    int n = atoi(r + 2);
    // Il nodo ripete lo stato ogni 15 secondi per riallineare il pannello dopo
    // un riavvio. Ma se e' in corso una schermata speciale, quella ripetizione
    // la spazzerebbe via a meta': il WiFi durava 15 secondi invece di 30 e
    // l'ordine spariva da solo. Qui me lo segno e basta; ci torno a tempo
    // scaduto. La 'L' invece passa sempre: il tavolo liberato e' un fatto.
    if (eSpeciale(copertiMostrati)) copertiDopo = (n > 0) ? n : 1;
    else                            copertiChiesti = n;
  } else if (r[0] == 'L' && r[1] == 0) {
    copertiChiesti = 0;
  } else if (r[0] == '?') {
    Serial1.printf("OK:%d\n", copertiMostrati);
  } else if (r[0] == 'N' && r[1] == ':') {
    strncpy(NOME_TAVOLO, r + 2, sizeof(NOME_TAVOLO) - 1);
    ricorda(); forzaRidisegno = true;
  } else if (r[0] == 'O' && r[1] == ':') {
    strncpy(ORARIO, r + 2, sizeof(ORARIO) - 1);
    ricorda(); forzaRidisegno = true;
  } else if (r[0] == 'U' && r[1] == ':') {
    strncpy(URL_MENU, r + 2, sizeof(URL_MENU) - 1);
    ricorda(); forzaRidisegno = true;
  } else if (r[0] == 'Q' && r[1] == ':') {
    strncpy(QR_WIFI, r + 2, sizeof(QR_WIFI) - 1);
    ricorda();
    Serial.println("[link] <- Q: (rete wifi aggiornata)");   // la chiave non si stampa
    return;
  } else if (r[0] == 'B' && r[1] == ':') {
    strncpy(vinoNome, r + 2, sizeof(vinoNome) - 1);
  } else if (r[0] == 'C' && r[1] == ':') {
    strncpy(vinoMotivo, r + 2, sizeof(vinoMotivo) - 1);
  } else if (r[0] == 'Q' && r[1] == 0) {
    // "rimetti il QR": stessa cosa del tasto MENU, ma dalla cassa.
    specialeDa = 0;
    copertiChiesti = eSpeciale(copertiMostrati)
                     ? copertiDopo
                     : (copertiMostrati > 0 ? copertiMostrati : 1);
  } else if (r[0] == 'Y' && r[1] == 0) {
    copertiDopo = (copertiMostrati > 0) ? copertiMostrati : 1;
    copertiChiesti = MOSTRA_VINO;
  } else if (r[0] == 'Z' && r[1] == 0) {
    nOrdine = 0;                                 // nuovo ordine in arrivo
  } else if (r[0] == 'V' && r[1] == ':') {
    if (nOrdine < MAX_RIGHE) {
      strncpy(ordine[nOrdine], r + 2, sizeof(ordine[0]) - 1);
      ordine[nOrdine][sizeof(ordine[0]) - 1] = 0;
      nOrdine++;
    }
  } else if (r[0] == 'X' && r[1] == 0) {
    copertiDopo = (copertiMostrati > 0) ? copertiMostrati : 1;
    copertiChiesti = MOSTRA_ORDINE;
  } else if (r[0] == 'W' && r[1] == 0) {
    copertiDopo = (copertiMostrati > 0) ? copertiMostrati : 1;
    copertiChiesti = MOSTRA_WIFI;
  } else if (r[0] == 'G' && r[1] == ':') {
    strncpy(OSPITE, r + 2, sizeof(OSPITE) - 1);
    ricorda(); forzaRidisegno = true;
  } else {
    Serial.printf("[link] non capisco: %s\n", r);
    return;
  }
  Serial.printf("[link] <- %s\n", r);
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== leggimenu - pannello 4.2\" ===");

  memoria.begin("leggimenu", false);
  rileggi();
  Serial.printf("tavolo: \"%s\" / \"%s\" / \"%s\"\n", NOME_TAVOLO, ORARIO, OSPITE);

  pinMode(PIN_PWR, OUTPUT);
  digitalWrite(PIN_PWR, HIGH);              // senza questo il pannello e' muto
  delay(500);                               // dagli tempo di alimentarsi davvero

  SPI.begin(PIN_CLK, -1, PIN_MOSI, PIN_CS); // i pin SPI di questa scheda non
  epd.init(115200, true, 50, false);        // sono quelli di default dell'S3

  for (int i = 0; i < N_TASTI; i++) pinMode(TASTI[i], INPUT_PULLUP);

  Serial1.begin(115200, SERIAL_8N1, PIN_RX_NODO, PIN_TX_NODO);
  // A nodo spento il filo resta a mezz'aria e raccoglie disturbi, quindi ci
  // metto una resistenza di richiamo. ATTENZIONE: qui NON si puo' usare
  // pinMode(INPUT_PULLUP): su ESP32 quello riconfigura il pin come GPIO normale
  // e lo stacca dalla seriale, uccidendo la ricezione. Ci ho perso un giro.
  // gpio_set_pull_mode tocca solo la resistenza e lascia il pin alla UART.
  gpio_set_pull_mode((gpio_num_t)PIN_RX_NODO, GPIO_PULLUP_ONLY);
  Serial.printf("ascolto il nodo su IO%d, rispondo su IO%d\n",
                PIN_RX_NODO, PIN_TX_NODO);

  disegna(0);                               // parto da "libero": si vede che e' vivo
}

void loop() {
  static char riga[64];
  static size_t n = 0;

  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (n) { riga[n] = 0; comando(riga); n = 0; }
    } else if (n < sizeof(riga) - 1) {
      riga[n++] = c;
    }
  }

  // I tasti della scheda. MENU rimette il QR del menu in qualunque momento:
  // e' la via d'uscita se il pannello e' finito su una schermata che il cliente
  // non voleva, o se qualcuno arriva a tavola dopo ed e' gia' scaduto tutto.
  if (millis() - ultimoTasto > 300) {
    for (int i = 0; i < N_TASTI; i++) {
      bool giu = (digitalRead(TASTI[i]) == LOW);
      if (giu && !tastoGiu[i]) {
        ultimoTasto = millis();
        Serial.printf("[tasto] IO%d premuto\n", TASTI[i]);
        if (TASTI[i] == PIN_MENU) {
          specialeDa = 0;                               // annullo l'attesa
          // Se sto mostrando una schermata speciale, copertiMostrati e' un
          // valore in codice e non un numero di persone: devo tornare a quello
          // che mi ero segnato, altrimenti il tavolo da tre diventa da uno.
          copertiChiesti = eSpeciale(copertiMostrati)
                           ? copertiDopo
                           : (copertiMostrati > 0 ? copertiMostrati : 1);
          forzaRidisegno = true;
          Serial.println("[tasto] MENU: rimetto il QR del menu");
        }
      }
      tastoGiu[i] = giu;
    }
  }

  // Ridisegno solo se e' cambiato davvero e non troppo di frequente.
  if (specialeDa && millis() - specialeDa > durataSpeciale(copertiMostrati)) {
    copertiChiesti = copertiDopo;              // il tempo e' scaduto
    specialeDa = 0;
    Serial.println("[pannello] schermata speciale finita, torno al menu");
  }

  // ATTENZIONE al confronto: "mostra il WiFi" e "nessuna richiesta" sono due
  // valori negativi diversi. Qui c'era un >= 0 scritto per escludere il secondo,
  // che silenziosamente buttava via anche il primo: il comando arrivava, veniva
  // registrato, e non succedeva niente. Confrontare con NIENTE, non col segno.
  bool daRifare = forzaRidisegno ||
                  (copertiChiesti != NIENTE && copertiChiesti != copertiMostrati);
  if (daRifare && millis() - ultimoDisegno > MIN_FRA_DISEGNI_MS) {
    int cosa = (copertiChiesti != NIENTE) ? copertiChiesti
                                          : (copertiMostrati > 0 ? copertiMostrati : 0);
    disegna(cosa);
  }
}
