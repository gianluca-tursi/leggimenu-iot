# Note per chi lavora su questo progetto

Scritte per un'altra sessione di Claude che apre il progetto senza averlo
visto nascere. Leggi anche il [README](README.md) per cosa fa e come si avvia.

## Come parlare con Gianluca

Scrive in italiano, rispondi in italiano. Il marchio è **leggimenu**, sempre
tutto minuscolo.

È un neofita di saldature ma non di tecnologia: vuole capire *perché*, non solo
cosa fare. Le spiegazioni tecniche vanno bene, il gergo gratuito no.

Lavora sull'hardware mentre tu lavori sul codice: se gli chiedi di spostare un
cavo, aspetta conferma prima di dare per scontato che sia fatto. **Verifica
sempre quale scheda è sull'USB** prima di caricare qualcosa:

```bash
esptool --port /dev/cu.wchusbserial* chip_id
# ESP32-D0WD-V3 = Freenove (radar, LED)
# ESP32-S3      = CrowPanel (pannello e-ink)
```

Le due schede si alternano sulla stessa porta USB, e caricare il firmware
sbagliato fallisce ma fa perdere un giro di cavi. Sono già stati tanti.
`./scripts/flash.sh <cartella>` controlla il chip da solo prima di scrivere.

**Tocca solo quello che chiede.** Se per fare una cosa ne va cambiata
un'altra, anche "è la stessa riga", chiediglielo *prima*. Dopo ogni modifica
guarda il diff e riprova il flusso intero, non solo il pezzo cambiato: lui
prova tutto sull'hardware e le rotture le scopre da solo ("ma che fai, una
cosa e ne rompi un'altra?"). È successo correggendo il vino: è sparita la
schermata dell'ordine.

**Due Mac.** Quello di sviluppo è di Gianluca: lì si modifica, si caricano i
firmware, si pubblica il server. Quello della fiera fa solo `git pull` e il
ponte, istruzioni in [docs/MAC-FIERA.md](docs/MAC-FIERA.md). Se sei sul Mac
della fiera, il firmware non si carica da lì.

**Segreti fuori dal repository**, che è pubblico: il segreto del ponte
(`LEGGIMENU_SEGRETO`), la password della cassa (sul server in
`dati/password.txt`), la password del WiFi (la scrive lui nella dashboard),
`credenziali.h`, la chiave SSH `~/.ssh/leggimenu_tavolo`. Non scriverli in
chat né in file versionati.

## Errori già fatti, da non rifare

Questi sono costati ore. Sono tutti annotati anche nel codice dove servono.

**Il radar a 3,3V.** Perdeva frame in modo intermittente e avevo diagnosticato
connettore difettoso, poi regolatore termico, poi modulo rotto. Era
l'alimentazione: va a **5V**. Le prime tre diagnosi erano tutte sbagliate.

**Il bersaglio fantasma.** Il firmware ribatteva l'ultimo bersaglio decodificato
quando il radar taceva: produceva persone finte, distanze finte, tutto
plausibile. Risolto con una scadenza di 600 ms. Se vedi dati "troppo stabili",
sospetta sempre che siano vecchi.

**`qrcode_initText()` mente.** Restituisce "tutto bene" anche quando il testo
non ci sta: scrive oltre lo spazio e produce un QR corrotto che a vederlo
sembra perfetto. La versione va scelta da tabella di capacità (c'è in
`pannello42.ino`), mai dal valore di ritorno.

**Il driver e-ink di Elecrow conta male.** `EPD_Clear` e `EPD_DisplayImage`
usavano `EPD_W * EPD_H / 8`, che sbaglia quando il lato corto non è multiplo di
8. In verticale lasciava ~12 righe di pixel mai riscritte, con sopra il disegno
precedente. Usare `ALLSCREEN_BYTES`.

**`pinMode()` stacca la UART.** Su ESP32 riconfigura il pin come GPIO normale.
Avevo messo `pinMode(RX, INPUT_PULLUP)` dopo `Serial1.begin()` per togliere il
rumore e ho ucciso la ricezione del pannello. Si usa `gpio_set_pull_mode`.

**Sentinelle negative diverse.** `MOSTRA_WIFI = -7` e "nessuna richiesta" `-1`,
e un `>= 0` scritto per escludere la seconda buttava via anche la prima. Il
comando arrivava e non succedeva niente. Confrontare col valore, non col segno.

**`pgrep -f radar_live.py` trova fantasmi.** Il pattern matcha anche la shell
che esegue il comando in cui la stringa compare, e `pkill -f` può ammazzare la
shell chiamante. Per questo la dashboard usa un file di PID
(`/tmp/leggimenu-dashboard.pid`) e si governa con `scripts/dashboard.sh`.

**Due dashboard insieme.** Su macOS due processi aprono la stessa seriale senza
errore e si dividono i byte: nessun frame arriva intero e sembra il radar
rotto. Ora `dashboard.sh` lo impedisce. Vale anche per dashboard locale e
ponte: mai insieme.

