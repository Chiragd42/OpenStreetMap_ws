(() => {
  "use strict";

  const DEFAULT_BACKEND = "http://127.0.0.1:8080";
  const DEBOUNCE_MS = 180;
  const RENDER_BATCH_SIZE = 700;

  const queryParameters = new URLSearchParams(window.location.search);
  const backendFromQuery = queryParameters.get("backend");
  const backendBase = (backendFromQuery || DEFAULT_BACKEND).replace(/\/$/, "");

  function finiteQueryNumber(name, fallback) {
    const rawValue = queryParameters.get(name);
    if (rawValue === null || rawValue.trim() === "") return fallback;
    const value = Number(rawValue);
    return Number.isFinite(value) ? value : fallback;
  }

  const initialLatitude = finiteQueryNumber("lat", 48.7758);
  const initialLongitude = finiteQueryNumber("lon", 9.1829);
  const initialZoom = Math.min(19, Math.max(3, finiteQueryNumber("zoom", 16)));

  const elements = {
    connectionDot: document.getElementById("connectionDot"),
    connectionText: document.getElementById("connectionText"),
    requestNumber: document.getElementById("requestNumber"),
    returnedCount: document.getElementById("returnedCount"),
    matchedCount: document.getElementById("matchedCount"),
    scanTime: document.getElementById("scanTime"),
    roundTripTime: document.getElementById("roundTripTime"),
    thresholdNotice: document.getElementById("thresholdNotice"),
    emptyNotice: document.getElementById("emptyNotice"),
    errorNotice: document.getElementById("errorNotice"),
    errorText: document.getElementById("errorText"),
    zoomReadout: document.getElementById("zoomReadout"),
    houseToggle: document.getElementById("houseToggle"),
    refreshButton: document.getElementById("refreshButton"),
    backendAddress: document.getElementById("backendAddress"),
  };

  elements.backendAddress.textContent = backendBase.replace(/^https?:\/\//, "");

  const canvasRenderer = L.canvas({ padding: 0.45, tolerance: 5 });
  const map = L.map("map", {
    center: [initialLatitude, initialLongitude],
    zoom: initialZoom,
    zoomControl: false,
    preferCanvas: true,
    renderer: canvasRenderer,
  });

  L.control.zoom({ position: "bottomright" }).addTo(map);
  L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 19,
    attribution: "&copy; OpenStreetMap contributors",
  }).addTo(map);

  const houseLayer = L.layerGroup().addTo(map);
  let activeRequest = null;
  let debounceTimer = null;
  let requestSequence = 0;
  let renderSequence = 0;

  function formatCount(value) {
    return Number(value || 0).toLocaleString("de-DE");
  }

  function setConnection(state, label) {
    elements.connectionDot.dataset.state = state;
    elements.connectionText.textContent = label;
  }

  function setLoading(loading) {
    elements.refreshButton.disabled = loading;
    if (loading) {
      elements.refreshButton.innerHTML = '<span aria-hidden="true">···</span> Querying';
    } else {
      elements.refreshButton.innerHTML = '<span aria-hidden="true">↻</span> Refresh viewport';
    }
  }

  function resetMessages() {
    elements.thresholdNotice.hidden = true;
    elements.emptyNotice.hidden = true;
    elements.errorNotice.hidden = true;
  }

  function clearMetrics() {
    elements.returnedCount.textContent = "—";
    elements.matchedCount.textContent = "—";
    elements.scanTime.textContent = "—";
    elements.roundTripTime.textContent = "—";
  }

  function addressLabel(house) {
    const streetLine = [house.street, house.house_number].filter(Boolean).join(" ");
    const cityLine = [house.postcode, house.city].filter(Boolean).join(" ");
    return [streetLine, cityLine].filter(Boolean).join(", ");
  }

  function popupContent(house) {
    const wrapper = document.createElement("div");
    const id = document.createElement("span");
    id.className = "house-popup__id";
    id.textContent = house.id || "OSM object";

    const title = document.createElement("h2");
    title.className = "house-popup__title";
    title.textContent = house.name || addressLabel(house) || house.building || "Building";

    const details = document.createElement("p");
    details.className = "house-popup__meta";
    const detailParts = [];
    const address = addressLabel(house);
    if (address && address !== title.textContent) detailParts.push(address);
    if (house.building) detailParts.push(`Type: ${house.building}`);
    details.textContent = detailParts.join(" · ") || "No additional metadata";

    wrapper.append(id, title, details);
    return wrapper;
  }

  function markerStyle(house) {
    const addressed = Boolean(house.street || house.house_number || house.postcode || house.city);
    const zoom = map.getZoom();
    return {
      renderer: canvasRenderer,
      radius: zoom >= 18 ? 4 : zoom >= 16 ? 3.2 : 2.4,
      color: "#171916",
      weight: 0.8,
      fillColor: addressed ? "#c73a24" : "#c49a42",
      fillOpacity: addressed ? 0.8 : 0.7,
    };
  }

  async function renderHouses(houses, localRenderSequence) {
    houseLayer.clearLayers();
    for (let begin = 0; begin < houses.length; begin += RENDER_BATCH_SIZE) {
      if (localRenderSequence !== renderSequence || !elements.houseToggle.checked) return;
      const end = Math.min(begin + RENDER_BATCH_SIZE, houses.length);
      for (let index = begin; index < end; index += 1) {
        const house = houses[index];
        if (!Number.isFinite(house.lat) || !Number.isFinite(house.lon)) continue;
        L.circleMarker([house.lat, house.lon], markerStyle(house))
          .bindPopup(() => popupContent(house), { maxWidth: 280 })
          .addTo(houseLayer);
      }
      if (end < houses.length) {
        await new Promise((resolve) => window.requestAnimationFrame(resolve));
      }
    }
  }

  function viewportBbox() {
    const bounds = map.getBounds();
    return [bounds.getWest(), bounds.getSouth(), bounds.getEast(), bounds.getNorth()]
      .map((coordinate) => coordinate.toFixed(7))
      .join(",");
  }

  async function refreshViewport() {
    window.clearTimeout(debounceTimer);
    elements.zoomReadout.textContent = `Z${map.getZoom()}`;

    if (!elements.houseToggle.checked) {
      if (activeRequest) {
        activeRequest.abort();
        activeRequest = null;
      }
      requestSequence += 1;
      renderSequence += 1;
      houseLayer.clearLayers();
      resetMessages();
      clearMetrics();
      setConnection("online", "Houses hidden");
      return;
    }

    if (activeRequest) activeRequest.abort();
    activeRequest = new AbortController();
    const localRequestSequence = ++requestSequence;
    const localRenderSequence = ++renderSequence;

    resetMessages();
    setLoading(true);
    setConnection("loading", "Scanning viewport");
    elements.requestNumber.textContent = `REQ ${String(localRequestSequence).padStart(3, "0")}`;

    const started = performance.now();
    try {
      const url = `${backendBase}/houses?bbox=${encodeURIComponent(viewportBbox())}`;
      const response = await fetch(url, { signal: activeRequest.signal });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
      if (localRequestSequence !== requestSequence) return;

      const houses = Array.isArray(payload.houses) ? payload.houses : [];
      elements.returnedCount.textContent = formatCount(payload.returned);
      elements.matchedCount.textContent = formatCount(payload.matched);
      elements.scanTime.textContent = Number(payload.scan_ms || 0).toFixed(1);
      elements.roundTripTime.textContent = (performance.now() - started).toFixed(0);
      elements.thresholdNotice.hidden = !payload.threshold_reached;
      elements.emptyNotice.hidden = houses.length !== 0;

      await renderHouses(houses, localRenderSequence);
      if (localRequestSequence === requestSequence) {
        setConnection("online", `Backend online · ${formatCount(houses.length)} drawn`);
      }
    } catch (error) {
      if (error.name === "AbortError") return;
      if (localRequestSequence !== requestSequence) return;
      renderSequence += 1;
      houseLayer.clearLayers();
      elements.errorNotice.hidden = false;
      elements.errorText.textContent = error.message || "Start the local server and try again.";
      setConnection("error", "Connection failed");
      clearMetrics();
    } finally {
      if (localRequestSequence === requestSequence) {
        setLoading(false);
        activeRequest = null;
      }
    }
  }

  function scheduleRefresh() {
    window.clearTimeout(debounceTimer);
    debounceTimer = window.setTimeout(refreshViewport, DEBOUNCE_MS);
  }

  map.on("moveend", scheduleRefresh);
  map.on("zoom", () => {
    elements.zoomReadout.textContent = `Z${map.getZoom()}`;
  });
  elements.refreshButton.addEventListener("click", refreshViewport);
  elements.houseToggle.addEventListener("change", refreshViewport);

  elements.zoomReadout.textContent = `Z${map.getZoom()}`;
  refreshViewport();
})();
