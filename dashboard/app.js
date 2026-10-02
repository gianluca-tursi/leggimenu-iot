/* Dashboard cassa: stato live via websocket, azioni via REST. */
const S = { tavoli: new Map(), menu: [], eventi: [], scelto: null };

const $ = (s) => document.querySelector(s);
const api = (url, body) =>
  fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: body ? JSON.stringify(body) : null,
  }).then((r) => (r.ok ? r.json() : r.json().then((e) => Promise.reject(e))));

const ETICHETTA_STATO = {
  libero: "libero", presenza: "si stanno sedendo", aperto: "aperto",
  in_servizio: "in servizio", conto: "conto", pulizia: "da pulire",
};
const ETICHETTA_PIATTO = {
  ordinato: "ordinato", in_preparazione: "in cucina", consegnato: "servito",
};
const PROSSIMO_STATO = {
  ordinato: "in_preparazione", in_preparazione: "consegnato", consegnato: "ordinato",
};

/* ---------- websocket ---------- */
function connetti() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${proto}://${location.host}/ws/dashboard`);

  ws.onopen = () => { $("#led").classList.add("on"); $("#conn").textContent = "in linea"; };
  ws.onclose = () => {
    $("#led").classList.remove("on");
    $("#conn").textContent = "riconnessione…";
    setTimeout(connetti, 1500);
  };
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.tipo === "snapshot") {
      m.tavoli.forEach((t) => S.tavoli.set(t.id, t));
      S.menu = m.menu; S.eventi = m.eventi || [];
      if (!S.scelto) S.scelto = m.tavoli[0]?.id || null;
      riempiSelect(); disegnaTutto(); caricaAnteprima();
    } else if (m.tipo === "tavolo") {
      S.tavoli.set(m.tavolo.id, m.tavolo);
      if (m.eventi) S.eventi = m.eventi;
      disegnaTutto();
    } else if (m.tipo === "anteprima" && m.tavolo_id === S.scelto) {
      caricaAnteprima();
    }
  };
  setInterval(() => ws.readyState === 1 && ws.send("ping"), 20000);
}

/* ---------- render ---------- */
function disegnaTutto() { disegnaTavoli(); disegnaEventi(); }

function disegnaTavoli() {
  const cont = $("#tavoli");
  cont.innerHTML = "";
  for (const t of S.tavoli.values()) cont.appendChild(schedaTavolo(t));
}

function schedaTavolo(t) {
  const n = $("#tpl-tavolo").content.cloneNode(true);
  const art = n.querySelector(".tavolo");
  art.classList.add("s-" + t.stato);
  if (t.id === S.scelto) art.classList.add("scelto");
  art.onclick = (e) => {
    if (e.target.closest("button") || e.target.closest(".st")) return;
    S.scelto = t.id; $("#sel-tavolo").value = t.id; disegnaTavoli(); caricaAnteprima();
  };

  n.querySelector(".t-nome").textContent = t.nome;
  n.querySelector(".t-sala").textContent = t.sala;
  const b = n.querySelector(".badge");
  b.textContent = ETICHETTA_STATO[t.stato] || t.stato;
  b.classList.add("b-" + t.stato);

  const s = t.sensori || {};
  n.querySelector(".pir").classList.toggle("attivo", !!s.movimento);
  n.querySelector(".radar").classList.toggle("attivo", !!s.presenza);
  n.querySelector(".radar").textContent = s.presenza ? `radar ${s.target}` : "radar";
  n.querySelector(".nodo").classList.toggle("attivo", !!t.device_online);
  n.querySelector(".t-coperti").innerHTML = t.stato === "libero" ? "" :
    `coperti <b>${t.coperti}</b>${t.coperti_confermati ? "" : " stimati"}`;

  const ul = n.querySelector(".t-ordine");
  for (const r of t.ordine) {
    const li = document.createElement("li");
    li.innerHTML = `<span class="q">${r.quantita}×</span><span class="n">${r.nome}</span>`;
    const st = document.createElement("span");
    st.className = "st " + r.stato;
    st.textContent = ETICHETTA_PIATTO[r.stato];
    st.title = "clicca per avanzare lo stato";
    st.onclick = () => api(`/api/tavoli/${t.id}/righe/${r.id}`, { stato: PROSSIMO_STATO[r.stato] });
    li.appendChild(st);
    ul.appendChild(li);
  }

  if (t.abbinamento) {
    const v = n.querySelector(".t-vino");
    v.classList.add("on");
    const a = t.abbinamento;
    const prezzo = a.formato_consigliato === "bottiglia" ? a.bottiglia : a.calice;
    v.innerHTML = `<div class="v-tit">🍷 ${a.nome} <span style="opacity:.6">${a.annata || ""}</span></div>
      <div class="v-motivo">${a.motivo} · ${a.quantita_consigliata}× ${a.formato_consigliato} · ${prezzo.toFixed(2)} €</div>`;
    if (!t.abbinamento_accettato) {
      const az = document.createElement("div");
      az.className = "v-azioni";
      az.innerHTML = `<button class="btn mini primario">Accettato</button><button class="btn mini">Altro vino</button>`;
      az.children[0].onclick = () => api(`/api/tavoli/${t.id}/vino?accetta=true`);
      az.children[1].onclick = () => api(`/api/tavoli/${t.id}/vino?accetta=false`);
      v.appendChild(az);
    } else {
      v.innerHTML += `<div class="v-motivo" style="color:var(--ok)">✓ accettato dal tavolo</div>`;
    }
  }

  n.querySelector(".t-totale").textContent = t.ordine.length ? `${t.totale.toFixed(2)} €` : "";
  const az = n.querySelector(".t-azioni");
  const bottone = (testo, fn, primario) => {
    const el = document.createElement("button");
    el.className = "btn mini" + (primario ? " primario" : "");
    el.textContent = testo; el.onclick = fn; az.appendChild(el);
  };
  if (t.stato === "libero") bottone("Apri", () => api(`/api/tavoli/${t.id}/apri`), true);
  if (["presenza", "aperto", "in_servizio"].includes(t.stato)) {
    bottone("−", () => api(`/api/tavoli/${t.id}/coperti`, { coperti: Math.max(1, t.coperti - 1) }));
    bottone("+", () => api(`/api/tavoli/${t.id}/coperti`, { coperti: t.coperti + 1 }));
  }
  if (["aperto", "in_servizio"].includes(t.stato)) bottone("Conto", () => api(`/api/tavoli/${t.id}/conto`));
  if (t.stato !== "libero") bottone("Chiudi", () => api(`/api/tavoli/${t.id}/chiudi`));
  return n;
}