**Troncare i byte invece del testo.** Il server faceva `.encode()[:40]` su
una riga già con il `\n` in fondo: il taglio mangiava l'a capo e la riga
dopo si incollava ("...struttY"), e il vino non compariva mai. Si accorcia
il testo con `accorcia()`, a parole intere, poi si codifica.

**Buffer di riga troppo corti.** Il pannello leggeva le righe dal nodo con
64 caratteri, e l'annuncio arriva su una riga sola (`D:titolo|riga1|riga2`):
la coda spariva ("inquadra il QR per" e basta). Ora sono 128. Il nodo ne
accetta 79 (`cmdBuf[80]`): `al_pannello()` sul server calcola lo spazio
rimasto. Se allunghi un comando, controlla tutti e due i buffer.

**L'ultima schermata arrivata vince.** Il pannello ridisegna al massimo ogni
4 secondi e disegna l'ultima richiesta: il vino, che arriva un decimo di
secondo dopo l'ordine, cancellava l'ordine. Il vino ora aspetta
(`vinoInAttesa`). Stessa trappola col battito del nodo: `T:<n>` ogni 15 s
sovrascriveva una schermata speciale chiesta ma non ancora disegnata. Se
aggiungi una schermata, pensa a cosa succede se arriva un altro comando nei
4 secondi prima del disegno.

**Prova al banco contaminata.** Con la CrowPanel sull'USB per caricarla, il
nodo può restare acceso attraverso il filo dati e continua a mandare `*L`
ogni 15 secondi: il pannello torna a "libero" da solo e sembra un bug.

## Il metodo che funziona

Quando qualcosa sull'hardware non va, **riproduci il pezzo sospetto sul Mac**
invece di tirare a indovinare sul dispositivo. Il bug del QR l'ho trovato
compilando la stessa libreria del firmware nativamente e decodificando quello
che produceva: cinque minuti, senza toccare un cavo. Prima avevo fatto
smontare e rimontare tutto quattro volte seguendo ipotesi sbagliate.

Vale anche al contrario: il nodo tiene un **registro sulla flash** (comando `L`
sulla seriale, o `./scripts/scarica-log.sh`) con dentro anche i bersagli
scartati e il motivo. Serve per ritarare i filtri sui dati veri.

## Il problema che conta davvero

Il radar manda **un bersaglio solo** anche con tre persone al tavolo. Misurato
su 5437 frame: mai due bersagli entro 90 cm, e la traccia si sposta di 1 cm
mediano fra un frame e l'altro — non esita fra due persone, ne insegue una
sola e ignora le altre.

È fisica dell'LD2450, non codice: cono di 120°, e separazione per distanza
(~36 cm) o velocità radiale, non per angolo. Al centro di un tavolo rotondo
sono tutti alla stessa distanza: il caso peggiore possibile.

Tre strade, nessuna ancora scelta:

1. **sensore sul bordo** anziché al centro — i commensali finiscono a distanze
   diverse dentro un unico ventaglio. Costa zero, si prova subito, ma si perde
   l'oggetto al centro del tavolo che è l'idea di partenza
2. **tre sensori a 120°** — mantiene il centro, ~30 € e tre UART (l'ESP32 ne ha
   tre), ma è una settimana di lavoro
3. **ridimensionare** — il dispositivo apre il tavolo, i coperti li conferma il
   cameriere dalla dashboard con un tocco

Prima di scegliere manca una misura: **due persone davanti al sensore a
distanze chiaramente diverse** (30 cm e 80 cm). Se lì ne vede due, il limite è
solo geometrico e la strada 1 basta.

## Dove va a finire (deciso con Gianluca)

Il gestionale cassa (`/cassa`) e' il primo pezzo di una cosa piu' grande. Nella
testa sua, alla fine il tavolo dovra' anche:

- **promuovere eventi futuri** nelle giornate scarse, sul pannello a tavolo
  libero o a fine pasto - offerte ed eventi si mandano dalla cassa
- **misurare i tempi di gestione del tavolo** - quanto si aspetta prima di
  ordinare, quanto dura il servizio. La durata gia' si registra alla
  liberazione, manca lo storico fra un servizio e l'altro
- **QR della recensione Google** a fine tavolo - fatto, parte col conto
- **QR per pagare il conto** a fine pasto
- **quanti telefoni** hanno inquadrato il QR a quel tavolo - gia' fatto, si
  contano i dispositivi distinti con un cookie
- **abbinamento del vino dopo il primo ordine**, ma solo se il vino non c'e'
  gia' nell'ordine - fatto, con `backend/wine.py`. Manca ancora una regola
  migliore del "ha senso proporlo"

## Server e ponte

Il server gira su **iot.leggimenu.it** (gunicorn, 1 worker, dietro
Cloudflare). Si pubblica dal Mac di sviluppo con `./scripts/pubblica.sh`:
copia i file e tocca `RICARICA`, il server si ricarica da solo senza root.
I dati del gestore (eventi, immagini, password) stanno sul server in
`dati/`, fuori dal repository.

