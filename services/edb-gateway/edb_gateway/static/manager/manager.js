const API = "";
let connectionId = null;
let activeHeadPtr = null;
let masterKey = null;

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
    masterKey = null;
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
  const enc = new TextEncoder();
  const material = await crypto.subtle.importKey(
    "raw", enc.encode(pass), "PBKDF2", false, ["deriveBits"]
  );
  const bits = await crypto.subtle.deriveBits(
    { name: "PBKDF2", salt: enc.encode("edb-manager"), iterations: 100000, hash: "SHA-256" },
    material, 256
  );
  masterKey = await crypto.subtle.importKey("raw", bits, { name: "AES-GCM" }, false, ["encrypt", "decrypt"]);
  document.getElementById("unlock-status").textContent = "Keys derived (session only, not sent to server)";
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
  await refreshRecords();
}

async function refreshRecords() {
  const res = await api(
    "/connections/" + connectionId + "/tables/" + activeHeadPtr + "/records?limit=100"
  );
  const tbody = document.querySelector("#records-table tbody");
  tbody.innerHTML = "";
  (res.data?.records || []).forEach((rec) => {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td>${rec.recno}</td><td><code>${rec.payload_b64 || ""}</code></td><td></td>`;
    tbody.appendChild(tr);
  });
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

document.getElementById("btn-append").onclick = async () => {
  const payload_b64 = document.getElementById("append-payload").value;
  await api("/connections/" + connectionId + "/tables/" + activeHeadPtr + "/records", {
    method: "POST",
    body: JSON.stringify({ payload_b64 }),
  });
  await refreshRecords();
};
