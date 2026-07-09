const API = "";
let connectionId = null;
let activeHeadPtr = null;
let passphrase = null;   // session-only; never sent to the server
let tableKey = null;     // AES-GCM key for the currently open table

// ---- End-to-end encryption (WebCrypto AES-GCM) --------------------------------------------
// A sealed record is: iv(12) || ciphertext || tag(16), base64-encoded into payload_b64. The
// gateway and device only ever see this ciphertext. The per-table key is PBKDF2(passphrase,
// salt = SHA-256("edb-e2e-v1:" + head_ptr)). The salt is deterministic (not per-install random),
// so the same passphrase decrypts a table on any machine; passphrase strength is the defense.

const enc = new TextEncoder();
const dec = new TextDecoder();

function bytesToB64(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return btoa(s);
}

function b64ToBytes(b64) {
  const s = atob(b64);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}

async function deriveTableKey(headPtr) {
  const saltBits = await crypto.subtle.digest("SHA-256", enc.encode("edb-e2e-v1:" + headPtr));
  const material = await crypto.subtle.importKey(
    "raw", enc.encode(passphrase), "PBKDF2", false, ["deriveKey"]
  );
  return crypto.subtle.deriveKey(
    { name: "PBKDF2", salt: new Uint8Array(saltBits), iterations: 250000, hash: "SHA-256" },
    material,
    { name: "AES-GCM", length: 256 },
    false,
    ["encrypt", "decrypt"]
  );
}

function recordAad(headPtr) {
  return enc.encode("edb-table:" + headPtr);
}

async function sealRecord(headPtr, plaintextBytes) {
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const ct = new Uint8Array(await crypto.subtle.encrypt(
    { name: "AES-GCM", iv, additionalData: recordAad(headPtr) }, tableKey, plaintextBytes
  ));
  const blob = new Uint8Array(iv.length + ct.length);
  blob.set(iv, 0);
  blob.set(ct, iv.length);
  return bytesToB64(blob);
}

async function openRecord(headPtr, payloadB64) {
  const blob = b64ToBytes(payloadB64);
  const iv = blob.slice(0, 12);
  const ct = blob.slice(12);
  const pt = await crypto.subtle.decrypt(
    { name: "AES-GCM", iv, additionalData: recordAad(headPtr) }, tableKey, ct
  );
  return new Uint8Array(pt);
}

function bytesToHex(bytes) {
  return Array.from(bytes).map((b) => b.toString(16).padStart(2, "0")).join("");
}

async function api(path, opts = {}) {
  const r = await fetch(API + path, {
    headers: { "Content-Type": "application/json", ...(opts.headers || {}) },
    ...opts,
  });
  const body = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(body.detail || r.statusText);
  return body;
}

document.getElementById("btn-connect").onclick = async () => {
  const mock = document.getElementById("mock").checked;
  const port = mock ? "mock" : document.getElementById("port").value;
  const body = {
    transport: "serial",
    port,
    baud: +document.getElementById("baud").value,
    encrypt: document.getElementById("encrypt").checked,
    mock,
  };
  try {
    const res = await api("/connections", { method: "POST", body: JSON.stringify(body) });
    connectionId = res.id;
    document.getElementById("conn-status").textContent = "Connected: " + connectionId;
    document.getElementById("btn-disconnect").disabled = false;
    document.getElementById("unlock-panel").classList.remove("hidden");
    document.getElementById("tables-panel").classList.remove("hidden");
    await loadTables();
  } catch (e) {
    document.getElementById("conn-status").textContent = "Error: " + e.message;
  }
};

document.getElementById("btn-disconnect").onclick = async () => {
  if (connectionId) {
    await api("/connections/" + connectionId, { method: "DELETE" });
    connectionId = null;
    passphrase = null;
    tableKey = null;
    document.getElementById("conn-status").textContent = "Disconnected";
    document.getElementById("btn-disconnect").disabled = true;
    document.getElementById("unlock-panel").classList.add("hidden");
    document.getElementById("tables-panel").classList.add("hidden");
    document.getElementById("records-panel").classList.add("hidden");
  }
};

document.getElementById("btn-unlock").onclick = async () => {
  const pass = document.getElementById("passphrase").value;
  if (!pass) return;
  passphrase = pass;
  tableKey = activeHeadPtr !== null ? await deriveTableKey(activeHeadPtr) : null;
  document.getElementById("unlock-status").textContent =
    "Unlocked (records encrypt/decrypt locally; passphrase never leaves this page)";
  if (activeHeadPtr !== null) await refreshRecords();
};

async function loadTables() {
  const res = await api("/connections/" + connectionId + "/tables");
  const ul = document.getElementById("table-list");
  ul.innerHTML = "";
  (res.data?.tables || []).forEach((t) => {
    const li = document.createElement("li");
    const btn = document.createElement("button");
    btn.textContent = `head ${t.head_ptr} (${t.label || "table"})`;
    btn.onclick = () => openTable(t.head_ptr);
    li.appendChild(btn);
    ul.appendChild(li);
  });
}

async function openTable(headPtr) {
  activeHeadPtr = headPtr;
  document.getElementById("table-label").textContent = "@" + headPtr;
  document.getElementById("records-panel").classList.remove("hidden");
  if (passphrase !== null) tableKey = await deriveTableKey(headPtr);
  await refreshRecords();
}

async function refreshRecords() {
  const res = await api(
    "/connections/" + connectionId + "/tables/" + activeHeadPtr + "/records?limit=100"
  );
  const tbody = document.querySelector("#records-table tbody");
  tbody.innerHTML = "";
  for (const rec of res.data?.records || []) {
    let cell;
    if (tableKey && rec.payload_b64) {
      try {
        const pt = await openRecord(activeHeadPtr, rec.payload_b64);
        cell = `<code>${bytesToHex(pt)}</code> <span class="muted">(decrypted)</span>`;
      } catch (e) {
        cell = `<code>${rec.payload_b64}</code> <span class="muted">(decrypt failed)</span>`;
      }
    } else {
      cell = `<code>${rec.payload_b64 || ""}</code>`;
    }
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${rec.recno}</td><td>${cell}</td><td></td>`;
    tbody.appendChild(tr);
  }
}

document.getElementById("btn-refresh").onclick = refreshRecords;

document.getElementById("btn-create").onclick = async () => {
  await api("/connections/" + connectionId + "/tables", {
    method: "POST",
    body: JSON.stringify({
      head_ptr: +document.getElementById("create-head").value,
      table_size: +document.getElementById("create-size").value,
      rec_size: +document.getElementById("create-rec").value,
    }),
  });
  await loadTables();
};

function hexToBytes(hex) {
  const clean = hex.replace(/\s+/g, "");
  const out = new Uint8Array(clean.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(clean.substr(i * 2, 2), 16);
  return out;
}

document.getElementById("btn-append").onclick = async () => {
  const raw = document.getElementById("append-payload").value.trim();
  let payload_b64;
  if (tableKey) {
    // Unlocked: input is hex plaintext, encrypted locally before it leaves the browser.
    payload_b64 = await sealRecord(activeHeadPtr, hexToBytes(raw));
  } else {
    payload_b64 = raw; // plaintext mode: input is raw base64
  }
  await api("/connections/" + connectionId + "/tables/" + activeHeadPtr + "/records", {
    method: "POST",
    body: JSON.stringify({ payload_b64 }),
  });
  document.getElementById("append-payload").value = "";
  await refreshRecords();
};
