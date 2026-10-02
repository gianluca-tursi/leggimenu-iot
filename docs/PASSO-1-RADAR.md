# Passo 1 — far parlare il radar

Obiettivo: vedere sul monitor seriale le coordinate delle persone davanti al
sensore. Niente display, niente Wi-Fi, niente server. Solo: **funziona o no**.

---

## 1.1 — La scheda da sola (5 minuti, senza toccare il radar)

Prima di collegare qualsiasi cosa, verifica che il CrowPanel si accenda e si
programmi. Se salti questo passo e poi non funziona niente, non saprai di chi è
la colpa.

1. Collega il CrowPanel al Mac con un **cavo USB-C dati** (non uno solo-carica:
   se il Mac non vede nessuna porta nuova, è il cavo).
   Il LED **PWR** sulla scheda deve accendersi.
2. Installa **Arduino IDE 2.x** e, in *Boards Manager*, il pacchetto
   **esp32 by Espressif** (3.x).
3. Imposta *Strumenti*:

   | Voce | Valore |
   |---|---|
   | Scheda | **ESP32S3 Dev Module** |
   | USB CDC On Boot | **Disabled** ← c'è un CH340, non la USB nativa |
   | Flash Size | 8MB |
   | PSRAM | OPI PSRAM |
   | Porta | `/dev/cu.wchusbserial...` |

   Oppure, senza aprire la IDE: `./scripts/flash.sh --monitor`, che imposta
   tutto da solo usando l'arduino-cli già dentro Arduino IDE.app.

   > **Driver**: questa scheda usa un ponte seriale **CH340 (1A86:7522)**, non la
   > USB nativa dell'ESP32-S3. macOS ha `AppleUSBCHCOM` ma non aggancia questo
   > PID, quindi serve **CH34xVCPDriver** dal Mac App Store (gratis, di WCH) e
   > l'approvazione in *Impostazioni → Privacy e Sicurezza*.
   >
   > **Attenzione a Chrome**: se hai aperto un flasher web (ESP Web Tools o
   > simili), Chrome si prende la porta in esclusiva via WebUSB e nessun driver
   > riesce ad agganciarla. Chiudi Chrome con ⌘Q prima di caricare.

4. Apri `firmware/test_radar/test_radar.ino`, carica, apri il **monitor seriale
   a 115200**.

Devi vedere:

```
=======================================
 leggimenu - test radar LD2450
=======================================
scheda viva: ESP32-S3, 240 MHz
flash 8 MB, PSRAM 8192 KB
```

Se lo vedi, **la scheda è viva e sai flashare**. Metà del rischio del progetto è
già sparito.

> Se la porta non compare: tieni premuto **BOOT**, premi e rilascia **RESET**,
> rilascia BOOT. Così entra in modalità download a forza.

---

## 1.2 — Collegare il radar (a scheda SCOLLEGATA dalla USB)

⚠️ **Non sul connettore UART0.** Lì ci passa la console del CH340: radar e
console si pesterebbero i piedi sullo stesso filo. Si usa **GPIO_D**, la cui
serigrafia dice `IO40 | IO41 | 3V3 | GND` — cioè esattamente i quattro fili che
servono, alimentazione compresa.

```
CrowPanel GPIO_D           LD2450
   3V3   ---------------->  3.3V    <-- il pin dedicato, NON il 5V
   GND   ---------------->  GND
   IO40  ---------------->  TX      <-- INCROCIATI
   IO41  ---------------->  RX      <-- INCROCIATI
```

L'LD2450 ha un ingresso 3,3 V dedicato che salta il suo regolatore, quindi si
alimenta direttamente da qui e i 5 V non servono affatto.

**L'errore che fanno tutti** è collegare TX con TX. Se non arriva niente, la
prima cosa da provare è invertire quei due fili: non si rompe nulla.

Due strade per i 4 fili:
- i **jumper F-F** direttamente sui pin dell'UART0, se il guscio li fa entrare;
- il **cavetto JST XH 2.54**, e i fili nudi uniti a quelli del radar.

Il radar va appoggiato **in piedi**, con la faccia delle antenne (le piazzole
dorate) rivolta verso di te, non verso il soffitto.

---

## 1.3 — Leggere

Ricollega la USB e guarda il monitor seriale. Ogni 2 secondi esce una riga di
diagnosi, e questa ti dice esattamente dove sei:

| Cosa leggi | Cosa vuol dire | Cosa fai |
|---|---|---|
| `byte ricevuti: 0` | il radar non trasmette o i fili sono sbagliati | controlla 5V e GND, poi **inverti TX/RX** |
| byte crescono, `frame validi: 0` | i fili sono giusti, i dati no | quasi sempre è il baud rate: dev'essere **256000** |
| `*** PRIMO FRAME VALIDO ***` | ✅ fatto | passa alla prova sotto |

