const apHost = "http://192.168.4.1";
const mdnsHost = "http://esp32-control.local";
const storageKey = "esp32-control-host";
const bluetoothServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const bluetoothRxCharacteristicUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const connectionCandidates = [apHost, mdnsHost];

const hostInput = document.querySelector("#host-input");
const pingButton = document.querySelector("#ping-button");
const saveButton = document.querySelector("#save-button");
const mdnsButton = document.querySelector("#mdns-button");
const apButton = document.querySelector("#ap-button");
const autoConnectButton = document.querySelector("#auto-connect-button");
const bluetoothButton = document.querySelector("#bluetooth-button");
const installButton = document.querySelector("#install-button");
const installHelp = document.querySelector("#install-help");
const statusTitle = document.querySelector("#status-title");
const statusDetail = document.querySelector("#status-detail");
const statusBadge = document.querySelector("#status-badge");
const angleSlider = document.querySelector("#angle-slider");
const angleReadout = document.querySelector("#angle-readout");
const centerButton = document.querySelector("#center-button");
const stepButtons = [...document.querySelectorAll(".step-button")];

let lastKnownAngle = 90;
let sendTimer = null;
let pollingTimer = null;
let deferredInstallPrompt = null;
let bluetoothDevice = null;
let bluetoothRxCharacteristic = null;
let activeTransport = "wifi";

function normalizeHost(value) {
  const trimmedValue = value.trim();
  if (!trimmedValue) {
    return apHost;
  }

  const withProtocol = /^https?:\/\//i.test(trimmedValue)
    ? trimmedValue
    : `http://${trimmedValue}`;

  return withProtocol.replace(/\/+$/, "");
}

function getHost() {
  return normalizeHost(hostInput.value);
}

function setHost(value) {
  hostInput.value = normalizeHost(value);
}

function saveHost() {
  const host = getHost();
  localStorage.setItem(storageKey, host);
  hostInput.value = host;
  showStatus("Saved", `Using ${host}`, "idle");
}

function showStatus(title, detail, tone) {
  statusTitle.textContent = title;
  statusDetail.textContent = detail;
  statusBadge.textContent =
    tone === "online" ? "Online" : tone === "error" ? "Error" : "Idle";
  statusBadge.className = `status-badge status-${tone}`;
}

function updateAngleReadout(angle) {
  lastKnownAngle = Number(angle);
  angleSlider.value = String(lastKnownAngle);
  angleReadout.textContent = `${lastKnownAngle}°`;
}

async function requestJSON(path, options = {}) {
  const response = await fetch(`${getHost()}${path}`, {
    headers: {
      "Content-Type": "application/json",
      ...(options.headers ?? {}),
    },
    ...options,
  });

  if (!response.ok) {
    throw new Error(`Request failed with ${response.status}`);
  }

  return response.json();
}

async function checkHost(host) {
  setHost(host);
  const payload = await requestJSON("/api/status");
  localStorage.setItem(storageKey, host);
  return payload;
}

function describeStatusPayload(payload) {
  const modeLabel = payload.mode === "ap" ? "ESP32 hotspot" : "home Wi‑Fi";
  return `Connected by ${modeLabel}. Servo is at ${payload.angle ?? lastKnownAngle}°.`;
}

async function refreshStatus() {
  if (activeTransport === "bluetooth" && bluetoothDevice?.gatt?.connected) {
    showStatus(
      "Connected over Bluetooth",
      `Bluetooth is ready. Servo is at ${lastKnownAngle}°.`,
      "online"
    );
    return;
  }

  activeTransport = "wifi";

  try {
    const payload = await requestJSON("/api/status");
    updateAngleReadout(payload.angle ?? lastKnownAngle);
    showStatus("Connected", describeStatusPayload(payload), "online");
  } catch (error) {
    showStatus(
      "ESP32 not reachable yet",
      "Join the ESP32-Control Wi‑Fi network on your iPhone, then tap Auto connect. No IP typing needed.",
      "error"
    );
  }
}

async function autoConnect() {
  showStatus(
    "Looking for ESP32…",
    "Trying the ESP32 hotspot first, then the home Wi‑Fi .local name.",
    "idle"
  );

  for (const host of connectionCandidates) {
    try {
      const payload = await checkHost(host);
      updateAngleReadout(payload.angle ?? lastKnownAngle);
      activeTransport = "wifi";
      showStatus("Auto connected", describeStatusPayload(payload), "online");
      return;
    } catch (error) {
      // Try the next known address.
    }
  }

  setHost(apHost);
  showStatus(
    "Tap your ESP32 Wi‑Fi first",
    "Open iPhone Settings → Wi‑Fi → ESP32-Control, then return here and tap Auto connect again.",
    "error"
  );
}

function bluetoothSupported() {
  return Boolean(navigator.bluetooth);
}