Il Freenove non ha WiFi: `scripts/ponte.sh` sul Mac a cui è attaccato legge
la seriale e la rigira al server su `/ws/nodo`, con il segreto condiviso.
Il server, ricevuto il ponte, lo tratta come una seriale locale (`Ponte` in
`radar_live.py`): stesse righe, stessi comandi.

## Le schermate del pannello

| schermata | quando | quanto resta |
|---|---|---|
| tavolo prenotato | `L` | finché non si apre |
| benvenuto + QR menu | `T:<n>` | sempre: è quella di ritorno |
| WiFi | primo QR inquadrato | 15 s |
| ordine inviato | `Z` `V:` `X` | 15 s |
| vino | `B:` `C:` `Y` dopo l'ordine, solo se non c'è vino | 30 s, dopo l'ordine |
| offerta / evento | `D:titolo\|riga1\|riga2` | 15 s |
| recensione | `E` (il conto) | finché qualcuno non la toglie |

Le durate sono costanti in cima a `pannello42.ino`. **MENU** rimette il QR
del menu. **EXIT** avvia la demo a scorrimento: tutte le schermate, 12 s
l'una, in loop, con contenuti scritti nel firmware; i dati veri vengono messi
da parte e rimessi, e i comandi che arrivano intanto si registrano.

I testi lunghi vanno a capo misurando i pixel (`dueRighe`, `stringiA`),
mai contando i caratteri: contandoli finivano fuori dallo schermo.

## Cosa manca

- **Il radar non vede più nessuno** (`[grezzi] nessun bersaglio`): da
  controllare orientamento, ostacoli davanti, 5V. Intanto la cassa ha
  "Apri il tavolo a mano".
- **URL della recensione**: è un segnaposto (`trattoria-demo`), va messo
  quello vero dalla dashboard.
- **QR per pagare il conto** e **storico dei tempi** fra un servizio e
  l'altro: non ancora fatti.
- **`aggiorna.sh` e `stato.sh`** sono scritti per il vecchio schema col
  server sul Mac: `aggiorna.sh` avvia la dashboard locale, che col ponte
  litiga per la seriale. Sul Mac della fiera si usa `git pull`.
- **WiFi sul pannello**: oggi il nodo arriva al server solo via USB e Mac.
  Il CrowPanel ha il WiFi mai acceso: potrebbe diventare lui il ponte.
- **Filo di ritorno pannello → nodo** (IO17 → 26): rotto, da ricontrollare.
  Ci passa solo la diagnostica, il funzionamento non ne dipende.
- **Modalità fiera**: alla Maker Faire la folla attorno allo stand apre il
  tavolo in continuazione. Serve una portata più corta o un'apertura diversa,
  e va provata prima, non lì.
- **L'indirizzo del QR**: con il server su iot.leggimenu.it l'indirizzo è
  fisso; si imposta una volta dalla dashboard e il pannello lo tiene in
  memoria. Se il QR porta a un IP, è rimasto quello vecchio.

## Dettagli che servono e non si deducono

**Pin del Freenove**: 16 radar RX, 17 radar TX, 13 LED, 25 → pannello,
26 ← pannello, 0 tasto BOOT (pianta un segnalibro nel registro).
Liberi: 18, 19, 21, 22, 23, 27, 32, 33.

**Pin del CrowPanel 4.2"**: e-ink su 48/47/46/45/12/11, alimentazione pannello
su **IO7** (non documentata, senza quella BUSY non risponde mai).
Tasti: **IO1 = EXIT, IO2 = MENU**, IO4/5/6 = comando centrale a più direzioni.
Verificati premendoli uno per uno. Evitare IO19 e IO20: sono l'USB nativo.

**La classe del pannello è `GxEPD2_420_SE0420NQ04`**, non quella che si trova
negli esempi in rete (`GYE042A87`): con quella il BUSY non risponde e l'init
muore in "Busy Timeout!". L'indizio sta scritto sul flat del display.

**Ogni comando verso il pannello porta un `*` davanti.** Il filo, con il nodo
spento o scollegato, raccoglie disturbi: prima o poi il rumore componeva per
caso una `L` — comando valido di una lettera — e il pannello tornava a "tavolo
libero" da solo, in continuazione. Sembrava bloccato. Il pannello accetta
comandi anche dall'USB, per provare le schermate senza rimontare il filo.

**Protocollo nodo ↔ pannello** (righe di testo, leggibili con un monitor):
`T:<n>` coperti, `L` libero, `W` schermata WiFi, `Z`/`V:<riga>`/`X` ordine,
`B:<nome>`/`C:<motivo>`/`Y` vino, `D:titolo|riga1|riga2` annuncio,
`E:<url>`/`E` recensione, `N:` `O:` `G:` `U:` `Q:` prenotazione e QR,
`Q` rimetti il QR del menu, `?` ping.
Sul nodo, da seriale: `R` ritara, `F` libera il tavolo, `P` ping, `S` stato,
`L` scarica il registro, `C` cancella, `M` segnalibro, `A:<secondi>` pausa,
`K:<n>` coperti decisi dalla cassa.
**Ogni comando è una riga.** C'era un parser misto e mangiava mezzo comando.
