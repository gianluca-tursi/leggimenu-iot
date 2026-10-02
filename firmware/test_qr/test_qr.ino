/*
 * leggimenu - PASSO 2: il QR sul display
 *
 * Disegna sul pannello e-ink quello che vedra' il cliente al tavolo:
 * barra col nome del tavolo, invito, e il QR da inquadrare.
 * Nessun Wi-Fi, nessun server: il QR lo genera la scheda da sola.
 *
 * CrowPanel ESP32-S3 2.13" (122x250, SSD1680).
 * Carica con:  ./scripts/flash.sh firmware/test_qr
 */

#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
// Generatore QR di Richard Moore (MIT), copiato qui dentro come qrgen.*:
// il core ESP32 ha un suo qrcode.h che vincerebbe sull'include, e la sua
// libreria non e' linkabile nelle build Arduino.
extern "C" {
#include "qrgen.h"
}

// --- cosa mostrare ------------------------------------------------------
// Quando ci sara' il server, qui arrivera' l'URL di sessione via WebSocket.
static const char *URL_MENU = "https://leggimenu.it";
static const char *NOME_TAVOLO = "Tavolo 7";
static const int   VERSIONE_QR = 3;   // 3 = 29 moduli, fino a 32 caratteri

// --- pin del display, dal readme ufficiale Elecrow del 2.13" ------------
#define EPD_SCK   12
#define EPD_MOSI  11
#define EPD_RST   10
#define EPD_DC    13
#define EPD_CS    14
#define EPD_BUSY   9
// Non documentati nella wiki, li ho trovati nel main.ino di Elecrow:
// senza IO7 alto il pannello non e' proprio alimentato e BUSY non risponde.
#define EPD_POWER  7    // alimentazione del pannello
#define LED_POWER 19    // LED di accensione della scheda

GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
    GxEPD2_213_BN(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void disegna() {
  const int W = display.width();
  const int H = display.height();

  // --- QR ---------------------------------------------------------------
  QRCode qr;
  uint8_t dati[qrcode_getBufferSize(VERSIONE_QR)];
  qrcode_initText(&qr, dati, VERSIONE_QR, ECC_MEDIUM, URL_MENU);

  int scala = (H - 8) / qr.size;          // il piu' grande che ci sta in altezza
  if (scala < 1) scala = 1;
  const int lato = qr.size * scala;
  const int qrX = W - lato - 4;
  const int qrY = (H - lato) / 2;

  // --- colonna di sinistra ---------------------------------------------
  const int colW = qrX - 8;

  display.fillRect(0, 0, colW, 22, GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(5, 17);
  display.print(NOME_TAVOLO);

  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(5, 48);
  display.print("Il menu");
  display.setCursor(5, 66);
  display.print("e' qui");

  display.setFont(&FreeSans9pt7b);
  display.setCursor(5, 92);
  display.print("Inquadra");
  display.setCursor(5, 110);
  display.print("il codice");

  // cornice leggera attorno al QR, aiuta l'occhio a capire dove guardare
  display.drawRect(qrX - 3, qrY - 3, lato + 6, lato + 6, GxEPD_BLACK);
  for (uint8_t y = 0; y < qr.size; y++) {
    for (uint8_t x = 0; x < qr.size; x++) {
      if (qrcode_getModule(&qr, x, y)) {
        display.fillRect(qrX + x * scala, qrY + y * scala, scala, scala, GxEPD_BLACK);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nleggimenu - disegno il QR sul pannello...");

  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);       // accende il pannello
  pinMode(LED_POWER, OUTPUT);
  digitalWrite(LED_POWER, HIGH);
  delay(100);

  display.init(115200, true, 2, false);
  // Il CrowPanel non usa i pin SPI di default dell'S3: li rimappo.
  SPI.end();
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);

  display.setRotation(1);               // orizzontale: 250 x 122
  Serial.printf("pannello: %d x %d\n", display.width(), display.height());

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    disegna();
  } while (display.nextPage());

  display.hibernate();                  // l'immagine resta anche senza corrente
  Serial.println("fatto: guarda il display.");
  Serial.printf("il QR punta a: %s\n", URL_MENU);
}

void loop() {}
