/*
 * leggimenu - la luce del tavolo
 *
 * Gira sul Freenove ESP32 (WROOM-32E), separato dal nodo col display.
 * Fa due cose:
 *   1. LUCE AMBIENTE: un bagliore caldo fisso, come una lanterna da tavolo.
 *      E' la funzione che rende l'oggetto desiderabile al ristoratore.
 *   2. IMPULSO DI ATTENZIONE: quando il display e-ink cambia (per esempio
 *      compare il vino abbinato) la luce sale e riscende dolcemente.
 *      L'e-ink non ha retroilluminazione: senza un segnale nessuno se ne accorge.
 *
 * Collegamento (niente resistenze ne' condensatori: 16 LED accanto alla scheda)
 *   Freenove 5V   -> 5V  dello stick
 *   Freenove GND  -> GND
 *   Freenove IO13 -> DIN del primo stick;  DOUT del primo -> DIN del secondo
 *
 * Comandi, da browser o dal server:
 *   http://luce.local/                 pagina di prova
 *   http://luce.local/impulso          un impulso di attenzione
 *   http://luce.local/ambiente?liv=40  luce fissa, 0-100
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Adafruit_NeoPixel.h>

#include "credenziali.h"

#define PIN_LED    13
#define NUM_LED    16          // due stick da 8. Mettine 8 se ne usi uno solo.

// TETTO DI LUMINOSITA': non alzarlo senza un alimentatore dedicato.
// 16 LED a piena potenza chiedono quasi 1 A e la porta USB non ce la fa.
#define MAX_LUMINOSITA 70      // su 255, circa il 27%

// Bianco caldo: rosso pieno, verde a meta', blu appena accennato.
// Sul tavolo il blu e' il colore che fa sembrare tutto un apparecchio medico.
#define CALDO_R 255
#define CALDO_G 140
#define CALDO_B  40

Adafruit_NeoPixel led(NUM_LED, PIN_LED, NEO_GRB + NEO_KHZ800);
WebServer http(80);

uint8_t  ambiente = 25;        // livello di riposo, 0-100
bool     acceso = true;
uint32_t impulsoFino = 0;      // millis fino a cui dura l'impulso
uint32_t impulsoDa = 0;

static void mostra(uint8_t livello) {
  uint8_t l = map(constrain(livello, 0, 100), 0, 100, 0, MAX_LUMINOSITA);
  for (int i = 0; i < NUM_LED; i++)
    led.setPixelColor(i, led.Color(CALDO_R * l / 255, CALDO_G * l / 255, CALDO_B * l / 255));
  led.show();
}

// Salita e discesa morbide: un lampeggio secco in sala sembra un allarme,
// una dissolvenza sembra un invito.
static uint8_t livelloOra() {
  if (!acceso) return 0;
  if (millis() >= impulsoFino) return ambiente;

  float durata = impulsoFino - impulsoDa;
  float t = (millis() - impulsoDa) / durata;          // 0 -> 1
  float curva = sinf(t * PI);                          // 0 -> 1 -> 0, senza spigoli
  return ambiente + (100 - ambiente) * curva;
}

static void avviaImpulso(uint32_t ms) {
  impulsoDa = millis();
  impulsoFino = impulsoDa + ms;
}

// --- pagina di prova ----------------------------------------------------
static const char PAGINA[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="it"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>luce · leggimenu</title><style>
body{margin:0;background:#0d1014;color:#e6ecf3;font:16px/1.5 -apple-system,system-ui,sans-serif;
 display:flex;min-height:100vh;align-items:center;justify-content:center}
.c{width:min(420px,92vw);background:#151a21;border:1px solid #242c36;border-radius:16px;padding:22px}
h1{margin:0 0 4px;font-size:18px}p.s{margin:0 0 20px;color:#7e8a99;font-size:13px}
label{display:block;font-size:13px;color:#7e8a99;margin:18px 0 6px}
input[type=range]{width:100%;accent-color:#e0a23c}
button{width:100%;padding:13px;margin-top:10px;border-radius:10px;border:1px solid #242c36;
 background:#222a35;color:#e6ecf3;font-size:15px;cursor:pointer}
button.p{background:#e0a23c;border-color:#e0a23c;color:#1a1205;font-weight:600}
b{color:#e6ecf3}</style></head><body><div class="c">
<h1>luce del tavolo</h1><p class="s">leggimenu · lanterna e segnale di attenzione</p>
<label>luce ambiente: <b id="v">25</b>%</label>
<input id="r" type="range" min="0" max="100" value="25">
<button class="p" onclick="fetch('/impulso')">Impulso di attenzione</button>
<button onclick="fetch('/spegni').then(()=>{})">Accendi / spegni</button>
</div><script>
const r=document.getElementById('r'),v=document.getElementById('v');
r.oninput=()=>{v.textContent=r.value;fetch('/ambiente?liv='+r.value)};
</script></body></html>)HTML";

void setup() {
  Serial.begin(115200);
  delay(400);
  led.begin();
  led.setBrightness(255);       // il tetto lo applico io in mostra()
  mostra(ambiente);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("collegamento a %s", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) { delay(400); Serial.print("."); }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("collegato. http://%s.local  oppure  http://%s\n",
                  NOME_NODO, WiFi.localIP().toString().c_str());
    if (MDNS.begin(NOME_NODO)) MDNS.addService("http", "tcp", 80);
    for (int i = 0; i < 2; i++) { mostra(80); delay(120); mostra(ambiente); delay(120); }
  } else {
    Serial.println("Wi-Fi non raggiunto: la luce funziona lo stesso, i comandi no.");
  }

  http.on("/", []() { http.send_P(200, "text/html", PAGINA); });
  http.on("/impulso", []() {
    uint32_t ms = http.hasArg("ms") ? http.arg("ms").toInt() : 3500;
    avviaImpulso(constrain(ms, 500, 15000));
    http.send(200, "text/plain", "ok");
  });
  http.on("/ambiente", []() {
    if (http.hasArg("liv")) ambiente = constrain(http.arg("liv").toInt(), 0, 100);
    acceso = true;
    http.send(200, "text/plain", String(ambiente));
  });
  http.on("/spegni", []() { acceso = !acceso; http.send(200, "text/plain", acceso ? "acceso" : "spento"); });
  http.begin();
}

void loop() {
  http.handleClient();

  static uint8_t ultimo = 255;
  uint8_t l = livelloOra();
  if (l != ultimo) { ultimo = l; mostra(l); }
  delay(16);                    // ~60 passi al secondo: dissolvenza fluida
}
