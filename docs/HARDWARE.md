# Nodo tavolo — hardware (rev. 2, dopo verifica compatibilità)

Verificato su amazon.it e sui wiki dei produttori il **21/09/2026**, leggendo
schede tecniche e recensioni. **Cambio raccomandazione rispetto alla rev. 1: si va
di ESP32.** Sotto il perché.

---

## 1. Cosa è saltato fuori dalle verifiche

### ❌ L'LD2450 non si collega con i Dupont

Recensione Vine (Germania, 24/12/2025) sul modulo Paradisetronic
[B0G4RSYD5V](https://www.amazon.it/dp/B0G4RSYD5V):

> "Non viene fornito nessun cavo JST a 4 pin, cosa che con questi pin
> particolarmente piccoli e **incompatibili con i DUPONT** sulla scheda è solo
> frustrante. Ho tolto gli header DUPONT e li ho pinzati direttamente sui pin."

I pin sulla scheda sono **JST 1,25 mm**, i Dupont sono **2,54 mm**: passo doppio,
non entrano. Stesso discorso per l'LD2410C. Quindi o compri un modulo **col cavo
incluso**, o saldi, o pinzi.

Sempre da quelle recensioni, due cose confermate:
- **falsi negativi con la persona perfettamente immobile** → conferma che la
  presenza statica va coperta dall'LD2410C, come ti dicevo
- **un esemplare arrivato guasto** (reso ok) → conviene prenderne due

### ✅ L'e-ink Waveshare funziona col Pi, ma è lento

Recensioni di [B07Q6VBF89](https://www.amazon.it/dp/B07Q6VBF89): funziona sia con
le librerie Arduino sia con quelle **Python per Raspberry Pi**, display di buona
qualità, ma "un po' lento il refresh, molto visibile". Normale sul 4,2" full
refresh (~4 s). Una recensione lamenta che non arriva nessun datasheet in scatola.

### ❌ Il tuo Pi Zero 2 W senza pin è un problema vero

Ho cercato le scorciatoie, non ci sono:
- **hammer header** (press-fit senza saldare): **non disponibile** su amazon.it
- **Pi Zero 2 WH** già saldato: non si trova, solo il vecchio Zero WH (v1) a
  40,91 € o kit gonfiati a 142 €

Quindi col Pi come nodo: **40 saldature** sul pettine GPIO (o ~14 se salti i pin
inutilizzati, ma è un lavoro brutto e fragile) **+ ~8** per i fili dei radar.
Circa **48 punti di saldatura** su una scheda da 20 €, con la piazzola del Pi Zero
che è piccola e si stacca se ci vai pesante.

---

## 2. Sì, usiamo l'ESP32 — ma questo ESP32

La scheda giusta esiste e risolve tutto in un colpo:

### **Elecrow CrowPanel ESP32 E-Paper 4.2"** — [B0G43FCHFX](https://www.amazon.it/dp/B0G43FCHFX) — 51,29 €, consegna ven 25 set

È **display e-ink + microcontrollore + scocca, già assemblati**. Dal wiki Elecrow
ufficiale:

| Voce | Valore |
|---|---|
| MCU | ESP32-S3-WROOM-1-N8R8, 240 MHz, **8 MB flash + 8 MB PSRAM** |
| Display | e-paper 400×300 B/N, driver SSD1683, area attiva 84,8×63,6 mm |
| Refresh | **parziale supportato** (aggiorni una riga senza lampeggio) |
| Interfacce | **UART0 ×1**, header **GPIO ×1**, batteria SH1.0 (con circuito di ricarica), slot microSD |
| GPIO liberi | IO3, IO8, IO9, IO14, IO15, IO16, IO17, IO18, IO19, IO20, IO21, IO38 |
| Alimentazione | **USB-C** (anche seriale e flash dalla stessa porta) |
| Ambienti | Arduino IDE, ESP-IDF, **MicroPython** |
| Extra | encoder rotativo + 3 pulsanti programmabili |

Recensione italiana (06/06/2026): *"Funzionante come da descrizione senza problemi
con IDE Arduino e librerie fornite da Elecrow."*

### Cosa guadagni rispetto al Pi

| | Pi Zero 2 W | CrowPanel ESP32 |
|---|---|---|
| Saldature | **~48** | **0** |
| Cavi display | 8 fili volanti | nessuno, è integrato |
| Alimentazione | PSU 5V 3A dedicato | **qualsiasi caricatore USB-C** |
| microSD | obbligatoria (+20 €) | non serve |
| Avvio | ~25 s di boot Linux | **istantaneo** |
| SD che si corrompe a fine serata | succede | non c'è |
| Batteria | no | **connettore LiPo con ricarica a bordo** |
| Costo nodo | ~100 € | **51 €** |

### E il tuo Raspberry Pi Zero 2 W?

**Diventa il server.** Ci gira sopra il backend Python che ho già scritto: la
dashboard cassa, il motore dei vini, le WebSocket. E siccome come server non usa
nessun GPIO, **non devi saldarci niente**. Saldature totali nel progetto: **zero**.

### Cosa perdi (onestamente)

- Firmware in **Arduino C++** invece che Python: si flasha via USB-C, si debugga
  dal monitor seriale. Iterazione più lenta di un `ssh pi@... && python agent.py`.
- Niente OTA all'inizio: per aggiornare il firmware serve il cavo. Si aggiunge
  dopo, l'ESP32 la supporta nativamente.
- Il Wi-Fi va scritto nel firmware (per l'MVP è giusto così).

---

## 3. Verifica sulle foto della scheda (21/09, dalle immagini prodotto)

### ✅ L'header GPIO del CrowPanel è popolato — 2×10, pinout letto dal serigrafato

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| **Riga A** | IO3 | IO9 | IO15 | IO17 | IO19 | IO21 | GND | GND | GND | GND |
| **Riga B** | IO8 | IO14 | IO16 | IO18 | IO20 | IO38 | 3V3 | 3V3 | 3V3 | 3V3 |

12 GPIO liberi, 4 GND, 4× 3V3. **Zero saldature sul nodo.**

⚠️ **Non usare IO19 e IO20**: sull'ESP32-S3 sono le linee **USB D-/D+ native**, e
su questa scheda la USB-C ci passa sopra. Se ci attacchi il radar perdi la porta
con cui la programmi. Restano comunque 10 GPIO buoni.

⚠️ **Sull'header non ci sono i 5 V**, solo 3V3. Questo decide quale radar prendere.

### ✅ LD2450 Dytabepl: si collega senza saldare e va a 3,3 V

Dalle foto del prodotto, la scheda ha **tre** modi di collegarsi:
1. **header maschio 2×4 già saldato**: `5V | 3.3V | PA9 | GND` e `RX | TX | DP | DM`
2. fila di 6 pad passanti sul bordo: `GND PA9 RX TX DP DM`
3. connettore JST 1,25 mm, **con il cavo incluso nel kit**: JST da una parte,
   **4 connettori Dupont femmina** dall'altra (si vede nella foto del kit)

E soprattutto: **c'è un pin 3.3V dedicato**, quindi lo alimenti direttamente dai
3V3 del CrowPanel senza cercare i 5 V da nessuna parte. La logica del radar è
già a 3,3 V come l'ESP32-S3: **nessun level shifter**.

L'antenna PCB inclusa nel kit non serve: il radar ha le sue patch a bordo.

### ⚠️ LD2410C: ha l'header, ma vuole 5 V che il CrowPanel non ha

Header maschio 5 pin 2,54 mm già saldato ✅, ma sulla scheda si vede un **LDO
6209A (3,3 V)** e l'unico ingresso esposto è **VIN a 5 V**. Dandogli i 3V3 del
CrowPanel il regolatore va in dropout e il sensore diventa ballerino.

**Decisione: per ora non comprarlo.** Partiamo con l'LD2450 da solo: tiene la
presenza col mio filtro software (3 minuti di memoria dopo l'ultimo segnale), e a
tavola ci si muove di continuo. Se sul campo vediamo davvero i buchi con la gente
immobile, lo aggiungiamo dopo prendendo i 5 V da un caricatore USB a parte.

### Gender dei cavetti

L'header del CrowPanel è **femmina** (corpo nero pieno, tipico dello zoccolo),
i pin dei radar sono **maschio**, il cavo incluso finisce **femmina**. Per non
restare bloccato per un connettore sbagliato: **kit misto M-M / M-F / F-F**,
13 € e hai coperto ogni combinazione.

---

## 4. LISTA DEFINITIVA DA COMPRARE

### A. Nodo tavolo — obbligatori — **97,13 €**

| # | Pezzo | Prodotto | € |
|---|---|---|---|
| 1 | Display + ESP32 + scocca | [Elecrow CrowPanel ESP32 E-Paper 4.2"](https://www.amazon.it/dp/B0G43FCHFX) | 52,24 |
| 2 | Radar ×2 + cavi inclusi | [Dytabepl HLK-LD2450 TEST KIT](https://www.amazon.it/dp/B0GQRJWB6P) | 31,90 |
| 3 | Jumper misti M-M / M-F / F-F, 120 pz 20 cm | [Kit jumper](https://www.amazon.it/dp/B0FGPNMDCL) | 12,99 |

Con questi tre il nodo funziona. Niente altro è necessario.

### B. Server — **0 €**, sta su Fly.io

Niente da comprare: il backend gira in cloud su Fly.io. Il Pi Zero 2 W resta di
scorta (utile se un giorno vorrai un fallback locale in sala). **Cancellata la
microSD da 19,90 €.**

### C. Comodità — **8,06 €**

| # | Pezzo | Prodotto | € |
|---|---|---|---|
| 5 | Cavo USB-A→C 3 m (2 pezzi) | [RAVIAD 2 pz, 3 m](https://www.amazon.it/dp/B09P59217Z) | 8,06 |

Serve per alimentare il nodo sul tavolo tenendo il caricatore al muro. **Per
flashare usa invece il cavo dati del telefono che hai già**: molti cavi economici
portano solo corrente e non i dati, e con quelli la scheda non si programma.

### A-bis. Variante schermo: striscia lunga e stretta

| Pezzo | Prodotto | € |
|---|---|---|
| Display + ESP32 5,79" | [Elecrow CrowPanel ESP32 E-Paper 5.79"](https://www.amazon.it/dp/B0FX4PDW6M) | 55,94 |

**In alternativa al 4.2", non in aggiunta.** Confronto:

| | 4.2" (B0G43FCHFX) | 5.79" (B0FX4PDW6M) |
|---|---|---|
| Risoluzione | 400×300 | **272×792** |
| Area attiva | 84,8 × 63,6 mm | **139 × 47,7 mm** (rapporto 2,9:1) |
| Pixel pitch | 0,212 mm | **0,1755 mm** (testo più nitido) |
| Pixel totali | 120.000 | **215.000** |
| Driver | SSD1683 | SSD1683 ×2 (due metà) |
| **GPIO liberi** | **12** (10 usabili) | **6** (5 usabili: IO3, IO9, IO15, IO17, IO21) |
| Prezzo | 52,24 | 55,94 |

Tutto il resto è identico: stesso ESP32-S3-WROOM-1-N8R8, 8+8 MB, refresh
parziale, BAT, slot TF, USB-C, stesse librerie.

**Quale prendere**: sulla tovaglia apparecchiata la striscia ingombra meno ed è
più elegante, e il QR ci sta comodo a destra con tutto il testo a sinistra. Ma se
pensi di attaccarci anche l'audio, i 5 GPIO del 5.79" finiscono subito (2 al
radar + 3 all'I2S = esattamente 5, zero margine). Col 4.2" hai il doppio di pin.

### A-quater. CrowPanel 2.13": l'unico che sta nei 3-4 x 6-8 cm

[ELECROW 2.13" ESP32 E-Ink 122x250](https://www.amazon.it/dp/B0H1VDH1R5) —
**33,24 €**, consegna gio 24, 4,3 stelle.

**La scheda misura 6,3 x 3,1 cm**: è esattamente la misura che chiedevi, ed è
anche il più economico e il più veloce ad arrivare.

**Cosa ha identico al 4.2"**: ESP32-S3, 8 MB flash + 8 MB PSRAM, Wi-Fi 2,4 GHz,
BLE, refresh parziale, connettore batteria con ricarica, case acrilico bianco,
Arduino / ESP-IDF / MicroPython.

**Sorpresa buona — qui i 5 V ci sono.** Il connettore **UART0** porta
`GND | 5V | TXD | RXD` (lo dice la serigrafia, "Power in"). Il radar si attacca
con **un connettore solo**, alimentazione compresa. Sul 4.2" l'header dà solo
3V3 e questo non si poteva fare.

**Cosa perdi**

| | |
|---|---|
| ❌ GPIO | **solo 2**: GPIO_D = IO40 e IO41. Niente audio I2S (vuole 3 pin) |
| ❌ microSD | **non c'è lo slot** (il 4.2" e il 5.79" ce l'hanno) |
| ⚠️ Schermo | visibile **5,7 x 2,8 cm**, 250x122 px: QR + tavolo + vino + totale, **niente lista piatti** |
| ⚠️ Connettore | UART0 è un JST con guscio, **passo 2,54** (misurato sulla foto con la USB-C come riferimento): vedi sotto |

Sul 4.2", invece, il radar entra diretto coi jumper sull'header 2,54: zero
accessori, zero giunzioni.

**Come scegliere**

| Se... | Prendi |
|---|---|
| la misura 3-4 x 6-8 cm è il prodotto | **2.13"** (33,24 €) |
| vuoi l'audio | **4.2"** (52,24 €) |
| vuoi la striscia lunga | **5.79"** (55,94 €) |

Il 2.13" costa così poco che prenderlo **insieme** al 4.2" (85,48 € in tutto) è
un modo onesto di decidere la forma provandola sul tavolo vero, invece che a
tavolino.

#### Come attaccare il radar al 2.13" — cosa serve davvero

Il connettore **UART0** porta `GND | 5V | TXD | RXD` con i pin a **passo 2,54**,
lo stesso dei Dupont. Dall'altra parte l'LD2450 ha un **header maschio 2×4 già
saldato**, anche lui a 2,54.

**Prima prova senza comprare niente.** Se il guscio di plastica dell'UART0 è
basso, i jumper **F-F** del kit che hai già preso entrano direttamente sui 4 pin,
e l'altro capo va sull'header del radar. Fine: **zero euro, zero saldature**.

```
CrowPanel UART0        LD2450 (header 2x4)
  GND  <--- F-F --->   GND
  5V   <--- F-F --->   5V      <-- qui i 5 V ci sono davvero
  TXD  <--- F-F --->   RX      <-- incrociati!
  RXD  <--- F-F --->   TX
```

**Se il guscio non li fa entrare** (i JST-XH sono spesso stretti), serve il
cavetto con la spina giusta:

| Pezzo | Prodotto | € |
|---|---|---|
| Cavo JST XH 2.54 4 pin, 25 paia, 150 mm | [B0CBWZNP6Z](https://www.amazon.it/dp/B0CBWZNP6Z) | 8,99 |

Il capo opposto è a fili nudi: si uniscono al radar con **4 saldature** (o 4
[WAGO](https://www.amazon.it/dp/B077QJ3G5B) da 8,90 € se non vuoi saldare).

⚠️ **Trappola software**: UART0 sull'ESP32-S3 è IO43/IO44, la porta da cui esce
anche il log di boot. In Arduino IDE va messo **USB CDC On Boot = Enabled**, così
`Serial` va sulla USB e la seriale del radar resta pulita. Senza questo, i
messaggi di debug finiscono addosso al radar.

#### Conto finale delle due strade

| | Da comprare | Saldature |
|---|---|---|
| **2.13"** | 33,24 + 8,99 (cavo XH, se serve) = **42,23 €** | 4 |
| **4.2"** | **52,24 €**, il radar entra diretto coi jumper | 0 |

Dieci euro di differenza, più la misura che volevi.

### A-ter. Audio: cosa comprare esattamente

Nessun CrowPanel ha audio a bordo. Servono due pezzi, entrambi con consegna
ven 25 set:

| Pezzo | Prodotto | € |
|---|---|---|
| Amplificatore I2S | [MAX98357A Ulegqin — **2 pezzi**](https://www.amazon.it/dp/B0H5BY33B8) | 7,99 |
| Altoparlante | [CQRobot 3 W 4 Ω **chiuso**, 70×31 mm](https://www.amazon.it/dp/B0822Z4LPH) | 10,99 |

**Totale audio: 18,98 €** (e ti avanza un amplificatore di scorta).

Prendi l'altoparlante **chiuso** (con la sua cassettina): un cono nudo dentro una
scatola stampata gracchia e basta.

#### Collegamento — 5 fili, si innestano tutti

```
MAX98357A            CrowPanel 4.2"
  VIN   ---------->  3V3
  GND   ---------->  GND
  DIN   ---------->  IO8
  BCLK  ---------->  IO15
  LRC   ---------->  IO14
  (SD e GAIN si lasciano scollegati: la scheda ha i pull-up giusti)

Altoparlante  ---->  morsetti + / - sull'uscita dell'amplificatore
```

Pin dopo aver messo radar + audio sul 4.2": occupati IO16, IO17 (radar) e
IO8, IO14, IO15 (audio). **Liberi: IO3, IO9, IO18, IO21, IO38.**

#### Tre cose da sapere

1. **Va a 3,3 V.** Il MAX98357A lavora da 2,5 a 5,5 V, quindi i 3V3 dell'header
   bastano: escono circa 0,7-1 W invece di 3 W. Per un din-don al tavolo è
   abbondante, per coprire il brusio di una sala piena no.
2. **Non serve la microSD.** Un suono da 1-2 secondi lo si impacchetta nella
   flash da 8 MB del modulo. La SD serve solo se un giorno vorrai frasi parlate.
3. **Solo col 4.2".** Sul 5.79" ci sono 5 GPIO usabili: 2 al radar + 3 all'audio
   = esattamente 5, zero margine per qualsiasi altra cosa.

Possibile saldatura: se l'uscita altoparlante di quella scheda è a piazzole e non
a morsetto a vite, sono **2 punti** di saldatura sui fili dello speaker.

### Totali

| Scenario | € |
|---|---|
| Nodo col 4.2" (minimo per lavorare) | **97,13** |
| Nodo con la striscia 5.79" | **100,83** |
| 4.2" + cavo lungo | **105,19** |
| 5.79" + cavo lungo | **108,89** |
| ...+ audio (MAX98357A + altoparlante) | **+15** |

Il server non costa hardware: sta su Fly.io.

### ❌ NON comprare adesso

| Pezzo | Perché no | Quando |
|---|---|---|
| LD2410C (11,87 €) | vuole 5 V che l'header non ha | se emergono buchi di presenza statica |
| WAGO 221 (8,90 €) | servono solo per la presa 5 V | insieme all'LD2410C |
| PIR HC-SR501 / SR602 | ridondante, il radar lo batte | mai, probabilmente |
| microSD per lo slot del CrowPanel | opzionale, ci va bene una vecchia | quando vorrai il logo offline |
| Batteria LiPo | connettore SH1.0, facile sbagliare modello | dopo, per le demo senza cavi |
| Header 2×20, alimentatore 5V/3A | erano del piano col Pi come nodo | mai |

### Già in casa (verifica prima di ordinare)
- Caricatore USB da telefono (5 V, 1 A basta e avanza)
- Un cavo USB-C **dati** corto, per flashare
- Pi Zero 2 W → fa il server
- Stampante 3D — **se hai solo PLA, aggiungi un rotolo di PETG** (~20 €): sul
  tavolo d'estate il PLA si affloscia

## 4-bis. Collegamenti definitivi (4 fili, tutti a innesto)

```
LD2450                    CrowPanel (header 2x10)
  3.3V  ------------------>  3V3      (riga B, pin 7-10)
  GND   ------------------>  GND      (riga A, pin 7-10)
  TX    ------------------>  IO16     (RX del nodo, UART1)
  RX    ------------------>  IO17     (TX del nodo, UART1)

NON usare IO19 / IO20: sono USB D-/D+ dell'ESP32-S3.
Il pin 5V del radar resta scollegato: lo alimentiamo dai 3.3V.
```

Da misurare al volo con un calibro quando arriva: il passo dell'header 2×4 del
radar. Dalla foto sembra il classico 2,54 mm; se fosse 2,0 mm non importa, usi il
cavo JST incluso che finisce in Dupont femmina.

## 4-bis-2. Perché da tre sensori siamo passati a uno

Partivamo da PIR + LD2450 + LD2410C. Ora c'è solo l'LD2450. I due che mancano non
sono stati tagliati per lo stesso motivo:

| Sensore | Cosa doveva fare | Verdetto |
|---|---|---|
| **LD2450** | contare le persone e vederle muovere | **resta**: è l'unico che sa contare |
| **PIR HC-SR501** | trigger istantaneo quando qualcuno si siede | **era ridondante** |
| **LD2410C** | presenza di chi sta fermo (respiro) | **rimandato**, per i 5 V |

### Il PIR era ridondante, non sacrificato

Nel piano col Raspberry il PIR aveva un senso preciso: *svegliare* il sistema,
costare due euro e reagire all'istante. Ma l'LD2450 è **sempre acceso e campiona
10 volte al secondo**: non c'è niente da svegliare, e vede il movimento meglio e
prima di un PIR. Sul piano nuovo il PIR farebbe lo stesso lavoro, peggio.

Nota: se lo rivolessi comunque, **non prendere l'HC-SR501** (vuole 4,5–20 V, che
sull'header non ci sono). Serve un **SR602** (3,3–15 V, ~5,60 €), che va dai 3V3
del CrowPanel. Ma ti direi di no: non aggiunge nulla.

### L'LD2410C è l'unica cosa che perdiamo davvero

È l'unico che vede la persona **perfettamente immobile** (rileva il respiro).
L'LD2450 su quello ha dei buchi — lo dice anche una recensione Vine. Non l'ho
tolto perché inutile, ma perché **vuole 5 V e l'header del CrowPanel dà solo
3V3**. Rimediabile in tre modi, quando serve:

| Come | Costo | Saldature |
|---|---|---|
| Presa 5 V da uno spezzone USB + WAGO | 12 € (sensore) + 9 € (WAGO) | 0 |
| Presa 5 V + fili saldati sui suoi pad | 12 € + 3 € breakout USB | 3 |
| Niente: ci pensa il filtro software | 0 | 0 |

Il filtro software c'è già: dopo l'ultimo segnale tengo la presenza "calda" per
**3 minuti** (`SEC_ABBANDONO` in `backend/state.py`). A tavola si muovono le
posate, i bicchieri, si gesticola: tre minuti di silenzio assoluto in sei persone
non capitano. Il caso critico è il tavolo da due che finisce di mangiare e resta
immobile a chiacchierare — e lì ce lo diranno le prove sul campo, non io adesso.

### Uso migliore del secondo radar

Il kit Dytabepl ne contiene **due**. Prima di comprare un terzo sensore diverso,
il secondo LD2450 vale molto di più: montandone due schiena contro schiena
copri **240° invece di 120°**, che è il vero limite sul tavolo tondo. L'ESP32-S3
ha 3 UART, quindi il secondo va su IO18/IO21 senza aggiungere nulla.

**Piano: si parte con un radar, si prova su un tavolo vero, e poi si decide se
serve il secondo LD2450 (copertura) o l'LD2410C (presenza statica).** Comprarli
adesso tutti e tre è indovinare al buio.

---

## 4-ter. Se te la senti di saldare qualcosa

Il piano qui sopra resta a **zero saldature**: non l'ho scelto perché non sai
saldare, ma perché il CrowPanel costa la metà, si accende all'istante, non ha una
microSD che si corrompe e può andare a batteria. Quelle ragioni valgono comunque.

**Precisazione su una cosa che ho scritto prima**: le 40 saldature del pettine
GPIO su un Pi Zero sono *noiose*, non *difficili*. Header passante da 2,54 mm in
fila: è letteralmente il primo esercizio che si fa. Avevo dato loro troppo peso
come argomento — la scelta del CrowPanel si regge da sola.

### Cosa ti sblocca davvero saper saldare

Tre reti di sicurezza, tutte su pad grossi e comodi:

| Quando | Cosa saldi | Punti |
|---|---|---|
| L'header 2×4 del radar è a 2,0 mm e i Dupont non entrano | 4 fili sui pad passanti `GND / RX / TX` + 3.3V | **4** |
| Un cavetto JST si sfila o si rompe (succede, sono minuscoli) | gli stessi 4 fili, direttamente | **4** |
| Più avanti vuoi l'LD2410C per la presenza statica | 3 fili sui suoi pad + presa 5 V da un USB | **3** |

Con il saldatore in casa, nessuno di questi tre casi è più un problema che ti
ferma per due giorni in attesa di un pacco. È esattamente il motivo per cui è
comodo saperlo fare: non per il piano A, per i piani B.

### E se volessi comunque il Pi Zero come nodo?

È una scelta legittima, e ha un vantaggio vero: l'agent Python in `device/` è
**già scritto e funzionante**, ci fai SSH e modifichi il codice a caldo, senza
riflashare. Il prezzo onesto:

| | CrowPanel ESP32 | Pi Zero 2 W come nodo |
|---|---|---|
| Da comprare | 52,24 € | display 44,15 + header ~7 + PSU 9,99 + microSD 19,90 = **~81 €** |
| Saldature | 0 | **~40** (noiose ma facili) + 4 per il radar |
| Accensione | istantanea | ~25 s di boot Linux |
| Rischio a fine serata | nessuno | microSD che si corrompe se stacchi la corrente |
| Batteria | connettore LiPo a bordo | no |
| Linguaggio nodo | C++ Arduino (o MicroPython) | Python, già pronto |

Se scegli il Pi, si torna alla lista della rev. 1 e il software non lo tocco
nemmeno. Dimmelo prima di ordinare: cambia solo quale display compri.

**Nota**: anche sul CrowPanel il Python non è buttato — la scheda supporta
**MicroPython**, e la logica di lettura del radar in `device/sensors.py` si porta
quasi riga per riga.

---

## 5. Impatto sul software (già scritto, cambia poco)

L'architettura che ho fatto manda al nodo un **view-model JSON** e il nodo lo
disegna. Su ESP32 non ho Pillow né la libreria QR, quindi **sposto il disegno sul
server**: il backend genera il bitmap 1-bit 400×300 (**15 KB**) e lo spinge sulla
WebSocket, l'ESP32 lo scrive e basta.

- Il renderer Python che ho già scritto e testato **resta com'è**, gira sul server
- Il firmware ESP32 diventa banale: Wi-Fi, WebSocket, blit del buffer, lettura
  UART del radar, lettura di un GPIO. **~200 righe di Arduino C++**
- Dashboard, motore vini, macchina a stati, pagina menu del cliente:
  **non cambia una riga**

Se poi vuoi tornare al Pi come nodo, il protocollo è lo stesso: si riattiva
l'agent Python che è già in `device/`.

---

## 5-bis. Il server sta su Fly.io: cosa cambia

Il backend non gira più in sala ma in cloud. Tre conseguenze buone e tre da
gestire.

**Buone**
- Niente microSD, niente Pi da tenere acceso in un locale pubblico
- Il QR diventa un **URL pubblico https**: il cliente lo inquadra e ordina **senza
  dover entrare nel Wi-Fi del ristorante**, gli basta il 4G. È un miglioramento
  vero rispetto al server locale
- Aggiornare il menu, i vini e la dashboard è un deploy, non un giro fra i tavoli

**Da gestire**
- **Il nodo deve parlare `wss://` con TLS.** L'ESP32-S3 lo fa (`WiFiClientSecure`),
  ma va messo in conto nel firmware: certificato root e un po' di RAM. Con 8 MB di
  PSRAM non è un problema, è solo lavoro.
- **`auto_stop_machines` su Fly va disattivato.** Con le WebSocket aperte serve
  `min_machines_running = 1` in `fly.toml`, altrimenti la macchina si ferma e i
  nodi si riconnettono in loop.
- **Lo stato oggi è in RAM.** `Sala` tiene i tavoli in memoria: su Fly un
  redeploy o un riavvio della macchina azzera i tavoli aperti a metà servizio.
  Per l'MVP può bastare, ma prima di metterlo in un locale vero serve una
  persistenza minima (SQLite su volume Fly, o Postgres).
- **Se cade la linea del ristorante il tavolo è cieco.** Mitigazione naturale:
  l'e-ink **tiene l'ultima schermata anche senza corrente né rete**, quindi il QR
  resta leggibile e i clienti continuano a ordinare dal 4G. A disallinearsi è
  solo la cassa.

---

## 6. Scatola stampata in 3D

Più semplice di prima: il CrowPanel è un blocco unico con la sua scocca, devi solo
fargli il supporto inclinato e la tasca per i radar.

- **PETG, non PLA.** Tavolo di ristorante d'estate + piatto caldo = PLA moscio.
- **Niente filamento con carbonio o metallo**: schermano il radar.
- Davanti all'LD2450: **parete piena 1,5-2 mm**, nessuna griglia, nessuna vite
  metallica nel cono. Radar **verticale**, rivolto ai commensali.
- Display inclinato **20-25°**: si legge da seduti senza riflessi.
- Alzalo **20-30 cm** dal piano se il tavolo è tondo: il cono scavalca i piatti.
- Lascia accessibile la **USB-C** (serve per riflashare) e il foro cavo dietro/basso.
