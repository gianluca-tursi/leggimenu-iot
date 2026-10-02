# Passo 2 — il QR sul pannello

Sketch: `firmware/qr_eink/` — carica con `./scripts/flash.sh firmware/qr_eink`

## Due trappole che ci sono costate un'ora, scritte qui per non ricascarci

### 1. Il pannello ha un pin di accensione: **IO7**

Non è documentato in nessuna pagina della wiki Elecrow. L'ho trovato solo nel
loro `main.ino`:

```cpp
pinMode(7, OUTPUT);  digitalWrite(7, HIGH);   // 屏电源 = alimentazione schermo
pinMode(19, OUTPUT); digitalWrite(19, HIGH);  // LED di accensione
```

Senza IO7 alto il pannello **non è alimentato**, quindi il segnale BUSY non
risponde mai e qualunque libreria va in `Busy Timeout`.

(Nota di contorno: **IO19 è usato come LED**. Sull'ESP32-S3 quello sarebbe USB D-,
ed è l'ennesima conferma che questa scheda non usa la USB nativa ma il CH340.)

### 2. GxEPD2 non funziona su questo pannello

Sembrava la scelta ovvia — la wiki dice "Driver Chip: SSD1680Z, JD79661" — ma
leggendo il driver Elecrow:

| | Cosa dice il loro codice | Cosa assume GxEPD2 |
|---|---|---|
| BUSY | `while (EPD_ReadBUSY == 0)` → **alto = libero** | alto = occupato |
| Refresh | `0x17` + `0xA5` (JD79661) | `0x20` (SSD1680) |
| SPI | bit-bang su GPIO | SPI hardware |

Quindi il pannello montato è il **JD79661**, non l'SSD1680: comandi diversi e
BUSY invertito. Usiamo il driver ufficiale della scheda, copiato in
`firmware/qr_eink/` dal [repo Elecrow](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250):
`spi.*`, `EPD_Init.*`, `EPD.*`, `EPDfont.h`.

## Come si disegna

```cpp
EPD_Init(); EPD_Clear(); EPD_Update();      // pulisce il pannello
memset(ImageBW, 0xFF, ALLSCREEN_BYTES);      // buffer bianco (1 = bianco)
EPD_ShowString(x, y, "testo", BLACK, 24);    // font: 12 16 24 32 48
EPD_DrawPoint(x, y, BLACK);
EPD_DrawRectangle(xs, ys, xe, ye, BLACK);
EPD_DisplayImage(ImageBW); EPD_Update();     // manda al pannello
EPD_Sleep();                                  // l'immagine resta comunque
```

Orientamento: `USE_HORIZONTIAL 2` in `EPD_Init.h` → **250 × 128** (122 visibili),
orizzontale. Colori: `BLACK 0x00`, `WHITE 0xFF`.

Il QR lo genera la scheda con il codice di Richard Moore (MIT), copiato in
`qrgen.c/h`: il core ESP32 ha un suo `qrcode.h` che vincerebbe sull'include e la
sua libreria non è linkabile in Arduino.
