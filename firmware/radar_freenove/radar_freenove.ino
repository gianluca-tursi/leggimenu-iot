/*
 * leggimenu - radar sul Freenove ESP32 (WROOM-32E)
 *
 * Stessa lettura dell'LD2450, ma su una scheda con i pin a 2,54 mm e un
 * vero 5V in uscita. Toglie di mezzo i due punti deboli della CrowPanel:
 * il connettore da 2,0 mm dove i Dupont non fanno presa, e il rail 3,3 V
 * che doveva alimentare anche il radar.
 *
 * COLLEGAMENTO
 *   Freenove 5V    ---->  5V  del radar   (riga 1, colonna sinistra)
 *   Freenove GND   ---->  GND del radar   (riga 4, colonna sinistra)
 *   Freenove IO16  ---->  TX  del radar   (riga 2, colonna destra)
 *   l'RX del radar resta scollegato: trasmette da solo
 *
 * Stampa nello stesso formato dello sketch CrowPanel, quindi la dashboard
 * (./scripts/radar-live.sh) funziona senza cambiare una riga.
 *
 * Non tocca il display: serve solo a rispondere a tre domande, in ordine.
 *   1. La scheda si accende e si flasha?           -> vedi il banner
 *   2. Dal radar arrivano byte?                    -> vedi "byte ricevuti"
 *   3. I byte hanno senso e vede le persone?       -> vedi i bersagli
 *
 * Carica con:  ./scripts/flash-freenove.sh
 */

// Sul Freenove uso UART2, i cui pin di default sono IO16 (RX) e IO17 (TX).
// A noi serve solo ricevere: l'LD2450 trasmette da solo.
static const int PIN_RX = 16;   // IO16, arriva dal TX del radar
static const int PIN_TX = -1;   // non collegato
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
  Serial.println(" leggimenu - radar su Freenove ESP32");
  Serial.println("=======================================");
  Serial.printf("scheda viva: ESP32 a %lu MHz, flash %lu MB\n",
                (unsigned long)getCpuFrequencyMhz(),
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("UART radar: RX=IO%d (TX non collegato) @ %lu baud\n",
                PIN_RX, (unsigned long)BAUD_RADAR);
  Serial.println("---------------------------------------");
  Serial.println("Se qui sotto resta tutto a zero, il problema e' il");
  Serial.println("cablaggio o l'alimentazione, non il software.");
  Serial.println();

  Serial2.begin(BAUD_RADAR, SERIAL_8N1, PIN_RX, PIN_TX);
}

void loop() {
  while (Serial2.available()) {
    if (usati >= sizeof(buf)) usati = 0;        // buffer pieno: ributto via
    buf[usati++] = Serial2.read();
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
      Serial.println("              arrivi su IO16, e che 5V e GND siano a posto.");
    } else if (frameValidi == 0) {
      Serial.println("           -> byte si', frame no: quasi sempre e' il baud");
      Serial.println("              rate. Il LD2450 vuole 256000.");
    }
  }
}
