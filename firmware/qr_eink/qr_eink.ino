/*
 * leggimenu - il QR sul pannello e-ink del CrowPanel 2.13"
 *
 * Usa il driver ufficiale Elecrow di questa scheda (EPD.cpp / EPD_Init.cpp /
 * spi.cpp, presi dal loro repo). GxEPD2 non va bene: questo pannello ha il
 * BUSY invertito (alto = libero) e i comandi del JD79661, non dell'SSD1680.
 *
 * Carica con:  ./scripts/flash.sh firmware/qr_eink
 */

#include <Arduino.h>
#include <string.h>
#include "EPD.h"

extern "C" {
#include "qrgen.h"          // generatore QR di Richard Moore (MIT)
}

extern uint8_t ImageBW[];   // framebuffer del driver Elecrow

// --- cosa mostrare ------------------------------------------------------
static const char *URL_MENU    = "https://leggimenu.it";
static const char *NOME_TAVOLO = "Tavolo 7";
static const int   VERSIONE_QR = 3;        // 29 moduli, fino a 32 caratteri

// --- pin non documentati, trovati nel main.ino di Elecrow ---------------
#define EPD_POWER  7        // alimentazione del pannello: senza, BUSY non risponde
#define LED_POWER 19        // LED di accensione

// ATTENZIONE: nel driver Elecrow il parametro colore di EPD_DrawPoint ha il
// significato ROVESCIATO rispetto al nome. Guarda EPD_ShowChar: per i pixel
// del testo chiama EPD_DrawPoint(x, y, !color). Nel framebuffer bit 0 = nero,
// bit 1 = bianco, e passare BLACK (0x00) mette il bit a 1, cioe' bianco.
// Quindi per disegnare inchiostro nero servono queste due, non BLACK/WHITE.
#define PIX_NERO   WHITE    // 0xFF -> pixel nero. Si', davvero.
#define PIX_BIANCO BLACK

static void quadrato(int x, int y, int lato, uint8_t colore) {
  for (int dy = 0; dy < lato; dy++)
    for (int dx = 0; dx < lato; dx++)
      EPD_DrawPoint(x + dx, y + dy, colore);
}

static void disegna() {
  memset(ImageBW, 0xFF, ALLSCREEN_BYTES);   // tutto bianco

  // --- QR a destra ------------------------------------------------------
  QRCode qr;
  uint8_t dati[qrcode_getBufferSize(VERSIONE_QR)];
  int esito = qrcode_initText(&qr, dati, VERSIONE_QR, ECC_MEDIUM, URL_MENU);
  Serial.printf("[qr] esito=%d size=%u buffer=%u\n",
                esito, (unsigned)qr.size, (unsigned)qrcode_getBufferSize(VERSIONE_QR));
  if (esito != 0 || qr.size == 0) {
    Serial.println("[qr] generazione FALLITA");
    EPD_ShowString(130, 50, "QR KO", BLACK, 24);
    return;
  }

  int scala = 118 / qr.size;
  if (scala < 1) scala = 1;
  const int lato = qr.size * scala;
  const int qrX  = EPD_W - lato - 5;
  const int qrY  = (122 - lato) / 2;

  Serial.printf("[qr] scala=%d lato=%d qrX=%d qrY=%d (EPD_W=%d)\n",
                scala, lato, qrX, qrY, EPD_W);
  EPD_DrawRectangle(qrX - 3, qrY - 3, qrX + lato + 2, qrY + lato + 2, PIX_NERO);
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        quadrato(qrX + x * scala, qrY + y * scala, scala, PIX_NERO);

  // --- colonna di sinistra ---------------------------------------------
  EPD_ShowString(4,  6, NOME_TAVOLO, BLACK, 24);
  EPD_ShowString(4, 42, "Il menu",   BLACK, 16);
  EPD_ShowString(4, 60, "e' qui",    BLACK, 16);
  EPD_ShowString(4, 88, "Inquadra",  BLACK, 12);
  EPD_ShowString(4, 102, "il codice", BLACK, 12);
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\nleggimenu - disegno il QR");

  pinMode(EPD_POWER, OUTPUT); digitalWrite(EPD_POWER, HIGH);
  pinMode(LED_POWER, OUTPUT); digitalWrite(LED_POWER, HIGH);
  delay(100);

  EPD_Init();
  EPD_Clear();
  EPD_Update();
  Serial.println("pannello pulito");

  disegna();
  EPD_DisplayImage(ImageBW);
  EPD_Update();

  Serial.printf("fatto. Il QR punta a: %s\n", URL_MENU);
  EPD_Sleep();
}

void loop() {}