function disegnaEventi() {
  const ul = $("#eventi");
  ul.innerHTML = "";
  for (const e of S.eventi) {
    const li = document.createElement("li");
    li.className = "l-" + (e.livello || "info");
    const ora = new Date(e.ts * 1000).toLocaleTimeString("it-IT", { hour12: false });
    li.innerHTML = `<time>${ora}</time><span>${e.testo}</span>`;
    ul.appendChild(li);
  }
}

function riempiSelect() {
  const st = $("#sel-tavolo");
  st.innerHTML = "";
  for (const t of S.tavoli.values()) st.add(new Option(t.nome, t.id));
  st.value = S.scelto;
  st.onchange = () => { S.scelto = st.value; disegnaTavoli(); caricaAnteprima(); };

  const sp = $("#sel-piatto");
  sp.innerHTML = "";
  for (const p of S.menu) sp.add(new Option(`${p.nome} — ${p.prezzo.toFixed(2)} €`, p.id));
}

function caricaAnteprima() {
  if (!S.scelto) return;
  const img = $("#anteprima");
  const url = `/api/tavoli/${S.scelto}/anteprima.png?t=${Date.now()}`;
  fetch(url).then((r) => {
    if (!r.ok) throw 0;
    img.src = url; img.style.display = "block";
    $("#hint-anteprima").textContent = "esattamente quello che vedono al tavolo";
  }).catch(() => {
    img.style.display = "none";
    $("#hint-anteprima").textContent = "nodo non collegato: avvia device/agent.py";
  });
}

/* ---------- simulatore sensori ---------- */
let simAttivo = false, simTimer = null;
function inviaSensori(presente) {
  if (!S.scelto) return;
  const n = +$("#sim-target").value;
  return api(`/api/tavoli/${S.scelto}/sensori`, {
    movimento: presente, presenza: presente, target: presente ? n : 0,
    distanze_cm: presente ? Array.from({ length: n }, (_, i) => 55 + i * 12) : [],
  });
}
function avviaSim(presente) {
  simAttivo = presente;
  inviaSensori(presente);
  clearInterval(simTimer);
  if (presente && $("#sim-auto").checked) simTimer = setInterval(() => inviaSensori(true), 2000);
}

$("#sim-seduti").onclick = () => avviaSim(true);
$("#sim-vanno").onclick = () => avviaSim(false);
$("#sim-target").oninput = (e) => {
  $("#sim-n").textContent = e.target.value;
  if (simAttivo) inviaSensori(true);
};
$("#sim-auto").onchange = () => avviaSim(simAttivo);
$("#btn-ordina").onclick = () =>
  api(`/api/tavoli/${S.scelto}/ordina`, { piatto_id: $("#sel-piatto").value, quantita: 1 });

connetti();
