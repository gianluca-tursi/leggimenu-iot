/*
 * leggimenu - PASSO 1: il radar respira?
 *
 * Sketch di sola diagnosi per CrowPanel ESP32-S3 (2.13")
 * con un HLK-LD2450 collegato al connettore GPIO_D.
 *
 * Non tocca il display: serve solo a rispondere a tre domande, in ordine.
 *   1. La scheda si accende e si flasha?           -> vedi il banner
 *   2. Dal radar arrivano byte?                    -> vedi "byte ricevuti"
 *   3. I byte hanno senso e vede le persone?       -> vedi i bersagli
 *
 * ATTENZIONE alla USB di questa scheda: non e' la USB nativa dell'S3, c'e' un
 * ponte CH340 attaccato a UART0 (IO43/IO44). Quindi:
 *   - la console seriale viaggia su UART0: "USB CDC On Boot" va DISABILITATO,
 *     altrimenti Serial finisce sulla USB nativa, che qui non e' collegata;
 *   - il radar NON va sul connettore UART0, si pesterebbe i piedi con la
 *     console. Va sul connettore GPIO_D, che porta tutto quello che serve:
 *     IO40 | IO41 | 3V3 | GND.
 *
 * IDE Arduino (o ./scripts/flash.sh, che le imposta da solo):
 *   Scheda:            ESP32S3 Dev Module
 *   USB CDC On Boot:   Disabled     <-- c'e' il CH340, non la USB nativa
 *   Flash Size:        8MB
 *   PSRAM:             OPI PSRAM
 *   Monitor seriale:   115200 baud
 */

// Il GPIO_D ha passo 2,0 mm (JST PH), l'UART0 2,54 mm (JST XH): i Dupont
// entrano solo sul secondo. Ma l'LD2450 trasmette da solo in continuo, quindi
// la linea TX verso il radar non serve: basta ricevere.
//
//   UART0  GND  -> GND del radar     (2,54: il Dupont entra)
//   UART0  5V   -> 5V  del radar     (2,54: il Dupont entra)
//   GPIO_D IO40 -> TX  del radar     (2,0: un pin solo)
//
// Prendere corrente dall'UART0 non disturba la console, che usa TXD/RXD.
static const int PIN_RX = 40;   // IO40, arriva dal TX del radar
static const int PIN_TX = -1;   // non collegato: al radar non mandiamo nulla
static const uint32_t BAUD_RADAR = 256000;

static const uint8_t HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t FOOTER[2] = {0x55, 0xCC};
static const size_t  LUNG_FRAME = 30;

uint8_t buf[256];
size_t  usati = 0;

uint32_t byteTotali = 0;
// Il totale cumulativo non torna a zero quando il cavo si stacca: resta fermo.
// Quindi il guasto va misurato sul TEMPO dall'ultimo frame, non sul valore.
uint32_t ultimoFrame = 0;
static const uint32_t GUASTO_MS = 3000;
uint32_t frameValidi = 0;
uint32_t ultimoReport = 0;
bool     primoFrame = true;

// Il LD2450 mette il segno nel bit alto invece di usare il complemento a due.
static int coord(uint16_t grezzo) {
  int v = grezzo & 0x7FFF;
  return (grezzo & 0x8000) ? v : -v;
}

void stampaFrame(const uint8_t *f) {
  int visti = 0;
  String riga = "";
  for (int n = 0; n < 3; n++) {
    const uint8_t *b = f + 4 + n * 8;
    uint16_t gx = b[0] | (b[1] << 8);
    uint16_t gy = b[2] | (b[3] << 8);
    uint16_t gv = b[4] | (b[5] << 8);
    if (gx == 0 && gy == 0 && gv == 0) continue;   // slot vuoto

    int x = coord(gx), y = coord(gy), v = coord(gv);
    float dist = sqrtf((float)x * x + (float)y * y) / 10.0f;   // mm -> cm
    visti++;
    riga += "  [" + String(n + 1) + "] x=" + String(x / 10) + "cm y=" +
            String(y / 10) + "cm  dist=" + String(dist, 1) + "cm  v=" +
            String(v) + "cm/s\n";
  }

  if (primoFrame) {
    Serial.println("\n*** PRIMO FRAME VALIDO: il radar parla. ***\n");
    primoFrame = false;
  }
  Serial.printf("bersagli: %d\n", visti);
  if (visti) Serial.print(riga);
}

void setup() {
  Serial.begin(115200);
  delay(2500);                       // il tempo di aprire il monitor seriale

  Serial.println();
  Serial.println("=======================================");
  Serial.println(" leggimenu - test radar LD2450");
  Serial.println("=======================================");
  Serial.printf("scheda viva: ESP32-S3, %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  Serial.printf("flash %lu MB, PSRAM %lu KB\n",
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned long)(ESP.getPsramSize() / 1024));
  Serial.printf("UART radar: RX=IO%d (TX non collegato) @ %lu baud\n",
                PIN_RX, (unsigned long)BAUD_RADAR);
  Serial.println("---------------------------------------");
  Serial.println("Se qui sotto resta tutto a zero, il problema e' il");
  Serial.println("cablaggio o l'alimentazione, non il software.");
  Serial.println();

  Serial1.begin(BAUD_RADAR, SERIAL_8N1, PIN_RX, PIN_TX);
}

void loop() {
  while (Serial1.available()) {
    if (usati >= sizeof(buf)) usati = 0;        // buffer pieno: ributto via
    buf[usati++] = Serial1.read();
    byteTotali++;

    // cerco l'header in coda al buffer
    if (usati >= LUNG_FRAME) {
      for (size_t i = 0; i + LUNG_FRAME <= usati; i++) {
        if (memcmp(buf + i, HEADER, 4) == 0 &&
            memcmp(buf + i + LUNG_FRAME - 2, FOOTER, 2) == 0) {
          frameValidi++;
          ultimoFrame = millis();
          stampaFrame(buf + i);
          size_t resto = usati - (i + LUNG_FRAME);
          memmove(buf, buf + i + LUNG_FRAME, resto);
          usati = resto;
          break;
        }
      }
    }
  }

  // battito ogni 2 secondi, cosi' capisci sempre a che punto sei
  if (millis() - ultimoReport > 2000) {
    ultimoReport = millis();
    bool vivo = ultimoFrame && (millis() - ultimoFrame <= GUASTO_MS);
    Serial.printf("[diagnosi] radar=%s byte ricevuti: %lu | frame validi: %lu\n",
                  vivo ? "ok" : "MUTO",
                  (unsigned long)byteTotali, (unsigned long)frameValidi);
    if (!vivo && ultimoFrame) {
      Serial.println("           -> il radar ha SMESSO di trasmettere: e' un filo.");
    }
    if (byteTotali == 0) {
      Serial.println("           -> ZERO byte: controlla che il TX del radar");
      Serial.println("              arrivi su IO40, e che 5V e GND siano a posto.");
    } else if (frameValidi == 0) {
      Serial.println("           -> byte si', frame no: quasi sempre e' il baud");
      Serial.println("              rate. Il LD2450 vuole 256000.");
    }
  }
}
