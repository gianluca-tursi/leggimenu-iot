/*
 * leggimenu - nodo autonomo
 *
 * Alimentato da una power bank, senza cavo verso il Mac:
 *   - legge l'LD2450 su IO40
 *   - si collega al Wi-Fi e serve la dashboard da solo
 *   - scrive il proprio indirizzo sul display e-ink
 *
 * Apri http://radar.local (o l'IP che compare sul display) da Mac,
 * telefono o tablet. Possono guardare in piu' persone insieme.
 *
 * Prima di caricare: metti la tua rete in credenziali.h
 * Carica con:  ./scripts/flash.sh firmware/nodo_wifi
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <string.h>
#include <math.h>

#include "credenziali.h"
#include "pagina.h"
#include "EPD.h"

extern uint8_t ImageBW[];

// --- pin --------------------------------------------------------------
static const int PIN_RADAR_RX = 40;     // IO40 del GPIO_D, TX del radar
#define EPD_POWER  7
#define LED_POWER 19
// nel driver Elecrow il colore di EPD_DrawPoint e' rovesciato: vedi docs
#define PIX_NERO   WHITE

static const uint32_t BAUD_RADAR = 256000;
static const uint8_t  HEADER[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t  FOOTER[2] = {0x55, 0xCC};
static const size_t   LUNG_FRAME = 30;

WebServer http(80);
WebSocketsServer ws(81);

uint8_t buf[256];
size_t  usati = 0;
uint32_t ultimoInvio = 0;
// Da quando non decodifico un frame valido dal radar. Senza questo, il
// firmware ripete all'infinito l'ultima posizione nota e una persona che
// non c'e' piu' resta disegnata ferma sullo schermo per sempre.
uint32_t ultimoFrameRadar = 0;
static const uint32_t SCADENZA_RADAR_MS = 600;    // ~6 frame persi: i bersagli scadono
static const uint32_t GUASTO_RADAR_MS  = 3000;   // 3 secondi di silenzio: e' un guasto
bool radarVivo = false;
uint32_t frameTot = 0, byteTot = 0;
uint32_t ultimaDiagnosi = 0;

struct Bersaglio { int x, y, v; float dist, ang; };
Bersaglio bersagli[3];
int nBersagli = 0;

// --- stima dei coperti -------------------------------------------------
// Il numero istantaneo balla: due persone vicine a volte si fondono, una che
// sta ferma sparisce per un attimo. Quindi non mostro l'istante, mostro il
// MASSIMO degli ultimi secondi: e' la statistica che sbaglia per difetto,
// che e' l'errore giusto da fare (meglio un coperto in meno che uno inventato).
// Due persone alla stessa distanza si separano solo negli istanti in cui si
// muovono in modo diverso. Una finestra larga cattura piu' di quei momenti:
// col massimo, ogni sdoppiamento visto anche una volta sola fa testo.
static const uint32_t FINESTRA_MS = 25000;   // su quanto guardo indietro
static const uint32_t MIN_FRA_REFRESH = 6000; // l'e-ink non si ridisegna a raffica

// --- sequenza di apertura del tavolo -----------------------------------
// Al tavolo le persone non arrivano insieme: uno si siede, tre secondi dopo
// un altro. Quindi non dichiaro il numero al primo che vedo: aspetto che la
// situazione si assesti. Ogni volta che il conteggio SALE, il cronometro
// riparte da capo. Solo quando resta fermo per ASSESTAMENTO_MS dico "sono N".
static const uint32_t ASSESTAMENTO_MS = 6000;   // quiete richiesta prima di dichiarare
static const uint32_t USCITA_MS       = 20000;  // vuoto per tanto = tavolo libero

enum Fase { VUOTO, ARRIVO, STABILE };
Fase fase = VUOTO;
uint8_t candidati = 0;        // il massimo visto in questo arrivo
uint8_t copertiConfermati = 0;
uint32_t tCambio = 0;         // quando il candidato e' salito l'ultima volta
uint32_t tVuoto = 0;          // da quando non vedo nessuno

static const char *nomeFase() {
  return fase == VUOTO ? "vuoto" : fase == ARRIVO ? "arrivo" : "stabile";
}


struct Campione { uint32_t t; uint8_t n; };
Campione storico[320];   // 25 s a 10 Hz
int nStorico = 0;
int copertiMostrati = -1;
uint32_t ultimoRefresh = 0;

static uint8_t stimaCoperti() {
  uint32_t ora = millis();
  int scrivi = 0;
  uint8_t massimo = 0;
  for (int i = 0; i < nStorico; i++) {          // butto i campioni vecchi
    if (ora - storico[i].t <= FINESTRA_MS) {
      storico[scrivi++] = storico[i];
      if (storico[i].n > massimo) massimo = storico[i].n;
    }
  }
  nStorico = scrivi;
  return massimo;
}

// Azzera tutto e riparte: e' il tasto "nuova prova".
static void azzeraSequenza() {
  fase = VUOTO; candidati = 0; copertiConfermati = 0;
  tCambio = tVuoto = millis();
  nStorico = 0;
  copertiMostrati = -1;       // forza il ridisegno del display
  Serial.println(">>> RESET: riparto da zero");
}

// Una passata di macchina a stati, chiamata a ogni campione.
static void aggiornaSequenza(uint8_t stima) {
  uint32_t ora = millis();

  if (stima == 0) {
    if (tVuoto == 0) tVuoto = ora;
    if (fase != VUOTO && ora - tVuoto >= USCITA_MS) {
      Serial.println(">>> tavolo tornato libero");
      fase = VUOTO; candidati = 0; copertiConfermati = 0;
    }
    return;
  }
  tVuoto = 0;

  if (fase == VUOTO) {                    // primo arrivo
    fase = ARRIVO; candidati = stima; tCambio = ora;
    Serial.printf(">>> qualcuno e' arrivato (%u): aspetto che si assestino\n", stima);
    return;
  }

  if (stima > candidati) {                // si e' seduto qualcun altro
    candidati = stima; tCambio = ora;
    fase = ARRIVO;
    Serial.printf(">>> ora sono %u: riparte l'attesa\n", stima);
    return;
  }

  if (fase == ARRIVO && ora - tCambio >= ASSESTAMENTO_MS) {
    fase = STABILE; copertiConfermati = candidati;
    Serial.printf(">>> CONFERMATO: %u coperti\n", copertiConfermati);
  }
}

static void registraCampione(uint8_t n) {
  if (nStorico >= (int)(sizeof(storico) / sizeof(storico[0]))) {
    memmove(storico, storico + 1, sizeof(storico) - sizeof(storico[0]));
    nStorico--;
  }
  storico[nStorico++] = {millis(), n};
}

static int coord(uint16_t grezzo) {
  int v = grezzo & 0x7FFF;
  return (grezzo & 0x8000) ? v : -v;
}

// --- display ----------------------------------------------------------
// La schermata del tavolo: numero grande a sinistra, dati piccoli a destra.
static void schermataCoperti(uint8_t n) {
  // Pulizia vera del pannello: senza, restano i residui della schermata
  // precedente e sembrano simboli casuali sparsi per lo schermo.
  EPD_Clear();
  EPD_Update();
  memset(ImageBW, 0xFF, ALLSCREEN_BYTES);
  char buf[48];

  // Display in VERTICALE: 122 larghezza x 250 altezza.
  // Le stringhe vanno tenute corte: col font 24 ci stanno 10 caratteri,
  // col 48 solo 5. Meglio due righe brevi che una troncata.
  if (!radarVivo) {
    // Il dispositivo deve dire quando e' rotto, non fingere un tavolo libero.
    EPD_ShowString(8,  44, (char *)"RADAR", PIX_NERO, 24);
    EPD_ShowString(8,  78, (char *)"ASSENTE", PIX_NERO, 24);
    EPD_ShowString(8, 120, (char *)"controlla", PIX_NERO, 16);
    EPD_ShowString(8, 142, (char *)"i 3 fili:", PIX_NERO, 16);
    EPD_ShowString(8, 164, (char *)"3V3 GND", PIX_NERO, 16);
    EPD_ShowString(8, 186, (char *)"IO40", PIX_NERO, 16);
  } else if (fase == VUOTO) {
    EPD_ShowString(8,  70, (char *)"TAVOLO", PIX_NERO, 24);
    EPD_ShowString(8, 104, (char *)"LIBERO", PIX_NERO, 24);
  } else if (fase == ARRIVO) {
    EPD_ShowString(8,  40, (char *)"si stanno", PIX_NERO, 16);
    EPD_ShowString(8,  62, (char *)"accomodando", PIX_NERO, 16);
    snprintf(buf, sizeof(buf), "%u", candidati);
    EPD_ShowString(8, 100, buf, PIX_NERO, 48);
    EPD_ShowString(48, 124, (char *)"per ora", PIX_NERO, 16);
  } else {
    snprintf(buf, sizeof(buf), "%u", n);
    EPD_ShowString(8,  56, buf, PIX_NERO, 48);
    EPD_ShowString(48, 80, (char *)(n == 1 ? "coperto" : "coperti"), PIX_NERO, 16);
    EPD_ShowString(8, 126, (char *)"confermati", PIX_NERO, 16);
  }

  EPD_ShowString(8, 200, (char *)"leggimenu", PIX_NERO, 16);
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "%d dBm", WiFi.RSSI());
    EPD_ShowString(8, 224, buf, PIX_NERO, 12);
    EPD_ShowString(8, 238, (char *)WiFi.localIP().toString().c_str(), PIX_NERO, 12);
  } else {
    EPD_ShowString(8, 224, (char *)"senza Wi-Fi", PIX_NERO, 12);
  }
  EPD_DisplayImage(ImageBW);
  EPD_Update();
}

static void schermata(const char *r1, const char *r2, const char *r3) {
  EPD_Clear();
  EPD_Update();
  memset(ImageBW, 0xFF, ALLSCREEN_BYTES);
  EPD_ShowString(6,  30, (char *)r1, PIX_NERO, 16);
  EPD_ShowString(6,  56, (char *)r2, PIX_NERO, 12);
  EPD_ShowString(6,  76, (char *)r3, PIX_NERO, 12);
  EPD_ShowString(6, 210, (char *)"leggimenu", PIX_NERO, 16);
  EPD_DisplayImage(ImageBW);
  EPD_Update();
}

// --- radar ------------------------------------------------------------
static void leggiRadar() {
  while (Serial1.available()) {
    if (usati >= sizeof(buf)) usati = 0;
    buf[usati++] = Serial1.read();
    byteTot++;
    if (usati < LUNG_FRAME) continue;

    for (size_t i = 0; i + LUNG_FRAME <= usati; i++) {
      if (memcmp(buf + i, HEADER, 4) || memcmp(buf + i + LUNG_FRAME - 2, FOOTER, 2)) continue;

      const uint8_t *f = buf + i;
      nBersagli = 0;
      for (int n = 0; n < 3; n++) {
        const uint8_t *b = f + 4 + n * 8;
        uint16_t gx = b[0] | (b[1] << 8), gy = b[2] | (b[3] << 8), gv = b[4] | (b[5] << 8);
        if (!gx && !gy && !gv) continue;
        int x = coord(gx) / 10, y = coord(gy) / 10, v = coord(gv);
        bersagli[nBersagli++] = {x, y, v,
            sqrtf((float)x * x + (float)y * y),
            degrees(atan2f((float)x, (float)max(y, 1)))};
      }
      frameTot++;
      ultimoFrameRadar = millis();
      size_t resto = usati - (i + LUNG_FRAME);
      memmove(buf, buf + i + LUNG_FRAME, resto);
      usati = resto;
      break;
    }
  }
}

// Niente String qui dentro: dieci allocazioni al secondo frammentano la heap
// e dopo qualche minuto il loop si pianta. Buffer fisso, riempito con snprintf.
static char jbuf[420];

static void inviaBersagli() {
  int k = snprintf(jbuf, sizeof(jbuf),
                   "{\"tipo\":\"frame\",\"n\":%d,\"bersagli\":[", nBersagli);
  for (int i = 0; i < nBersagli && k < (int)sizeof(jbuf) - 120; i++) {
    k += snprintf(jbuf + k, sizeof(jbuf) - k,
                  "%s{\"id\":%d,\"x\":%d,\"y\":%d,\"dist\":%.1f,\"v\":%d,\"ang\":%.1f}",
                  i ? "," : "", i + 1, bersagli[i].x, bersagli[i].y,
                  bersagli[i].dist, bersagli[i].v, bersagli[i].ang);
  }
  uint32_t manca = 0;
  if (fase == ARRIVO) {
    uint32_t passato = millis() - tCambio;
    manca = passato >= ASSESTAMENTO_MS ? 0 : (ASSESTAMENTO_MS - passato) / 100;
  }
  snprintf(jbuf + k, sizeof(jbuf) - k,
           "],\"fase\":\"%s\",\"candidati\":%u,\"confermati\":%u,\"manca\":%.1f}",
           nomeFase(), candidati, copertiConfermati, manca / 10.0);
  ws.broadcastTXT(jbuf, strlen(jbuf));
}

void setup() {
  Serial.begin(115200);
  delay(600);

  pinMode(EPD_POWER, OUTPUT); digitalWrite(EPD_POWER, HIGH);
  pinMode(LED_POWER, OUTPUT); digitalWrite(LED_POWER, HIGH);
  delay(100);
  EPD_Init(); EPD_Clear(); EPD_Update();
  schermata("Avvio...", "mi collego al Wi-Fi", WIFI_SSID);

  Serial1.begin(BAUD_RADAR, SERIAL_8N1, PIN_RADAR_RX, -1);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // la latenza conta piu' del consumo
  WiFi.setAutoReconnect(true);          // lo gestisce l'SDK, meglio di me a mano
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("collegamento a %s", WIFI_SSID);
  uint32_t inizio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inizio < 25000) {
    delay(400); Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi FALLITO: controlla credenziali.h (rete a 2,4 GHz!)");
    schermata("Wi-Fi KO", "rete non raggiunta", WIFI_SSID);
    return;
  }

  String ip = WiFi.localIP().toString();
  Serial.printf("collegato. IP: %s\n", ip.c_str());
  Serial.printf("apri: http://%s.local  oppure  http://%s\n", NOME_NODO, ip.c_str());

  if (MDNS.begin(NOME_NODO)) MDNS.addService("http", "tcp", 80);

  http.on("/", []() { http.send_P(200, "text/html", PAGINA_HTML); });
  http.on("/reset", []() { azzeraSequenza(); http.send(200, "text/plain", "ok"); });
  http.onNotFound([]() { http.send(404, "text/plain", "non c'e'"); });
  http.begin();
  ws.begin();

  schermata(("http://" + String(NOME_NODO) + ".local").c_str(),
            ip.c_str(), ("rete: " + String(WIFI_SSID)).c_str());
}

void loop() {
  http.handleClient();
  ws.loop();
  leggiRadar();

  // 'R' sulla seriale = reset, cosi' il tasto funziona anche dalla
  // dashboard via cavo, che non puo' chiamare l'HTTP del nodo.
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 'R' || c == 'r') azzeraSequenza();
  }

  // 10 volte al secondo: sul WebSocket per chi guarda in Wi-Fi, e in chiaro
  // sulla seriale per la dashboard via cavo (./scripts/radar-live.sh).
  // Cosi' lo stesso firmware va in tutti e due i modi.
  // I dati del radar scadono: se non arrivano frame, i bersagli non esistono
  // piu'. Meglio dire "non vedo nessuno" che mentire con un dato vecchio.
  if (ultimoFrameRadar && millis() - ultimoFrameRadar > SCADENZA_RADAR_MS && nBersagli) {
    nBersagli = 0;
    Serial.println(">>> radar in silenzio: azzero i bersagli");
  }

  // Un contatore fermo non e' un contatore a zero: il totale resta alto anche
  // a filo staccato. Quindi il guasto lo misuro sul TEMPO dall'ultimo frame.
  bool vivoOra = ultimoFrameRadar && (millis() - ultimoFrameRadar <= GUASTO_RADAR_MS);
  if (vivoOra != radarVivo) {
    radarVivo = vivoOra;
    Serial.printf(">>> RADAR %s\n", radarVivo ? "torna a parlare" : "NON RISPONDE (cavo?)");
    copertiMostrati = -99;                  // forza il ridisegno del display
  }

  if (millis() - ultimoInvio >= 100) {
    ultimoInvio = millis();
    if (ws.connectedClients()) inviaBersagli();

    registraCampione(nBersagli);
    // La fase va anche in chiaro: la dashboard via cavo ricostruisce i frame
    // leggendo questo testo, e senza non potrebbe mostrare il pannello Tavolo.
    uint32_t manca = 0;
    if (fase == ARRIVO) {
      uint32_t passato = millis() - tCambio;
      manca = passato >= ASSESTAMENTO_MS ? 0 : (ASSESTAMENTO_MS - passato);
    }
    Serial.printf("fase: %s cand=%u conf=%u manca=%.1f\n",
                  nomeFase(), candidati, copertiConfermati, manca / 1000.0);
    Serial.printf("bersagli: %d\n", nBersagli);
    for (int i = 0; i < nBersagli; i++)
      Serial.printf("  [%d] x=%dcm y=%dcm  dist=%.1fcm  v=%dcm/s\n",
                    i + 1, bersagli[i].x, bersagli[i].y,
                    bersagli[i].dist, bersagli[i].v);
  }

  // Aggiorno l'e-ink solo quando la stima cambia davvero, e mai piu' di una
  // volta ogni sei secondi: ogni refresh costa un paio di secondi e lampeggia.
  {
    uint8_t stima = stimaCoperti();
    aggiornaSequenza(stima);
    // sul display mostro il numero confermato, o il candidato se stanno arrivando
    int daMostrare = (fase == STABILE) ? copertiConfermati
                   : (fase == ARRIVO)  ? -2 - candidati    // codice per "in arrivo"
                   : 0;
    if (daMostrare != copertiMostrati && millis() - ultimoRefresh >= MIN_FRA_REFRESH) {
      copertiMostrati = daMostrare;
      ultimoRefresh = millis();
      schermataCoperti(fase == STABILE ? copertiConfermati : candidati);
    }
  }

  if (millis() - ultimaDiagnosi >= 2000) {
    ultimaDiagnosi = millis();
    // La heap libera e' la spia da tenere d'occhio: se cala di continuo,
    // da qualche parte c'e' ancora una perdita.
    bool su = WiFi.status() == WL_CONNECTED;
    Serial.printf("[%s] radar=%s ip=%s rssi=%d byte=%lu frame=%lu client=%u heap=%lu\n",
                  su ? "wifi" : "NO-WIFI",
                  radarVivo ? "ok" : "MUTO",
                  su ? WiFi.localIP().toString().c_str() : "-",
                  su ? WiFi.RSSI() : 0,
                  (unsigned long)byteTot, (unsigned long)frameTot,
                  ws.connectedClients(), (unsigned long)ESP.getFreeHeap());
    if (ws.connectedClients()) {
      char d[140];
      snprintf(d, sizeof(d),
               "{\"tipo\":\"diagnosi\",\"byte\":%lu,\"frame\":%lu,\"heap\":%lu,"
               "\"rssi\":%d,\"ip\":\"%s\",\"coperti\":%d,\"radar\":%s}",
               (unsigned long)byteTot, (unsigned long)frameTot,
               (unsigned long)ESP.getFreeHeap(),
               su ? WiFi.RSSI() : 0,
               su ? WiFi.localIP().toString().c_str() : "-",
               copertiMostrati, radarVivo ? "true" : "false");
      ws.broadcastTXT(d, strlen(d));
    }

    // Se il Wi-Fi cade riprovo, ma non piu' di una volta ogni 20 secondi:
    // richiamare begin() mentre un tentativo e' gia' in corso lo annulla, e
    // il nodo resta a riconnettersi per sempre senza riuscirci.
    static uint32_t ultimoTentativo = 0;
    if (WiFi.status() != WL_CONNECTED && millis() - ultimoTentativo > 20000) {
      ultimoTentativo = millis();
      Serial.println("Wi-Fi caduto: nuovo tentativo");
      WiFi.disconnect(true);
      delay(50);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
}
