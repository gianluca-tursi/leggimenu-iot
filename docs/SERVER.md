# Mettere il server su internet

Oggi il server gira sul Mac a cui è attaccato il nodo. Funziona, ma l'indirizzo
cambia, serve un tunnel, e il QR va rifatto ogni volta.

Con il server su una macchina tua l'indirizzo è fisso per sempre.

## Come resta diviso il lavoro

```
nodo ESP32  --USB-->  Mac (ponte)  --internet-->  tuo server  -->  telefoni
```

L'ESP32 **non usa il WiFi**: la sua connessione al mondo è il cavo USB e il Mac.
In fiera è un vantaggio — una sola macchina da far funzionare, e la rete
instabile del posto la subisce solo lei.

Il Mac diventa un tramite stupido: legge la seriale, la rigira al server, e
riporta indietro i comandi. Tutto il resto — menu, cassa, ordini — vive sul
server.

## Cosa serve

```
1 vCPU, 1 GB di RAM        (bastano anche 512 MB)
Debian o Ubuntu LTS
Python 3.11+
un sottodominio, es. tavolo.leggimenu.it
```

**L'hosting condiviso non va bene**: serve un processo sempre acceso e i
WebSocket aperti. Ci vuole una VPS con accesso root.

## Sul server, una volta sola

```bash
sudo apt update && sudo apt install -y python3-venv git
sudo git clone https://github.com/gianluca-tursi/leggimenu-iot.git /opt/leggimenu
cd /opt/leggimenu && sudo python3 -m venv .venv
sudo .venv/bin/pip install -r requirements.txt
```

Un segreto condiviso, senza il quale chiunque conosca l'indirizzo può fingersi
il tuo tavolo e riempire la cassa di ordini inventati:

```bash
openssl rand -hex 16          # annotalo, serve anche sul Mac
```

Il servizio, in `/etc/systemd/system/leggimenu.service`:

```ini
[Unit]
Description=leggimenu
After=network.target

[Service]
WorkingDirectory=/opt/leggimenu
Environment=LEGGIMENU_SEGRETO=IL_SEGRETO_DI_SOPRA
ExecStart=/opt/leggimenu/.venv/bin/python tools/radar_live.py
Restart=always

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now leggimenu
```

Davanti ci va un reverse proxy con certificato. Con **Caddy** sono tre righe e
il certificato se lo prende da solo:

```
tavolo.leggimenu.it {
    reverse_proxy localhost:8080
}
```

Caddy inoltra i WebSocket senza doverglielo dire. **Con nginx invece va detto**,
e se lo dimentichi la cassa non si aggiorna e sembra rotta:

```nginx
location / {
    proxy_pass http://localhost:8080;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header X-Forwarded-For $remote_addr;
}
```

Quell'ultima riga non è un dettaglio: è così che il server distingue chi arriva
da internet da chi è in casa. Senza, la cassa resterebbe aperta al mondo.

## Sul Mac, ogni volta

```bash
./scripts/dashboard.sh ferma      # il server adesso sta altrove
SERVER=wss://tavolo.leggimenu.it LEGGIMENU_SEGRETO=... ./scripts/ponte.sh
```

Il ponte si riconnette da solo se la rete cade, aspettando sempre un po' di
più fino a mezzo minuto, e tiene sveglio il Mac finché gira.

Conviene metterli in un file che non finisce su GitHub:

```bash
echo 'export SERVER=wss://tavolo.leggimenu.it'   >> .env.locale
echo 'export LEGGIMENU_SEGRETO=...'              >> .env.locale
source .env.locale && ./scripts/ponte.sh
```

## Dopo

Nel QR va il sottodominio fisso, e non si tocca più. La cassa si apre dal
server su `/cassa`, raggiungibile solo dalla rete del locale — quindi dal
Mac del banco va aperta in locale, oppure va aggiunta una password.
