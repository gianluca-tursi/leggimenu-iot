# Da mandare a chi amministra il server

Testo pronto da inoltrare. Sostituisci la chiave pubblica con la tua
(`cat ~/.ssh/leggimenu_server.pub`) e il sottodominio se ne preferisci un altro.

---

Ciao,

sul server **65.21.93.36** (AlmaLinux 10, DirectAdmin) mi serve far girare una
piccola applicazione Python per un prototipo. Consuma pochissimo — una
cinquantina di MB di RAM — e non tocca niente di quello che c'è già.

Ti chiedo quattro cose.

## 1. Un utente limitato

```bash
useradd -m -s /bin/bash leggimenu
loginctl enable-linger leggimenu
```

Il `linger` serve perché l'applicazione giri come servizio di quell'utente
senza bisogno di root: così non serve dargli nessun privilegio.

## 2. La chiave pubblica

```bash
mkdir -p /home/leggimenu/.ssh && chmod 700 /home/leggimenu/.ssh
echo 'INCOLLA_QUI_LA_CHIAVE_PUBBLICA' >> /home/leggimenu/.ssh/authorized_keys
chmod 600 /home/leggimenu/.ssh/authorized_keys
chown -R leggimenu:leggimenu /home/leggimenu/.ssh
```

Solo chiave, niente password.

## 3. Una porta libera

L'applicazione ascolta su **127.0.0.1:8099**, solo in locale. Ho verificato che
la 8099 è libera (la 8080 è di httpd). Da fuori non è raggiungibile: ci arriva
solo nginx.

## 4. Un sottodominio su nginx

**tavolo.leggimenu.apserver.it**, che inoltri a `127.0.0.1:8099`.

Dato che c'è DirectAdmin, immagino convenga crearlo dal pannello e aggiungere
la configurazione personalizzata dove il pannello se l'aspetta — quella parte
la lascio a te, che conosci l'impianto.

Tre direttive però sono indispensabili, e se mancano il guasto non è evidente:

```nginx
location / {
    proxy_pass http://127.0.0.1:8099;

    # senza questi due, i WebSocket non passano e la pagina resta ferma
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";

    # senza questo, l'applicazione crede che ogni visitatore arrivi dalla rete
    # interna e gli mostra anche le pagine riservate
    proxy_set_header X-Forwarded-For $remote_addr;
    proxy_set_header Host $host;

    # il dispositivo resta collegato per ore: il timeout standard lo taglierebbe
    proxy_read_timeout 3600s;
}
```

## Quello che faccio io

Dopo, da quell'utente e senza privilegi: cartella `/home/leggimenu/app`,
ambiente Python, e un servizio utente che si riavvia da solo.

Niente root, niente pacchetti di sistema, niente modifiche a siti esistenti.

Grazie!
