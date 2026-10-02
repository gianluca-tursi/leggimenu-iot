/*
 * leggimenu - verifica onesta del conteggio bersagli
 *
 * Stampa i BYTE GREZZI dei tre slot del frame LD2450, compresi quelli vuoti,
 * piu' la decodifica. Cosi' si vede cosa manda il radar senza doversi fidare
 * di come lo interpreto io.
 *
 * Tiene anche il massimo di bersagli visti da quando e' acceso: e' il numero
 * che serve per la prova con due persone.
 */
#include <Arduino.h>
#include <string.h>
#include <math.h>

static const int PIN_RX = 40;
static const uint32_t BAUD = 256000;
static const uint8_t HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t FOOTER[2] = {0x55, 0xCC};
static const size_t LUNG = 30;

uint8_t buf[256];
size_t usati = 0;
int maxVisti = 0;
uint32_t frameTot = 0, ultimaStampa = 0;
uint32_t istogramma[4] = {0, 0, 0, 0};   // quante volte 0,1,2,3 bersagli

static int coord(uint16_t g) { int v = g & 0x7FFF; return (g & 0x8000) ? v : -v; }

static void esamina(const uint8_t *f) {
  frameTot++;
  int n = 0;
  for (int s = 0; s < 3; s++) {
    const uint8_t *b = f + 4 + s * 8;
    uint16_t gx = b[0] | (b[1] << 8), gy = b[2] | (b[3] << 8), gv = b[4] | (b[5] << 8);
    if (gx || gy || gv) n++;
  }
  if (n < 4) istogramma[n]++;
  if (n > maxVisti) {
    maxVisti = n;
    Serial.printf("\n*** NUOVO MASSIMO: %d bersagli contemporanei ***\n\n", n);
  }

  if (millis() - ultimaStampa < 500) return;    // 2 stampe al secondo, leggibili
  ultimaStampa = millis();

  Serial.printf("n=%d | ", n);
  for (int s = 0; s < 3; s++) {
    const uint8_t *b = f + 4 + s * 8;
    uint16_t gx = b[0] | (b[1] << 8), gy = b[2] | (b[3] << 8), gv = b[4] | (b[5] << 8);
    if (!gx && !gy && !gv) { Serial.printf("S%d:vuoto        ", s + 1); continue; }
    int x = coord(gx) / 10, y = coord(gy) / 10, v = coord(gv);
    Serial.printf("S%d:x=%+4d y=%4d v=%+4d  ", s + 1, x, y, v);
  }
  Serial.print("| grezzi ");
  for (int i = 4; i < 28; i++) {
    Serial.printf("%02X", f[i]);
    if ((i - 3) % 8 == 0) Serial.print(" ");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== verifica bersagli LD2450 ===");
  Serial.println("Stampo i tre slot del frame, anche quelli vuoti, e i byte grezzi.");
  Serial.println("Muovetevi in due: se il radar ne vede due, compare S2 pieno.\n");
  Serial1.begin(BAUD, SERIAL_8N1, PIN_RX, -1);
}

void loop() {
  while (Serial1.available()) {
    if (usati >= sizeof(buf)) usati = 0;
    buf[usati++] = Serial1.read();
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

  static uint32_t ultimoRiepilogo = 0;
  if (millis() - ultimoRiepilogo > 10000) {
    ultimoRiepilogo = millis();
    Serial.printf("\n--- riepilogo: frame=%lu  massimo visto=%d  "
                  "[0]%lu [1]%lu [2]%lu [3]%lu ---\n\n",
                  (unsigned long)frameTot, maxVisti,
                  (unsigned long)istogramma[0], (unsigned long)istogramma[1],
                  (unsigned long)istogramma[2], (unsigned long)istogramma[3]);
  }
}
