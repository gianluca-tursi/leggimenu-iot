/*
 * leggimenu - trova da solo dove e' attaccato il radar
 *
 * Non sappiamo con certezza da che parte cominci la numerazione del
 * connettore GPIO_D, quindi invece di smontare i fili lo chiediamo alla
 * scheda. Per ogni pin candidato:
 *   1. guarda il livello elettrico e conta le transizioni
 *      (una UART viva a 256000 baud ne fa migliaia; un pin scollegato no)
 *   2. ci apre sopra una seriale e conta i byte e i frame validi
 */

#include <Arduino.h>

static const int CANDIDATI[] = {40, 41, 3, 9, 15, 17, 21, 38};
static const int N_CAND = sizeof(CANDIDATI) / sizeof(CANDIDATI[0]);
static const uint32_t BAUD = 256000;

static const uint8_t HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};

struct Esito { int pin; uint32_t transizioni; int alto; uint32_t byte_; uint32_t frame; };

static uint32_t conta_transizioni(int pin, uint32_t campioni) {
  pinMode(pin, INPUT);
  delayMicroseconds(200);
  int prec = digitalRead(pin);
  uint32_t t = 0;
  for (uint32_t i = 0; i < campioni; i++) {
    int v = digitalRead(pin);
    if (v != prec) { t++; prec = v; }
  }
  return t;
}

static void prova_uart(int pin, uint32_t ms, uint32_t &byteTot, uint32_t &frameTot) {
  byteTot = 0; frameTot = 0;
  Serial1.begin(BAUD, SERIAL_8N1, pin, -1);
  uint8_t finestra[4] = {0};
  uint32_t fine = millis() + ms;
  while (millis() < fine) {
    while (Serial1.available()) {
      uint8_t b = Serial1.read();
      byteTot++;
      finestra[0] = finestra[1]; finestra[1] = finestra[2];
      finestra[2] = finestra[3]; finestra[3] = b;
      if (memcmp(finestra, HEADER, 4) == 0) frameTot++;
    }
  }
  Serial1.end();
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== leggimenu - caccia al radar ===");
  Serial.println("Cerco su quale pin arriva il TX dell'LD2450.\n");

  Esito best = {-1, 0, 0, 0, 0};
  for (int i = 0; i < N_CAND; i++) {
    int pin = CANDIDATI[i];
    uint32_t tr = conta_transizioni(pin, 40000);
    int alto = digitalRead(pin);

    // Con il pull-up interno distinguo i due casi:
    //  - resta BASSO  -> qualcosa lo tiene giu': filo collegato, radar spento
    //  - diventa ALTO -> il pin e' libero: su questo filo non c'e' niente
    pinMode(pin, INPUT_PULLUP);
    delay(2);
    int conPullup = digitalRead(pin);
    pinMode(pin, INPUT);
    uint32_t by = 0, fr = 0;
    prova_uart(pin, 900, by, fr);

    const char *stato = conPullup ? "LIBERO (niente attaccato)"
                                  : "TENUTO BASSO (c'e' un filo)";
    Serial.printf("IO%-2d  riposo=%-5s  %-26s  transizioni=%-6lu  byte=%-5lu  header=%lu %s\n",
                  pin, alto ? "ALTO" : "basso", stato,
                  (unsigned long)tr, (unsigned long)by, (unsigned long)fr,
                  fr > 0 ? "  <<< E' QUESTO" : (by > 0 ? "  (dati non LD2450)" : ""));

    if (fr > best.frame || (fr == best.frame && by > best.byte_)) {
      best = {pin, tr, alto, by, fr};
    }
  }

  Serial.println();
  if (best.frame > 0) {
    Serial.printf(">>> TROVATO: il radar parla su IO%d\n", best.pin);
  } else if (best.byte_ > 0) {
    Serial.printf(">>> Su IO%d arrivano byte ma non frame LD2450: baud sbagliato?\n", best.pin);
  } else {
    Serial.println(">>> Nessun pin riceve niente.");
    Serial.println("    Vuol dire quasi sempre che il radar NON e' alimentato:");
    Serial.println("    il 3V3 o il GND sono finiti sul pin sbagliato.");
    Serial.println("    Prova a spostare l'alimentazione sull'UART0 (GND e 5V),");
    Serial.println("    lasciando sul GPIO_D solo il filo dei dati.");
  }
}

void loop() {}
