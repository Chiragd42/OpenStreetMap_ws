(() => {
  "use strict";

  const query = new URLSearchParams(window.location.search);
  const backendBase = (query.get("backend") || "http://127.0.0.1:8080").replace(/\/$/, "");
  const minimumHouseZoom = 17;
  const renderBatchSize = 1000;
  const maximumCacheEntries = 20;

  function queryNumber(name, fallback) {
    const raw = query.get(name);
    if (raw === null || raw.trim() === "") return fallback;
    const value = Number(raw);
    return Number.isFinite(value) ? value : fallback;
  }

  const elements = {
    count: document.getElementById("houseCount"),
    zoom: document.getElementById("zoomValue"),
    toggle: document.getElementById("showHouses"),
    status: document.getElementById("status"),
    threshold: document.getElementById("thresholdWarning"),
    tileWarning: document.getElementById("tileWarning"),
    refresh: document.getElementById("refreshButton"),
    backend: document.getElementById("backendAddress"),
  };

  elements.backend.textContent = backendBase.replace(/^https?:\/\//, "");

  const renderer = L.canvas({ padding: 0.4, tolerance: 6 });
  const map = L.map("map", {
    center: [queryNumber("lat", 48.7758), queryNumber("lon", 9.1829)],
    zoom: Math.max(3, Math.min(19, queryNumber("zoom", 18))),
    preferCanvas: true,
    renderer,
  });

  L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 19,
    attribution: "&copy; OpenStreetMap contributors",
    referrerPolicy: "origin",
  })
    .on("tileerror", () => { elements.tileWarning.hidden = false; })
    .addTo(map);

  const housesLayer = L.layerGroup().addTo(map);
  const viewportCache = new Map();
  let activeRequest = null;
  let refreshTimer = null;
  let requestNumber = 0;
  let renderNumber = 0;

  function setStatus(message, state = "") {
    elements.status.textContent = message;
    elements.status.dataset.state = state;
  }

  function formatCount(value) {
    return Number(value || 0).toLocaleString("de-DE");
  }

  function addressText(house) {
    const street = [house.street, house.house_number].filter(Boolean).join(" ");
    const city = [house.postcode, house.city].filter(Boolean).join(" ");
    return [street, city].filter(Boolean).join(", ");
  }

  function popupFor(house) {
    const wrapper = document.createElement("div");
    const title = document.createElement("strong");
    title.textContent = house.name || addressText(house) || house.building || "Building";
    const details = document.createElement("div");
    const address = addressText(house);
    details.textContent = [address, house.building ? `Type: ${house.building}` : ""]
      .filter(Boolean)
      .join(" · ") || "No additional information";
    const id = document.createElement("div");
    id.className = "popup-id";
    id.textContent = house.id || "OSM object";
    wrapper.append(title, details, id);
    return wrapper;
  }

  async function drawHouses(houses, currentRender) {
    housesLayer.clearLayers();
    for (let start = 0; start < houses.length; start += renderBatchSize) {
      if (currentRender !== renderNumber || !elements.toggle.checked) return;
      const end = Math.min(start + renderBatchSize, houses.length);
      for (let index = start; index < end; index += 1) {
        const house = houses[index];
        if (!Number.isFinite(house.lat) || !Number.isFinite(house.lon)) continue;
        L.circleMarker([house.lat, house.lon], {
          renderer,
          radius: 4,
          color: "#003f9e",
          fillColor: "#2a7fff",
          fillOpacity: 0.82,
          weight: 1,
        })
          .bindPopup(() => popupFor(house), { maxWidth: 280 })
          .addTo(housesLayer);
      }
      if (end < houses.length) {
        await new Promise((resolve) => window.requestAnimationFrame(resolve));
      }
    }
  }

  function viewport() {
    const bounds = map.getBounds();
    return [bounds.getWest(), bounds.getSouth(), bounds.getEast(), bounds.getNorth()];
  }

  function cacheKey(coordinates) {
    return `${map.getZoom()}|${coordinates.map((value) => value.toFixed(4)).join(",")}`;
  }

  function saveCache(key, payload) {
    if (viewportCache.has(key)) viewportCache.delete(key);
    viewportCache.set(key, payload);
    while (viewportCache.size > maximumCacheEntries) {
      viewportCache.delete(viewportCache.keys().next().value);
    }
  }

  async function applyPayload(payload, currentRender, fromCache = false) {
    const houses = Array.isArray(payload.houses) ? payload.houses : [];
    elements.count.textContent = `Houses: ${formatCount(payload.returned)}`;
    elements.threshold.hidden = !payload.threshold_reached;
    const timing = Number(payload.scan_ms || 0).toFixed(1);
    setStatus(`Rendering ${formatCount(houses.length)} houses…`, "loading");
    await drawHouses(houses, currentRender);
    if (currentRender === renderNumber) {
      setStatus(`${fromCache ? "Cached viewport" : "Backend online"} · scan ${timing} ms`, "online");
    }
  }

  async function refresh(forceNetwork = false) {
    window.clearTimeout(refreshTimer);
    elements.zoom.textContent = `Zoom: ${map.getZoom()}`;
    elements.threshold.hidden = true;

    if (!elements.toggle.checked) {
      if (activeRequest) activeRequest.abort();
      activeRequest = null;
      requestNumber += 1;
      renderNumber += 1;
      housesLayer.clearLayers();
      elements.refresh.disabled = false;
      elements.count.textContent = "Houses: hidden";
      setStatus("House layer is switched off.");
      return;
    }

    if (map.getZoom() < minimumHouseZoom) {
      if (activeRequest) activeRequest.abort();
      activeRequest = null;
      requestNumber += 1;
      renderNumber += 1;
      housesLayer.clearLayers();
      elements.refresh.disabled = false;
      elements.count.textContent = "Houses: —";
      setStatus(`Zoom in to level ${minimumHouseZoom} to load houses.`);
      return;
    }

    const coordinates = viewport();
    const key = cacheKey(coordinates);
    const currentRender = ++renderNumber;
    if (!forceNetwork && viewportCache.has(key)) {
      if (activeRequest) activeRequest.abort();
      activeRequest = null;
      requestNumber += 1;
      elements.refresh.disabled = false;
      await applyPayload(viewportCache.get(key), currentRender, true);
      return;
    }

    if (activeRequest) activeRequest.abort();
    activeRequest = new AbortController();
    const currentRequest = ++requestNumber;
    elements.refresh.disabled = true;
    setStatus("Loading houses…", "loading");

    try {
      const bbox = coordinates.map((coordinate) => coordinate.toFixed(7)).join(",");
      const response = await fetch(`${backendBase}/houses?bbox=${encodeURIComponent(bbox)}`, {
        signal: activeRequest.signal,
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
      if (currentRequest !== requestNumber) return;
      saveCache(key, payload);
      await applyPayload(payload, currentRender);
    } catch (error) {
      if (error.name === "AbortError" || currentRequest !== requestNumber) return;
      renderNumber += 1;
      housesLayer.clearLayers();
      elements.count.textContent = "Houses: 0";
      setStatus(`Backend unavailable: ${error.message}`, "error");
    } finally {
      if (currentRequest === requestNumber) {
        elements.refresh.disabled = false;
        activeRequest = null;
      }
    }
  }

  function scheduleRefresh() {
    window.clearTimeout(refreshTimer);
    refreshTimer = window.setTimeout(() => refresh(false), 180);
  }

  map.on("moveend", scheduleRefresh);
  map.on("zoom", () => { elements.zoom.textContent = `Zoom: ${map.getZoom()}`; });
  elements.toggle.addEventListener("change", () => refresh(false));
  elements.refresh.addEventListener("click", () => refresh(true));

  elements.zoom.textContent = `Zoom: ${map.getZoom()}`;
  refresh(false);
})();
