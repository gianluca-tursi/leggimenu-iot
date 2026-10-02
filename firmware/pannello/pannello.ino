/*
 * leggimenu - il pannello e-ink del tavolo (CrowPanel ESP32-S3 2.13")
 *
 * Non ha sensori: ascolta il nodo radar sul filo seriale e disegna quello che
 * gli viene detto. La divisione e' voluta - il radar deve girare a 10 Hz senza
 * mai fermarsi, e un refresh e-ink blocca tutto per due secondi buoni.
 *
 * FILO CON IL FREENOVE (nodo radar)
 *   Freenove GND  <-> CrowPanel GND    <- indispensabile, e' il riferimento
 *   Freenove 17   ->  CrowPanel IO41   (io ascolto qui)
 *   Freenove 4    <-  CrowPanel IO40   (ritorno, per le notifiche)
 *
 * PROTOCOLLO (righe di testo, apposta: si legge con un monitor seriale)
 *   T:<n>   tavolo aperto con n coperti
 *   L       tavolo libero
 *   ?       dimmi che stai bene
 *
 * Carica con:  ./scripts/flash.sh firmware/pannello
 */

#include <Arduino.h>
#include <string.h>
#include "EPD.h"

extern "C" {
#include "qrgen.h"
}

extern uint8_t ImageBW[];

// --- cosa mostrare ------------------------------------------------------
static const char *URL_MENU    = "https://leggimenu.it";
static const char *NOME_TAVOLO = "Tavolo 7";
static const int   VERSIONE_QR = 3;          // 29 moduli, fino a 32 caratteri

// --- filo verso il nodo radar -------------------------------------------
static const int PIN_RX_NODO = 41;
static const int PIN_TX_NODO = 40;

// --- pin non documentati, trovati nel main.ino di Elecrow ---------------
#define EPD_POWER  7          // alimentazione del pannello: senza, BUSY non risponde
#define LED_POWER 19

// Nel driver Elecrow il parametro colore di EPD_DrawPoint ha il significato
// ROVESCIATO rispetto al nome: EPD_ShowChar chiama EPD_DrawPoint(x, y, !color).
#define PIX_NERO   WHITE      // 0xFF -> pixel nero. Si', davvero.

// L'e-ink non si puo' ridisegnare a raffica: ogni refresh completo costa due
// secondi e consuma. Sotto questo intervallo ignoro i cambi.
static const uint32_t MIN_FRA_DISEGNI_MS = 4000;

int      copertiMostrati = -1;      // -1 = niente disegnato ancora
int      copertiChiesti  = -1;
uint32_t ultimoDisegno   = 0;

static void quadrato(int x, int y, int lato, uint8_t colore) {
  for (int dy = 0; dy < lato; dy++)
    for (int dx = 0; dx < lato; dx++)
      EPD_DrawPoint(x + dx, y + dy, colore);
}

/* Il QR, centrato in orizzontale, con l'angolo alto a sinistra in (x, y). */
static void disegnaQR(int y) {
  QRCode qr;
  uint8_t dati[qrcode_getBufferSize(VERSIONE_QR)];
  if (qrcode_initText(&qr, dati, VERSIONE_QR, ECC_MEDIUM, URL_MENU) != 0 || !qr.size) {
    Serial.println("[qr] generazione FALLITA");
    EPD_ShowString(4, y + 20, "QR KO", BLACK, 24);
    return;
  }
  int scala = (EPD_W - 20) / qr.size;          // 122 - margini
  if (scala < 1) scala = 1;
  const int lato = qr.size * scala;
  const int qrX  = (EPD_W - lato) / 2;

  EPD_DrawRectangle(qrX - 3, y - 3, qrX + lato + 2, y + lato + 2, PIX_NERO);
  for (uint8_t r = 0; r < qr.size; r++)
    for (uint8_t c = 0; c < qr.size; c++)
      if (qrcode_getModule(&qr, c, r))
        quadrato(qrX + c * scala, y + r * scala, scala, PIX_NERO);
}

// Il pannello e' verticale: 122 px di larghezza per 250 di altezza. Con il
// corpo 24 ci stanno 10 caratteri per riga, col 16 ne stanno 15, col 12 venti.
// Ogni riga qui sotto e' stata contata: se allunghi un testo, ricontalo.
static void schermoLibero() {
  memset(ImageBW, 0xFF, ALLSCREEN_BYTES);
  EPD_ShowString(6,  96, NOME_TAVOLO, BLACK, 24);
  EPD_ShowString(6, 136, "libero",    BLACK, 16);
}

static void schermoBenvenuto(int coperti) {
  memset(ImageBW, 0xFF, ALLSCREEN_BYTES);

  EPD_ShowString(4, 10, "Benvenuto", BLACK, 24);
  EPD_ShowString(4, 48, "Scansiona", BLACK, 16);
  EPD_ShowString(4, 66, "il QR per", BLACK, 16);
  EPD_ShowString(4, 84, "ordinare",  BLACK, 16);

  disegnaQR(112);                              // 87 px di lato: arriva a 199

  char riga[24];
  snprintf(riga, sizeof(riga), "%d %s", coperti, coperti == 1 ? "coperto" : "coperti");
  EPD_ShowString(4, 212, NOME_TAVOLO, BLACK, 12);
  EPD_ShowString(4, 228, riga,        BLACK, 12);
}

static void disegna(int coperti) {
  EPD_Init();
  EPD_Clear();                 // senza questa resta il fantasma di prima
  EPD_Update();

  if (coperti > 0) schermoBenvenuto(coperti);
  else             schermoLibero();

  EPD_DisplayImage(ImageBW);
  EPD_Update();
  EPD_Sleep();

  copertiMostrati = coperti;
  ultimoDisegno = millis();
  Serial.printf("[pannello] disegnato: %d coperti\n", coperti);
}

/* Una riga arrivata dal nodo radar. */
static void comando(const char *r) {
  if (r[0] == 'T' && r[1] == ':')      copertiChiesti = atoi(r + 2);
  else if (r[0] == 'L')                copertiChiesti = 0;
  else if (r[0] == '?')                Serial1.printf("OK:%d\n", copertiMostrati);
  else { Serial.printf("[link] non capisco: %s\n", r); return; }
  Serial.printf("[link] <- %s\n", r);
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== leggimenu - pannello e-ink ===");

  pinMode(EPD_POWER, OUTPUT); digitalWrite(EPD_POWER, HIGH);
  pinMode(LED_POWER, OUTPUT); digitalWrite(LED_POWER, HIGH);
  delay(100);

  Serial1.begin(115200, SERIAL_8N1, PIN_RX_NODO, PIN_TX_NODO);
  Serial.printf("ascolto il nodo su IO%d, rispondo su IO%d\n",
                PIN_RX_NODO, PIN_TX_NODO);

  disegna(0);                  // parto da "libero", cosi' si vede che e' vivo
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

  // Ridisegno solo se e' cambiato davvero e non troppo di frequente.
  if (copertiChiesti >= 0 && copertiChiesti != copertiMostrati &&
      millis() - ultimoDisegno > MIN_FRA_DISEGNI_MS) {
    disegna(copertiChiesti);
  }
}
