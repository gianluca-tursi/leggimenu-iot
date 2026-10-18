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
char URL_MENU[64]       = "https://leggimenu.it";
char URL_RECENSIONE[64] = "https://g.page/r/trattoria-demo/review";
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
  memoria.putString("recens", URL_RECENSIONE);
  memoria.putString("wifi",   QR_WIFI);      // contiene la chiave: non stamparla
}

static void rileggi() {
  memoria.getString("nome",   NOME_TAVOLO, sizeof(NOME_TAVOLO));
  memoria.getString("orario", ORARIO,      sizeof(ORARIO));
  memoria.getString("ospite", OSPITE,      sizeof(OSPITE));
  memoria.getString("url",    URL_MENU,    sizeof(URL_MENU));
  memoria.getString("recens", URL_RECENSIONE, sizeof(URL_RECENSIONE));
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
static const int MOSTRA_ANNUNCIO = -10;
static const int MOSTRA_RECENSIONE = -11;

// Nota: chiedere una schermata speciale alza SEMPRE forzaRidisegno. Il
// confronto che decide se ridisegnare guarda il numero di coperti, e due
// annunci diversi hanno lo stesso numero: senza, la seconda offerta non
// comparirebbe mai perche' "e' gia' a video un annuncio".
static const int NIENTE        = -1;   // nessuna richiesta in sospeso

// L'ordine arrivato dal telefono. Otto righe bastano per un tavolo: se ne
// ordinano di piu' le ultime non si vedrebbero comunque sullo schermo.
static const int MAX_RIGHE = 8;
char ordine[MAX_RIGHE][52];   // fino a due righe per piatto
int  nOrdine = 0;

// Da che parte stanno i tasti fisici rispetto allo schermo in verticale.
// Se la freccia punta dalla parte sbagliata, e' l'unica riga da cambiare.
static const bool TASTI_A_DESTRA = true;

// La schermata del WiFi e' un suggerimento, non una destinazione: passati questi
// secondi il tavolo torna al suo QR del menu da solo, senza che nessuno debba
// dirglielo. Se restasse li', il QR del menu sparirebbe per il resto della cena.
// QUANTO RESTANO LE SCHERMATE SPECIALI prima di tornare al QR del menu.
// Sono i due numeri da cambiare se durano troppo o troppo poco.
static const uint32_t DURATA_WIFI_MS   = 15000;   // 15 s
static const uint32_t DURATA_ORDINE_MS = 15000;   // 15 s
static const uint32_t DURATA_VINO_MS   = 30000;   // 30 s

char vinoNome[40]   = "";
char vinoMotivo[40] = "";

// Un annuncio dalla cassa: offerta del giorno, evento in arrivo. Tre righe,
// perche' su un tavolo nessuno legge un paragrafo.
static const uint32_t DURATA_ANNUNCIO_MS = 15000;   // offerte ed eventi

// Il vino arriva dal server un decimo di secondo dopo l'ordine. Se lo
// mostrassi subito, l'ordine non si vedrebbe mai: il pannello ridisegna al
// massimo ogni 4 secondi e disegna l'ultima cosa che gli e' arrivata. Quindi
// il vino aspetta che l'ordine abbia finito i suoi 15 secondi.
bool vinoInAttesa = false;
// La recensione resta finche' qualcuno non la toglie: il conto e' l'ultimo
// momento utile, e un QR che sparisce mentre uno cerca il telefono e' sprecato.
static const uint32_t DURATA_RECENSIONE_MS = 0;
char annTitolo[32] = "";
char annRiga1[34]  = "";
char annRiga2[34]  = "";

int      copertiDopo = 1;      // a cosa tornare quando la speciale finisce
uint32_t specialeDa  = 0;      // da quando e' sullo schermo (0 = nessuna)

static bool eSpeciale(int c) {
  return c == MOSTRA_WIFI || c == MOSTRA_ORDINE || c == MOSTRA_VINO
      || c == MOSTRA_ANNUNCIO || c == MOSTRA_RECENSIONE;
}
static uint32_t durataSpeciale(int c) {
  if (c == MOSTRA_ORDINE)   return DURATA_ORDINE_MS;
  if (c == MOSTRA_VINO)     return DURATA_VINO_MS;
  if (c == MOSTRA_ANNUNCIO)   return DURATA_ANNUNCIO_MS;
  if (c == MOSTRA_RECENSIONE) return DURATA_RECENSIONE_MS;
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
  int x = (epd.width() - (int)w) / 2;
  if (x < 4) x = 4;            // mai fuori dal bordo: meglio stretto che tagliato
  epd.setCursor(x, y);
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

static void stringiA(char *s, int largo);

static void schermoOrdine() {
  centrata("Ordine inviato", &FreeMonoBold18pt7b, 52);
  centrata("in cucina",      &FreeMonoBold18pt7b, 88);
  epd.drawFastHLine(30, 106, epd.width() - 60, GxEPD_BLACK);

  int y = 140;
  epd.setFont(&FreeMono9pt7b);
  const int largo  = epd.width() - 12 - 24;
  const int rientro = 33;                    // la seconda riga parte dopo "2x "
  for (int i = 0; i < nOrdine && y < 320; i++) {
    // Un piatto lungo va a capo una volta, all'ultimo spazio che ci sta;
    // se non basta, e' la seconda riga a essere troncata. Misuro sempre:
    // contare i caratteri a mano aveva fatto finire "cinghiale" a meta'.
    char a[56], b[56] = "";
    snprintf(a, sizeof(a), "%s", ordine[i]);
    int16_t bx, by; uint16_t bw, bh;
    epd.getTextBounds(a, 0, y, &bx, &by, &bw, &bh);
    if ((int)bw > largo && y + 20 < 320) {
      for (int k = strlen(a) - 1; k > 0; k--) {
        if (a[k] != ' ') continue;
        a[k] = 0;
        epd.getTextBounds(a, 0, y, &bx, &by, &bw, &bh);
        if ((int)bw <= largo) { snprintf(b, sizeof(b), "%s", a + k + 1); break; }
        a[k] = ' ';
      }
    }
    stringiA(a, largo);
    epd.setCursor(24, y);
    epd.print(a);
    if (b[0]) {
      stringiA(b, largo - rientro);
      epd.setCursor(24 + rientro, y + 20);
      epd.print(b);
      y += 46;
    } else {
      y += 26;
    }
  }
  if (!nOrdine) centrata("(nessuna portata)", &FreeMono9pt7b, 160);

  epd.drawFastHLine(30, 332, epd.width() - 60, GxEPD_BLACK);
  // Le righe stanno di fianco alla freccia, non sotto: in 12pt "per tornare
  // al menu" e' larga quasi quanto lo schermo, e centrata ci finirebbe sopra.
  const char *dici[2] = { "premi questo tasto", "per tornare al menu" };
  const int da = TASTI_A_DESTRA ? 4 : 30, fino = TASTI_A_DESTRA ? 270 : 296;
  epd.setFont(&FreeMono12pt7b);
  for (int k = 0; k < 2; k++) {
    int16_t bx, by; uint16_t bw, bh;
    epd.getTextBounds(dici[k], 0, 0, &bx, &by, &bw, &bh);
    int x = da + (fino - da - (int)bw) / 2;
    if (x < da) x = da;
    epd.setCursor(x, k ? 388 : 362);
    epd.print(dici[k]);
  }

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

/* Accorcia <s> a parole intere finche' sta in <largo> px col font attuale.
   Una parola sola troppo lunga la accorcia a lettere: non c'e' altro modo. */
static void stringiA(char *s, int largo) {
  int16_t bx, by; uint16_t bw, bh;
  while (strlen(s) > 1) {
    epd.getTextBounds(s, 0, 100, &bx, &by, &bw, &bh);
    if ((int)bw <= largo) return;
    char *sp = strrchr(s, ' ');
    if (sp && sp != s) *sp = 0;
    else s[strlen(s) - 1] = 0;
  }
}

/* Spezza un testo in due righe senza tagliare le parole a meta'.
   Misura davvero quanto occupa invece di contare i caratteri: contandoli
   avevo fatto uscire dallo schermo la V di "Va bene con formaggio e pepato". */
static void dueRighe(const char *testo, int /*non usato*/, int y1, int y2,
                     const GFXfont *font) {
  const int utile = epd.width() - 16;
  int16_t bx, by; uint16_t bw, bh;
  epd.setFont(font);
  epd.getTextBounds(testo, 0, y1, &bx, &by, &bw, &bh);
  if ((int)bw <= utile) { centrata(testo, font, y1); return; }

  // Arretro all'ultimo spazio che ci sta davvero.
  char a[64];
  int taglio = strlen(testo);
  while (taglio > 1) {
    while (taglio > 1 && testo[taglio] != ' ') taglio--;
    snprintf(a, sizeof(a), "%.*s", taglio, testo);
    epd.getTextBounds(a, 0, y1, &bx, &by, &bw, &bh);
    if ((int)bw <= utile) break;
    taglio--;
  }
  if (taglio <= 1) { centrata(testo, font, y1); return; }   // una parola sola
  centrata(a, font, y1);
  // La seconda riga, se ancora non ci sta, perde le parole in fondo: meglio
  // "Valpolicella" intero che "Valpolicella D" tagliato dal bordo.
  char b[64];
  snprintf(b, sizeof(b), "%s", testo + taglio + 1);
  stringiA(b, utile);
  centrata(b, font, y2);
}

/* Una riga dell'annuncio: in 12pt se ci sta, se no a capo in 9pt. */
static void rigaAnnuncio(const char *testo, int y) {
  int16_t bx, by; uint16_t bw, bh;
  epd.setFont(&FreeMono12pt7b);
  epd.getTextBounds(testo, 0, y, &bx, &by, &bw, &bh);
  if ((int)bw <= epd.width() - 16) centrata(testo, &FreeMono12pt7b, y);
  else dueRighe(testo, 0, y - 6, y + 12, &FreeMono9pt7b);
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

static void schermoAnnuncio() {
  dueRighe(annTitolo, 13, 70, 112, &FreeMonoBold18pt7b);
  epd.drawFastHLine(50, 146, epd.width() - 100, GxEPD_BLACK);
  if (annRiga1[0]) rigaAnnuncio(annRiga1, 188);
  if (annRiga2[0]) rigaAnnuncio(annRiga2, 218);

  // Il QR resta: l'annuncio e' un richiamo, il menu e' quello che serve.
  disegnaQR(URL_MENU, 246, 130);
}

static void schermoRecensione() {
  centrata("Grazie!",        &FreeMonoBold24pt7b, 62);
  centrata("Com'e' andata?", &FreeMono12pt7b,     98);

  disegnaQR(URL_RECENSIONE, 118, 200);

  centrata("Lasciaci una recensione", &FreeMono9pt7b, 352);
  centrata("ci aiuta davvero",        &FreeMono9pt7b, 378);
}

/* Solo il disegno, senza toccare lo stato: lo usa anche la demo. */
static void stampa(int coperti) {
  epd.setRotation(1);                       // 400x300 -> 300x400, verticale
  epd.setTextColor(GxEPD_BLACK);
  // Niente a capo automatico: una riga troppo lunga finirebbe sopra quella
  // dopo e si leggerebbero due piatti sovrapposti. Meglio troncarla.
  epd.setTextWrap(false);
  epd.setFullWindow();
  epd.firstPage();
  do {
    epd.fillScreen(GxEPD_WHITE);
    if (coperti == MOSTRA_RECENSIONE)     schermoRecensione();
    else if (coperti == MOSTRA_ANNUNCIO)  schermoAnnuncio();
    else if (coperti == MOSTRA_VINO) schermoVino();
    else if (coperti == MOSTRA_ORDINE) schermoOrdine();
    else if (coperti == MOSTRA_WIFI) schermoWifi();
    else if (coperti > 0)       schermoBenvenuto();
    else                        schermoLibero();
  } while (epd.nextPage());
  epd.hibernate();
}

static void disegna(int coperti) {
  stampa(coperti);
  copertiMostrati = coperti;
  specialeDa = eSpeciale(coperti) ? millis() : 0;
  forzaRidisegno = false;
  ultimoDisegno = millis();
  Serial.printf("[pannello] disegnato: %d coperti\n", coperti);
}

// --- demo a scorrimento -------------------------------------------------
// Il tasto EXIT fa girare tutte le schermate una dopo l'altra, con contenuti
// scritti qui. Non dipende da nodo, ponte o server: se in fiera salta la
// rete, il tavolo si puo' mostrare lo stesso. Si esce con EXIT o con MENU.
// Intanto i comandi veri continuano ad arrivare e a essere registrati: alla
// fine della demo il pannello torna a quello che deve mostrare davvero.
static const int      PIN_DEMO     = 1;        // EXIT
static const uint32_t DEMO_OGNI_MS = 12000;    // quanto resta ogni schermata
static const int DEMO_PASSI[] = { 0, 1, MOSTRA_WIFI, MOSTRA_ORDINE, MOSTRA_VINO,
                                  MOSTRA_ANNUNCIO, MOSTRA_ANNUNCIO,
                                  MOSTRA_RECENSIONE };
static const int N_DEMO = sizeof(DEMO_PASSI) / sizeof(DEMO_PASSI[0]);
bool     demoAttiva = false;
int      demoPasso  = 0;
uint32_t demoDa     = 0;       // 0 = la prima schermata non e' ancora uscita

static void disegnaDemo(int passo) {
  // Metto da parte i dati veri, disegno con quelli finti e li rimetto:
  // un ordine arrivato durante la demo non deve sparire.
  static char sOrd[MAX_RIGHE][52];
  static char sVN[40], sVM[40], sAT[32], sA1[34], sA2[34], sQW[80];
  int sN = nOrdine;
  memcpy(sOrd, ordine, sizeof(ordine));
  memcpy(sVN, vinoNome, sizeof(sVN));   memcpy(sVM, vinoMotivo, sizeof(sVM));
  memcpy(sAT, annTitolo, sizeof(sAT));  memcpy(sA1, annRiga1, sizeof(sA1));
  memcpy(sA2, annRiga2, sizeof(sA2));   memcpy(sQW, QR_WIFI, sizeof(sQW));

  nOrdine = 3;
  snprintf(ordine[0], sizeof(ordine[0]), "2x Tagliatelle al ragu di cinghiale");
  snprintf(ordine[1], sizeof(ordine[1]), "1x Carciofi alla romana");
  snprintf(ordine[2], sizeof(ordine[2]), "2x Tiramisu della casa");
  snprintf(vinoNome,   sizeof(vinoNome),   "Amarone della Valpolicella DOCG");
  snprintf(vinoMotivo, sizeof(vinoMotivo), "Regge il ragu di cinghiale");
  if (!QR_WIFI[0])
    snprintf(QR_WIFI, sizeof(QR_WIFI), "WIFI:T:WPA;S:leggimenu;P:demo-leggimenu;;");
  if (passo == 5) {                                    // l'offerta
    snprintf(annTitolo, sizeof(annTitolo), "Offerta del giorno");
    snprintf(annRiga1,  sizeof(annRiga1),  "Tiramisu della casa");
    snprintf(annRiga2,  sizeof(annRiga2),  "%s", "-20% solo oggi");  // % e' un segnaposto
  } else {                                             // l'evento
    snprintf(annTitolo, sizeof(annTitolo), "Serata degustazione");
    snprintf(annRiga1,  sizeof(annRiga1),  "venerdi ore 20:30");
    snprintf(annRiga2,  sizeof(annRiga2),  "inquadra il QR per i dettagli");
  }

  stampa(DEMO_PASSI[passo]);

  nOrdine = sN;
  memcpy(ordine, sOrd, sizeof(ordine));
  memcpy(vinoNome, sVN, sizeof(sVN));   memcpy(vinoMotivo, sVM, sizeof(sVM));
  memcpy(annTitolo, sAT, sizeof(sAT));  memcpy(annRiga1, sA1, sizeof(sA1));
  memcpy(annRiga2, sA2, sizeof(sA2));   memcpy(QR_WIFI, sQW, sizeof(sQW));

  ultimoDisegno = millis();
  Serial.printf("[demo] schermata %d di %d\n", passo + 1, N_DEMO);
}

/* Una riga arrivata dal nodo radar. */
static void comando(const char *r) {
  // Ogni comando vero porta un '*' davanti. Il filo scollegato raccoglie
  // disturbi, e senza questo segno il rumore ogni tanto componeva una "L" -
  // comando valido di una lettera sola - riportando il pannello a "tavolo
  // libero" da solo, in continuazione. Sembrava bloccato.
  if (r[0] != '*') return;
  r++;

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
    // Anche se la speciale e' solo IN ARRIVO (chiesta, non ancora disegnata):
    // il pannello ridisegna al massimo ogni 4 secondi, e se la ripetizione
    // dello stato cade in quella finestra cancellava l'ordine prima che
    // comparisse - lasciando il vino ad aspettare un ordine mai mostrato.
    if (eSpeciale(copertiMostrati) || eSpeciale(copertiChiesti))
      copertiDopo = (n > 0) ? n : 1;
    else
      copertiChiesti = n;
  } else if (r[0] == 'L' && r[1] == 0) {
    vinoInAttesa = false;                        // tavolo liberato: niente vino
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
  } else if (r[0] == 'E' && r[1] == ':') {
    strncpy(URL_RECENSIONE, r + 2, sizeof(URL_RECENSIONE) - 1);
    ricorda();
  } else if (r[0] == 'E' && r[1] == 0) {
    copertiDopo = (copertiMostrati > 0) ? copertiMostrati : 1;
    copertiChiesti = MOSTRA_RECENSIONE;
    forzaRidisegno = true;
  } else if (r[0] == 'D' && r[1] == ':') {
    // Tutto l'annuncio in una riga sola: "D:titolo|riga1|riga2".
    // Prima arrivava in tre comandi separati, e se uno si perdeva restava
    // mezzo annuncio vecchio sotto il titolo nuovo - un evento con sotto lo
    // sconto dell'offerta di prima.
    annTitolo[0] = annRiga1[0] = annRiga2[0] = 0;
    const char *p1 = r + 2;
    const char *b1 = strchr(p1, '|');
    const char *b2 = b1 ? strchr(b1 + 1, '|') : nullptr;
    size_t n0 = b1 ? (size_t)(b1 - p1) : strlen(p1);
    snprintf(annTitolo, sizeof(annTitolo), "%.*s", (int)n0, p1);
    if (b1) {
      size_t n1 = b2 ? (size_t)(b2 - b1 - 1) : strlen(b1 + 1);
      snprintf(annRiga1, sizeof(annRiga1), "%.*s", (int)n1, b1 + 1);
    }
    if (b2) snprintf(annRiga2, sizeof(annRiga2), "%s", b2 + 1);
    copertiDopo = (copertiMostrati > 0) ? copertiMostrati : 1;
    copertiChiesti = MOSTRA_ANNUNCIO;
    forzaRidisegno = true;
  } else if (r[0] == 'B' && r[1] == ':') {
    strncpy(vinoNome, r + 2, sizeof(vinoNome) - 1);
  } else if (r[0] == 'C' && r[1] == ':') {
    strncpy(vinoMotivo, r + 2, sizeof(vinoMotivo) - 1);
  } else if (r[0] == 'Q' && r[1] == 0) {
    // "rimetti il QR": stessa cosa del tasto MENU, ma dalla cassa.
    specialeDa = 0;
    vinoInAttesa = false;
    copertiChiesti = eSpeciale(copertiMostrati)
                     ? copertiDopo
                     : (copertiMostrati > 0 ? copertiMostrati : 1);
  } else if (r[0] == 'Y' && r[1] == 0) {
    if (copertiMostrati == MOSTRA_ORDINE || copertiChiesti == MOSTRA_ORDINE) {
      vinoInAttesa = true;                        // dopo l'ordine, non al posto suo
      Serial.println("[pannello] vino in attesa: prima finisce l'ordine");
    } else {
      // Mi segno i coperti solo se sto mostrando una schermata normale: se a
      // video c'e' un'altra speciale, copertiMostrati e' un codice e non un
      // numero di persone, e dopo il vino il tavolo da due diventerebbe da uno.
      if (!eSpeciale(copertiMostrati))
        copertiDopo = (copertiMostrati > 0) ? copertiMostrati : 1;
      copertiChiesti = MOSTRA_VINO;
      forzaRidisegno = true;
    }
  } else if (r[0] == 'Z' && r[1] == 0) {
    nOrdine = 0;                                 // nuovo ordine in arrivo
    vinoInAttesa = false;                        // il suo vino, se c'e', arriva dopo
  } else if (r[0] == 'V' && r[1] == ':') {
    if (nOrdine < MAX_RIGHE) {
      strncpy(ordine[nOrdine], r + 2, sizeof(ordine[0]) - 1);
      ordine[nOrdine][sizeof(ordine[0]) - 1] = 0;
      nOrdine++;
    }
  } else if (r[0] == 'X' && r[1] == 0) {
    copertiDopo = (copertiMostrati > 0) ? copertiMostrati : 1;
    copertiChiesti = MOSTRA_ORDINE;
    forzaRidisegno = true;
  } else if (r[0] == 'W' && r[1] == 0) {
    copertiDopo = (copertiMostrati > 0) ? copertiMostrati : 1;
    copertiChiesti = MOSTRA_WIFI;
    forzaRidisegno = true;
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
  // 128: l'annuncio arriva tutto su una riga sola (titolo|riga1|riga2), e
  // con 64 la coda si perdeva: "inquadra il QR per" e poi niente.
  static char riga[128];
  static size_t n = 0;

  // Gli stessi comandi arrivano anche dall'USB: serve per provare le
  // schermate con la scheda sul banco, senza rimontare il filo verso il nodo.
  while (Serial.available()) {
    static char rigaU[80]; static size_t nU = 0;
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (nU) { rigaU[nU] = 0; comando(rigaU); nU = 0; }
    } else if (nU < sizeof(rigaU) - 1) rigaU[nU++] = c;
  }

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
        if (TASTI[i] == PIN_DEMO) {
          demoAttiva = !demoAttiva;
          demoPasso = 0; demoDa = 0;
          if (!demoAttiva) forzaRidisegno = true;   // torno a quello vero
          Serial.println(demoAttiva ? "[tasto] EXIT: parte la demo"
                                    : "[tasto] EXIT: fine demo");
        }
        if (TASTI[i] == PIN_MENU) {
          demoAttiva = false;                           // MENU chiude anche la demo
          specialeDa = 0;                               // annullo l'attesa
          vinoInAttesa = false;
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

  // In demo comanda la demo: niente scadenze, niente ridisegni veri.
  // demoDa parte DOPO il disegno, cosi' ogni schermata si vede 12 s pieni.
  if (demoAttiva) {
    bool tocca = demoDa ? millis() - demoDa >= DEMO_OGNI_MS
                        : millis() - ultimoDisegno > MIN_FRA_DISEGNI_MS;
    if (tocca) {
      if (demoDa) demoPasso = (demoPasso + 1) % N_DEMO;
      disegnaDemo(demoPasso);
      demoDa = millis();
    }
    return;
  }

  // Ridisegno solo se e' cambiato davvero e non troppo di frequente.
  uint32_t quanto = durataSpeciale(copertiMostrati);
  if (specialeDa && quanto && millis() - specialeDa > quanto) {
    specialeDa = 0;
    if (copertiMostrati == MOSTRA_ORDINE && vinoInAttesa) {
      vinoInAttesa = false;                    // finito l'ordine, ora il vino
      copertiChiesti = MOSTRA_VINO;
      forzaRidisegno = true;
      Serial.println("[pannello] ordine finito, ora il vino");
    } else {
      copertiChiesti = copertiDopo;            // il tempo e' scaduto
      Serial.println("[pannello] schermata speciale finita, torno al menu");
    }
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