function handleBluetoothDisconnected() {
  bluetoothRxCharacteristic = null;
  if (activeTransport === "bluetooth") {
    activeTransport = "wifi";
  }
  showStatus("Bluetooth disconnected", "Use Wi‑Fi or tap Connect Bluetooth again.", "idle");
}

async function connectBluetooth() {
  if (!bluetoothSupported()) {
    showStatus(
      "Bluetooth unavailable here",
      "iPhone Safari does not support Web Bluetooth. Use Wi‑Fi on iPhone, or try Chrome/Edge on a supported device.",
      "error"
    );
    return;
  }

  try {
    showStatus("Choose your ESP32", "Pick ESP32-Control-BLE from the Bluetooth chooser.", "idle");
    bluetoothDevice = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: "ESP32-Control" }],
      optionalServices: [bluetoothServiceUuid],
    });

    bluetoothDevice.addEventListener("gattserverdisconnected", handleBluetoothDisconnected);
    const server = await bluetoothDevice.gatt.connect();
    const service = await server.getPrimaryService(bluetoothServiceUuid);
    bluetoothRxCharacteristic = await service.getCharacteristic(bluetoothRxCharacteristicUuid);
    activeTransport = "bluetooth";
    showStatus(
      "Connected over Bluetooth",
      "Move the slider or tap Center to send angles over BLE.",
      "online"
    );
  } catch (error) {
    showStatus(
      "Bluetooth connection failed",
      "Make sure the ESP32 sketch is running and Bluetooth is not already connected elsewhere.",
      "error"
    );
  }
}

async function sendBluetoothAngle(angle) {
  if (!bluetoothRxCharacteristic) {
    throw new Error("Bluetooth is not connected");
  }

  const payload = new TextEncoder().encode(`${angle}\n`);
  await bluetoothRxCharacteristic.writeValue(payload);
}

async function sendAngle(angle) {
  const safeAngle = Math.max(0, Math.min(180, Math.round(angle)));
  updateAngleReadout(safeAngle);

  if (activeTransport === "bluetooth" && bluetoothRxCharacteristic) {
    try {
      await sendBluetoothAngle(safeAngle);
      showStatus("Angle sent by Bluetooth", `Servo updated to ${safeAngle}°.`, "online");
      return;
    } catch (error) {
      showStatus("Bluetooth send failed", "Reconnect Bluetooth or use Wi‑Fi.", "error");
      return;
    }
  }

  try {
    const payload = await requestJSON("/api/servo", {
      method: "POST",
      body: JSON.stringify({ angle: safeAngle }),
    });

    updateAngleReadout(payload.angle ?? safeAngle);
    showStatus("Angle sent by Wi‑Fi", `Servo updated to ${payload.angle ?? safeAngle}°.`, "online");
  } catch (error) {
    showStatus(
      "Send failed",
      "Join ESP32-Control Wi‑Fi and tap Auto connect, or connect with Bluetooth on a supported browser.",
      "error"
    );
  }
}

function queueAngleSend(angle) {
  updateAngleReadout(angle);
  clearTimeout(sendTimer);
  sendTimer = setTimeout(() => {
    sendAngle(angle);
  }, 90);
}

function nudgeAngle(step) {
  const nextAngle = Math.max(0, Math.min(180, lastKnownAngle + step));
  queueAngleSend(nextAngle);
}

async function handleInstallClick() {
  installHelp.hidden = false;

  if (deferredInstallPrompt) {
    deferredInstallPrompt.prompt();
    await deferredInstallPrompt.userChoice;
    deferredInstallPrompt = null;
  }
}

function boot() {
  setHost(localStorage.getItem(storageKey) || apHost);
  updateAngleReadout(lastKnownAngle);
  showStatus(
    "Not connected yet",
    "Tap Auto connect to try the ESP32 hotspot and local Wi‑Fi addresses.",
    "idle"
  );

  window.addEventListener("beforeinstallprompt", (event) => {
    event.preventDefault();
    deferredInstallPrompt = event;
  });

  pingButton.addEventListener("click", refreshStatus);
  saveButton.addEventListener("click", saveHost);
  mdnsButton.addEventListener("click", () => setHost(mdnsHost));
  apButton.addEventListener("click", () => setHost(apHost));
  autoConnectButton.addEventListener("click", autoConnect);
  bluetoothButton.addEventListener("click", connectBluetooth);
  installButton.addEventListener("click", handleInstallClick);
  centerButton.addEventListener("click", () => queueAngleSend(90));

  angleSlider.addEventListener("input", (event) => {
    queueAngleSend(Number(event.currentTarget.value));
  });

  stepButtons.forEach((button) => {
    button.addEventListener("click", () => {
      nudgeAngle(Number(button.dataset.step));
    });
  });

  if ("serviceWorker" in navigator) {
    navigator.serviceWorker.register("./sw.js").catch(() => {});
  }

  pollingTimer = setInterval(refreshStatus, 5000);
}

boot();
