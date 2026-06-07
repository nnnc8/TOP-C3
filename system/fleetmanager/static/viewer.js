const viewerRoot = document.querySelector("[data-route-viewer]");

if (viewerRoot) {
  const routeId = viewerRoot.dataset.routeId;
  const state = {
    manifest: null,
    focusedCamera: null,
    downloadTarget: "merged",
    currentSegmentIndex: 0,
    currentTime: 0,
    isPlaying: false,
    mobileMode: false,
    syncLoopId: null,
  };
  const refs = {};

  window.addEventListener("resize", handleViewportChange);
  void loadManifest();

  async function loadManifest() {
    const response = await fetch(`/api/routes/${routeId}`);
    if (!response.ok) {
      viewerRoot.innerHTML = `
        <article class="empty-state">
          <strong>無法載入這條路線。</strong>
          <p>路線資料端點回傳了 ${response.status}。</p>
        </article>
      `;
      return;
    }

    state.manifest = await response.json();
    state.focusedCamera = pickFocusedCamera(state.manifest.cameras);
    updateLayoutMode();
    renderViewer();
    loadSegment(0, 0, false);
    startSyncLoop();
  }

  function renderViewer() {
    viewerRoot.innerHTML = `
      <div class="viewer-panel">
        <div class="section-row">
          <div>
            <div class="viewer-kicker">目前選取鏡頭使用高畫質</div>
            <strong>${state.manifest.routeId}</strong>
            <div class="player-meta">${formatDuration(state.manifest.durationSec)} · ${state.manifest.cameras.length} 鏡頭 · ${state.mobileMode ? "手機版使用單鏡頭輕量播放" : "全鏡頭保持同步"}</div>
          </div>
          <div class="player-actions">
            <button class="player-button is-primary" type="button" data-play-toggle>播放</button>
            <button class="player-button" type="button" data-jump="-10">-10秒</button>
            <button class="player-button" type="button" data-jump="10">+10秒</button>
          </div>
        </div>
        ${state.mobileMode ? '<div class="camera-switch-row" data-camera-switches></div>' : ''}
        <div class="viewer-grid" data-camera-grid></div>
        <div class="event-lane">
          <div class="badge-row" data-event-chips></div>
        </div>
        <div class="timeline-block">
          <div class="timeline-track">
            <div class="segment-ticks" data-segment-ticks></div>
            <div class="event-markers" data-event-markers></div>
            <input class="timeline-slider" type="range" min="0" max="${state.manifest.durationSec}" value="0" step="0.25" data-timeline>
          </div>
          <div class="section-row">
            <div class="timeline-caption" data-time-readout>00:00 / ${formatClock(state.manifest.durationSec)}</div>
          </div>
        </div>
        <div class="download-panel">
          <div class="section-row">
            <div>
              <div class="viewer-kicker">下載選項</div>
              <strong>可下載目前這一分鐘，也可下載整條路線。</strong>
              <div class="player-meta">可選單一鏡頭，或把四鏡頭合成成一支影片下載。</div>
            </div>
          </div>
          <div class="download-targets" data-download-targets></div>
          <div class="download-row">
            <a class="download-link" data-minute-download href="#">下載目前這一分鐘</a>
            <a class="download-link is-primary" data-full-download href="#">下載整個路線</a>
          </div>
        </div>
      </div>
    `;

    refs.grid = viewerRoot.querySelector("[data-camera-grid]");
    refs.eventChips = viewerRoot.querySelector("[data-event-chips]");
    refs.eventMarkers = viewerRoot.querySelector("[data-event-markers]");
    refs.segmentTicks = viewerRoot.querySelector("[data-segment-ticks]");
    refs.timeline = viewerRoot.querySelector("[data-timeline]");
    refs.timeReadout = viewerRoot.querySelector("[data-time-readout]");
    refs.playToggle = viewerRoot.querySelector("[data-play-toggle]");
    refs.downloadTargets = viewerRoot.querySelector("[data-download-targets]");
    refs.minuteDownload = viewerRoot.querySelector("[data-minute-download]");
    refs.fullDownload = viewerRoot.querySelector("[data-full-download]");
    refs.cameraSwitches = viewerRoot.querySelector("[data-camera-switches]");

    refs.playToggle.addEventListener("click", togglePlayback);
    viewerRoot.querySelectorAll("[data-jump]").forEach((button) => {
      button.addEventListener("click", () => seekTo(state.currentTime + Number(button.dataset.jump)));
    });
    refs.timeline.addEventListener("input", () => {
      refs.timeReadout.textContent = `${formatClock(Number(refs.timeline.value))} / ${formatClock(state.manifest.durationSec)}`;
    });
    refs.timeline.addEventListener("change", () => seekTo(Number(refs.timeline.value)));
    renderEventControls();
    renderDownloadTargets();
  }

  function renderTiles() {
    const segment = state.manifest.segments[state.currentSegmentIndex];
    if (!segment.cameras.includes(state.focusedCamera)) {
      state.focusedCamera = pickFocusedCamera(segment.cameras);
    }

    if (state.mobileMode) {
      renderMobileViewer(segment);
      return;
    }

    refs.grid.innerHTML = state.manifest.cameras.map((camera) => {
      const available = segment.cameras.includes(camera);
      const quality = camera === state.focusedCamera ? "focus" : "sync";
      const classes = [
        "camera-tile",
        camera === state.focusedCamera ? "is-focused" : "",
        !available ? "is-unavailable" : "",
      ].join(" ");
      return `
        <article class="${classes}" data-camera-tile data-camera="${camera}">
          <div class="camera-meta">
            <span class="camera-tag">${cameraLabel(camera)}</span>
            <span class="quality-tag">${available ? qualityLabel(quality) : "離線"}</span>
          </div>
          ${available
            ? `<video data-camera-video="${camera}" muted playsinline webkit-playsinline preload="metadata"></video>`
            : `<div class="camera-empty">這一段沒有這個鏡頭</div>`}
        </article>
      `;
    }).join("");

    refs.videoMap = new Map();
    refs.grid.querySelectorAll("[data-camera-tile]").forEach((tile) => {
      const camera = tile.dataset.camera;
      tile.addEventListener("click", () => {
        if (camera !== state.focusedCamera) {
          const offset = getCurrentSegmentOffset();
          const wasPlaying = state.isPlaying;
          state.focusedCamera = camera;
          loadSegment(state.currentSegmentIndex, offset, wasPlaying);
        }
      });
      const video = tile.querySelector("video");
      if (!video) {
        return;
      }
      bindVideoLifecycle(camera, video);
      refs.videoMap.set(camera, video);
    });
  }

  function renderMobileViewer(segment) {
    refs.grid.innerHTML = `
      <article class="camera-tile is-focused is-mobile-player" data-camera-tile data-camera="${state.focusedCamera}">
        <div class="camera-meta">
          <span class="camera-tag">${cameraLabel(state.focusedCamera)}</span>
          <span class="quality-tag">${segment.cameras.includes(state.focusedCamera) ? "主畫面" : "離線"}</span>
        </div>
        ${segment.cameras.includes(state.focusedCamera)
          ? `<video data-camera-video="${state.focusedCamera}" muted playsinline webkit-playsinline preload="metadata" controls disablepictureinpicture controlslist="nodownload noremoteplayback"></video>`
          : `<div class="camera-empty">這一段沒有這個鏡頭</div>`}
      </article>
    `;

    refs.videoMap = new Map();
    const video = refs.grid.querySelector("video");
    if (video) {
      bindVideoLifecycle(state.focusedCamera, video);
      refs.videoMap.set(state.focusedCamera, video);
    }

    if (!refs.cameraSwitches) {
      return;
    }

    refs.cameraSwitches.innerHTML = state.manifest.cameras.map((camera) => {
      const active = camera === state.focusedCamera;
      const available = segment.cameras.includes(camera);
      return `
        <button
          class="camera-switch ${active ? "is-active" : ""}"
          type="button"
          data-camera-switch="${camera}"
          ${available ? "" : "disabled"}>
          ${cameraLabel(camera)}
        </button>
      `;
    }).join("");

    refs.cameraSwitches.querySelectorAll("[data-camera-switch]").forEach((button) => {
      button.addEventListener("click", () => {
        if (button.dataset.cameraSwitch === state.focusedCamera) {
          return;
        }
        const offset = getCurrentSegmentOffset();
        const wasPlaying = state.isPlaying;
        state.focusedCamera = button.dataset.cameraSwitch;
        loadSegment(state.currentSegmentIndex, offset, wasPlaying);
      });
    });
  }

  function bindVideoLifecycle(camera, video) {
    video.addEventListener("ended", () => {
      if (camera === state.focusedCamera) {
        advanceSegment();
      }
    });
  }

  function renderEventControls() {
    refs.segmentTicks.innerHTML = state.manifest.segments.map((segment) => `
      <span class="segment-tick" style="left:${(segment.startSec / state.manifest.durationSec) * 100}%"></span>
    `).join("");

    refs.eventMarkers.innerHTML = state.manifest.events.map((event) => `
      <button
        class="event-marker"
        type="button"
        data-severity="${event.severity}"
        data-target="${event.startSec}"
        style="left:${(event.startSec / state.manifest.durationSec) * 100}%"
        title="${event.label}">
      </button>
    `).join("");

    refs.eventChips.innerHTML = state.manifest.events.length
      ? state.manifest.events.map((event) => `
          <button class="event-chip" type="button" data-target="${event.startSec}">
            ${event.label} · ${formatClock(event.startSec)}
          </button>
        `).join("")
      : '<span class="timeline-caption">這條路線沒有可用的事件標記。</span>';

    viewerRoot.querySelectorAll("[data-target]").forEach((element) => {
      element.addEventListener("click", () => seekTo(Number(element.dataset.target)));
    });
  }

  function renderDownloadTargets() {
    const targets = [
      {id: "merged", label: "四鏡頭合成"},
      ...state.manifest.cameras.map((camera) => ({id: camera, label: cameraLabel(camera)})),
    ];

    refs.downloadTargets.innerHTML = targets.map((target) => `
      <button
        class="download-target ${target.id === state.downloadTarget ? "is-active" : ""}"
        type="button"
        data-download-target="${target.id}">
        ${target.label}
      </button>
    `).join("");

    refs.downloadTargets.querySelectorAll("[data-download-target]").forEach((button) => {
      button.addEventListener("click", () => {
        state.downloadTarget = button.dataset.downloadTarget;
        renderDownloadTargets();
        updateDownloadLinks();
      });
    });
  }

  function updateLayoutMode() {
    state.mobileMode = isMobileViewport();
    viewerRoot.dataset.layout = window.matchMedia("(orientation: landscape)").matches ? "landscape" : "portrait";
    viewerRoot.dataset.viewport = state.mobileMode ? "mobile" : "desktop";
  }

  function handleViewportChange() {
    const previousMobileMode = state.mobileMode;
    updateLayoutMode();
    if (!state.manifest || previousMobileMode === state.mobileMode) {
      return;
    }
    const offset = state.currentTime - state.manifest.segments[state.currentSegmentIndex].startSec;
    renderViewer();
    loadSegment(state.currentSegmentIndex, Math.max(0, offset), state.isPlaying);
  }

  function isMobileViewport() {
    return window.matchMedia("(max-width: 900px)").matches;
  }

  function pickFocusedCamera(cameras) {
    for (const preferred of ["qcamera", "fcamera", "ecamera", "dcamera"]) {
      if (cameras.includes(preferred)) {
        return preferred;
      }
    }
    return cameras[0];
  }

  function loadSegment(segmentIndex, offset, autoplay) {
    state.currentSegmentIndex = segmentIndex;
    state.currentTime = state.manifest.segments[segmentIndex].startSec + offset;
    state.isPlaying = autoplay;
    refs.timeline.value = String(state.currentTime);
    refs.timeReadout.textContent = `${formatClock(state.currentTime)} / ${formatClock(state.manifest.durationSec)}`;
    renderTiles();

    const segment = state.manifest.segments[segmentIndex];
    for (const camera of state.manifest.cameras) {
      const video = refs.videoMap.get(camera);
      if (!video) {
        continue;
      }
      const quality = state.mobileMode ? "preview" : (camera === state.focusedCamera ? "full" : "preview");
      video.src = `/api/segments/${segment.segmentId}/stream/${camera}?quality=${quality}`;
      video.currentTime = 0;
      video.muted = true;
      video.addEventListener("loadedmetadata", () => {
        video.currentTime = offset;
        if (autoplay) {
          void video.play().catch(() => null);
        }
      }, {once: true});
    }

    updatePlaybackButton();
    updateDownloadLinks();
  }

  function togglePlayback() {
    if (state.isPlaying) {
      state.isPlaying = false;
      refs.videoMap?.forEach((video) => video.pause());
    } else {
      state.isPlaying = true;
      refs.videoMap?.forEach((video) => {
        void video.play().catch(() => null);
      });
    }
    updatePlaybackButton();
  }

  function updatePlaybackButton() {
    refs.playToggle.textContent = state.isPlaying ? "暫停" : "播放";
  }

  function startSyncLoop() {
    if (state.syncLoopId !== null) {
      window.clearInterval(state.syncLoopId);
    }
    state.syncLoopId = window.setInterval(() => {
      if (!refs.videoMap) {
        return;
      }
      const master = refs.videoMap.get(state.focusedCamera);
      if (!master || master.readyState < 2) {
        return;
      }

      const segment = state.manifest.segments[state.currentSegmentIndex];
      state.currentTime = segment.startSec + master.currentTime;
      refs.timeline.value = String(state.currentTime);
      refs.timeReadout.textContent = `${formatClock(state.currentTime)} / ${formatClock(state.manifest.durationSec)}`;
      updateDownloadLinks();

      refs.videoMap.forEach((video, camera) => {
        if (camera === state.focusedCamera || video.readyState < 2) {
          return;
        }
        if (Math.abs(video.currentTime - master.currentTime) > 0.35) {
          video.currentTime = master.currentTime;
        }
        if (state.isPlaying && video.paused) {
          void video.play().catch(() => null);
        }
      });
    }, 220);
  }

  function advanceSegment() {
    if (state.currentSegmentIndex >= state.manifest.segments.length - 1) {
      state.isPlaying = false;
      updatePlaybackButton();
      return;
    }
    loadSegment(state.currentSegmentIndex + 1, 0, state.isPlaying);
  }

  function seekTo(targetSeconds) {
    const bounded = Math.max(0, Math.min(targetSeconds, state.manifest.durationSec));
    let safeIndex = 0;
    for (let index = 0; index < state.manifest.segments.length; index += 1) {
      if (state.manifest.segments[index].startSec <= bounded) {
        safeIndex = index;
      }
    }
    const offset = bounded - state.manifest.segments[safeIndex].startSec;
    loadSegment(safeIndex, offset, state.isPlaying);
  }

  function updateDownloadLinks() {
    const segment = state.manifest.segments[state.currentSegmentIndex];
    refs.minuteDownload.textContent = `下載第 ${String(segment.index + 1).padStart(2, "0")} 分鐘`;
    refs.minuteDownload.href = `/api/segments/${segment.segmentId}/download/${state.downloadTarget}`;
    refs.fullDownload.href = state.manifest.downloads.full[state.downloadTarget];

    const segmentHasTarget = state.downloadTarget === "merged" || segment.cameras.includes(state.downloadTarget);
    refs.minuteDownload.classList.toggle("is-disabled", !segmentHasTarget);
    refs.minuteDownload.setAttribute("aria-disabled", String(!segmentHasTarget));
    if (!segmentHasTarget) {
      refs.minuteDownload.removeAttribute("href");
    }
  }

  function getCurrentSegmentOffset() {
    const master = refs.videoMap?.get(state.focusedCamera);
    if (master && master.readyState >= 2) {
      return master.currentTime;
    }
    return state.currentTime - state.manifest.segments[state.currentSegmentIndex].startSec;
  }

  function cameraLabel(camera) {
    return {
      qcamera: "前方廣角",
      fcamera: "前方主鏡",
      ecamera: "廣角",
      dcamera: "車內",
    }[camera] || camera;
  }

  function qualityLabel(quality) {
    return {
      focus: "主畫面",
      sync: "同步",
    }[quality] || quality;
  }

  function formatDuration(seconds) {
    const minutes = Math.floor(seconds / 60);
    const remainder = seconds % 60;
    return `${minutes}分 ${String(remainder).padStart(2, "0")}秒`;
  }

  function formatClock(seconds) {
    const whole = Math.floor(seconds);
    const minutes = Math.floor(whole / 60);
    const remainder = whole % 60;
    return `${String(minutes).padStart(2, "0")}:${String(remainder).padStart(2, "0")}`;
  }
}