Poi la prova vera:

```
bersagli: 1
  [1] x=-12cm y=78cm  dist=78.9cm  v=-14cm/s
```

- **y** = quanto è lontano da te, **x** = quanto è spostato a destra (positivo)
  o a sinistra (negativo), **v** = velocità (negativa = si avvicina).
- Cammina davanti al sensore: i numeri devono seguirti.
- Mettiti di lato, oltre i 60°: devi **sparire**. È il cono di cui parlavamo.
- Fatti aiutare da qualcuno e mettetevi davanti in due: devono comparire
  `[1]` e `[2]`. **È il conteggio dei coperti, ed è il cuore del progetto.**
- Poi state fermi immobili qualche secondo e guardate se sparite: è il limite
  dell'LD2450 di cui ti avevo detto, ed è il momento in cui scopriamo se serve
  davvero l'LD2410C.

---

## 1.4 — Quando funziona

Segnati questi tre numeri, servono per tarare il software:

1. A che distanza **massima** ti vede ancora, seduto al tavolo.
2. A che angolo **sparisci** andando di lato.
3. Quanti secondi di immobilità servono perché ti **perda**.

Il terzo è quello che decide il valore di `SEC_ABBANDONO` in
`backend/state.py`, oggi a 180 secondi.

---

## Appendice — quello che dice la documentazione Elecrow

Fonte: [wiki 2.13"](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_HMI_2.13-inch_Display.html)
e [repo ufficiale](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250).

### Pin del display sul 2.13" (dal readme ufficiale)

**Diversi da quelli del 4.2"**, attenzione:

```
SCK  12      RES  10      CS    14
MOSI 11      DC   13      BUSY   9
```

Quindi sul 2.13" sono occupati IO9, IO10, IO11, IO12, IO13, IO14 — e
**IO40/IO41 del GPIO_D restano liberi**, che è dove mettiamo il radar.

Libreria display: **EPD v1.0.0**, sta nella cartella `example` del repo.
Nel repo c'è anche `factory_firmware`, se un giorno rivuoi la balena.

### Errori nella wiki, da non seguire

- La descrizione parla di *"2x10 pin GPIO interface"*: è copiato dalla pagina del
  4.2". Qui il connettore GPIO_D ha **4 pin** — lo dice sia la tabella
  *Interfaces* (`GPIO_D: IO40\IO41`) sia la serigrafia sulla scheda
  (`IO40 | IO41 | 3V3 | GND`).
- La tabella dei pin è intitolata *"1.54-inch Display Port"*: altro copia-incolla.

In caso di dubbio vale la serigrafia sulla scheda, non la wiki.

### Cosa la documentazione NON dice

Niente sul driver USB. Nessuna menzione del ponte seriale, e nella cartella
`Datasheet` ci sono solo i datasheet dei driver display e dell'ESP32-S3.

La prova del CH340 non viene dai documenti ma dal Mac: `idVendor 0x1A86` (WCH),
`idProduct 0x7522`, `bDeviceClass 255` (vendor-specific). È il chip che parla.

---

## Come si alimenta il radar (risolto il 23/09 dopo parecchi tentativi)

⚠️ **Il 5V dell'UART0 NON eroga corrente.** Sulla serigrafia, sopra quel
connettore, c'è scritto **"Power in"**: serve ad alimentare la CrowPanel
dall'esterno, non a prendere 5 V da lei. Collegandoci il radar, il radar resta
spento e non trasmette niente.

**Cablaggio che funziona** — tutto sul GPIO_D, saltando il regolatore del radar:

```
CrowPanel GPIO_D            LD2450 (header 2x4)
  3V3   ---------------->    3.3V     riga 2, colonna sinistra
  GND   ---------------->    GND      riga 4, colonna sinistra
  IO40  ---------------->    TX       riga 2, colonna destra
  (IO41 resta libero)        RX       NON collegato
```

Il cavetto JST incluso porta solo `5V | RX | TX | GND`: il 3,3 V sta solo
sull'header 2×4, ed è da lì che va preso.

### Come si riconosce il problema

Lo scanner (`firmware/scan_radar/`) lo dice a colpo d'occhio:

| Lettura su IO40 | Significato |
|---|---|
| `riposo=basso` + `TENUTO BASSO` | filo collegato ma **radar senza corrente** |
| `riposo=ALTO` + transizioni + header | ✅ radar alimentato e funzionante |

Una linea TX di un dispositivo acceso sta **alta** a riposo. Se è bassa e
immobile anche con il pull-up interno, il problema è l'alimentazione, non i dati.
