# Da mandare a chi amministra il server

Stesso schema del centralino, che su quella macchina funziona già. Sostituisci
la chiave con la tua (`cat ~/.ssh/leggimenu_tavolo.pub`).

---

Ciao,

sul server **65.21.93.36** mi serve un secondo ambiente come quello del
centralino, per un altro prototipo. Stessa impostazione, così non devi
inventarti niente di nuovo.

## 1. Utente in gabbia

```bash
useradd -m -s /bin/bash tavolo
```

Come `centralino`: confinato, senza root, senza possibilità di aprire tunnel,
e che non veda gli altri domini.

## 2. Chiave pubblica

```bash
mkdir -p /home/tavolo/.ssh && chmod 700 /home/tavolo/.ssh
echo 'INCOLLA_QUI_LA_CHIAVE' >> /home/tavolo/.ssh/authorized_keys
chmod 600 /home/tavolo/.ssh/authorized_keys
chown -R tavolo:tavolo /home/tavolo/.ssh
```

Solo chiave, niente password. È una chiave dedicata a questo progetto, diversa
da quella del centralino.

## 3. Ambiente Python

```bash
mkdir -p /home/tavolo/app && chown tavolo:tavolo /home/tavolo/app
su - tavolo -c "python3 -m venv /home/tavolo/.venv"
```

I pacchetti li installo io da lì, non servono pacchetti di sistema.

## 4. Il servizio, in `/etc/systemd/system/tavolo.service`

Come per il centralino: lo crei tu una volta e poi non lo tocchiamo più. Io
pubblico copiando i file e toccando `RICARICA`, e il servizio si ricambia i
worker da solo.

```ini
[Unit]
Description=leggimenu tavolo
After=network.target

[Service]
User=tavolo
WorkingDirectory=/home/tavolo/app
Environment=RICARICA_ATTIVA=1
Environment=RICARICA_FILE=/home/tavolo/app/RICARICA
Environment=LEGGIMENU_SEGRETO=METTI_QUI_UN_SEGRETO
ExecStart=/home/tavolo/.venv/bin/gunicorn \
    --workers 1 \
    --worker-class uvicorn.workers.UvicornWorker \
    --bind 127.0.0.1:8099 \
    --timeout 0 \
    tools.radar_live:app
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

Due cose su questo file, che se cambiate rompono tutto in modo poco evidente:

**`--workers 1` deve restare 1.** L'applicazione tiene in memoria lo stato del
tavolo e la connessione col dispositivo: con più worker ognuno avrebbe il suo
stato e il dispositivo parlerebbe con uno solo. Non è un problema di carico —
si tratta di un tavolo, non di un sito.

**`--timeout 0`** perché il dispositivo resta collegato per ore: col timeout
normale gunicorn lo considererebbe bloccato e lo ucciderebbe.

Il segreto puoi generarlo con `openssl rand -hex 16` e passarmelo, oppure dirmi
di sceglierlo io e te lo mando.

## 5. Sottodominio

**tavolo.leggimenu.it** (o quello che preferisci), che inoltri a
`127.0.0.1:8099` — stesso meccanismo di `centralino.leggimenu.it`.

Rispetto al centralino c'è una differenza che conta: questa applicazione usa i
**WebSocket**, quindi il proxy deve lasciar passare l'upgrade del protocollo.
Su Apache serve `mod_proxy_wstunnel` e una riga in più:

```apache
ProxyPreserveHost On
RewriteEngine On
RewriteCond %{HTTP:Upgrade} =websocket [NC]
RewriteRule /(.*) ws://127.0.0.1:8099/$1 [P,L]
ProxyPass        / http://127.0.0.1:8099/
ProxyPassReverse / http://127.0.0.1:8099/
```

Se manca l'upgrade non si vede nessun errore: la pagina si apre e resta ferma
per sempre.

Grazie!
