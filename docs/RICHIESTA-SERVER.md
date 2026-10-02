# Da mandare a chi amministra il server

L'ambiente c'e' gia': utente `centralino`, dominio `iot.leggimenu.it`, chiave
installata. L'applicazione e' copiata in `~/app-tavolo`, gira e risponde su
`127.0.0.1:8099`. Mancano due cose, entrambe da root.

---

Ciao,

l'applicazione del prototipo e' pronta in `/home/centralino/app-tavolo` e
risponde gia' su `127.0.0.1:8099` (verificato: `curl` da dentro restituisce
200). Mi servono due cose che da utente non posso fare.

## 1. L'inoltro da iot.leggimenu.it

Oggi quel dominio serve la pagina segnaposto da `public_html`. Va inoltrato
all'applicazione, come fai per `centralino.leggimenu.it` verso la 8000, ma
verso la **8099**.

C'e' una differenza rispetto al centralino che conta: questa applicazione usa i
**WebSocket**, quindi serve `mod_proxy_wstunnel` e la regola per l'upgrade.

```apache
ProxyPreserveHost On
RewriteEngine On
RewriteCond %{HTTP:Upgrade} =websocket [NC]
RewriteRule /(.*) ws://127.0.0.1:8099/$1 [P,L]
ProxyPass        / http://127.0.0.1:8099/
ProxyPassReverse / http://127.0.0.1:8099/
ProxyTimeout 3600
```

Se l'upgrade non passa non si vede nessun errore: la pagina si apre e resta
ferma per sempre. E `ProxyTimeout` lungo perche' il dispositivo resta collegato
per ore.

## 2. Il servizio, come per il centralino

In `/etc/systemd/system/tavolo.service`:

```ini
[Unit]
Description=leggimenu tavolo
After=network.target

[Service]
User=centralino
WorkingDirectory=/home/centralino/app-tavolo
Environment=PORTA=8099
Environment=ASCOLTA=127.0.0.1
Environment=RICARICA_ATTIVA=1
Environment=RICARICA_FILE=/home/centralino/app-tavolo/RICARICA
Environment=LEGGIMENU_SEGRETO=d4ead05485c7314566aa66379a7692d2
ExecStart=/home/centralino/app-tavolo/.venv/bin/gunicorn \
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

```bash
systemctl daemon-reload && systemctl enable --now tavolo
```

Tre righe di quel file, se cambiate, rompono tutto in modo poco evidente:

- **`--workers 1` deve restare 1.** L'applicazione tiene in memoria lo stato del
  tavolo e la connessione col dispositivo: con piu' worker ognuno avrebbe il suo
  stato e il dispositivo parlerebbe con uno solo. Non e' una questione di carico,
  si tratta di un tavolo.
- **`--timeout 0`** perche' il dispositivo resta collegato per ore: col timeout
  normale gunicorn lo crederebbe bloccato e lo ucciderebbe.
- **`ASCOLTA=127.0.0.1`** perche' altrimenti si arriva alla 8099 anche in
  diretta, saltando il proxy, e l'applicazione mostrerebbe a chiunque le pagine
  riservate alla sala.

Da li' in poi pubblico da solo: copio i file e tocco `RICARICA`, e gunicorn
ricambia i worker. Niente root, come per il centralino.

Grazie!
