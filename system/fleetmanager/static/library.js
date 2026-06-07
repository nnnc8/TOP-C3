const libraryRoot = document.querySelector("[data-route-library]");

if (libraryRoot) {
  const searchInput = libraryRoot.querySelector("[data-route-search]");
  const groupsRoot = libraryRoot.querySelector("[data-route-groups]");
  const state = {
    routes: [],
    query: "",
  };

  searchInput?.addEventListener("input", (event) => {
    state.query = event.target.value.trim().toLowerCase();
    render();
  });

  void loadRoutes();

  async function loadRoutes() {
    const response = await fetch("/api/routes");
    const payload = await response.json();
    state.routes = payload.routes || [];
    render();
  }

  function render() {
    const routes = state.routes.filter((route) => route.routeId.toLowerCase().includes(state.query));
    if (!routes.length) {
      groupsRoot.innerHTML = `
        <article class="empty-state">
          <strong>沒有符合條件的路線。</strong>
          <p>可以試試像 2026-06-07 這種日期片段，或清除搜尋條件。</p>
        </article>
      `;
      return;
    }

    const grouped = new Map();
    for (const route of routes) {
      const day = route.routeId.slice(0, 10);
      const existing = grouped.get(day) || [];
      existing.push(route);
      grouped.set(day, existing);
    }

    groupsRoot.innerHTML = Array.from(grouped.entries()).map(([day, dayRoutes]) => `
      <section class="route-group">
        <div class="route-group-heading">
          <h2>${day}</h2>
          <span class="timeline-caption">${dayRoutes.length} 條路線</span>
        </div>
        <div class="library-routes">
          ${dayRoutes.map(renderCard).join("")}
        </div>
      </section>
    `).join("");
  }

  function renderCard(route) {
    const eventBadges = Object.entries(route.eventCounts)
      .filter(([, count]) => count)
      .map(([name, count]) => `<span class="event-badge">${eventLabel(name)} · ${count}</span>`)
      .join("");
    const routeWindow = formatRouteWindow(route.startedAt, route.endedAt);

    return `
      <a class="route-card" href="/footage/${route.routeId}">
        <div class="route-media">
          <img
            src="${route.previewUrl}"
            alt=""
            loading="lazy"
            decoding="async"
            onerror="this.hidden=true; this.parentElement.classList.add('is-fallback')">
          <div class="route-media-fallback">
            <span class="route-media-chip">${routeWindow}</span>
            <strong>${route.cameras.length} 鏡頭畫面</strong>
          </div>
        </div>
        <div class="route-card-body">
          <div class="route-card-header">
            <strong>${route.routeId}</strong>
            <span>${route.segmentCount} 段</span>
          </div>
          <p>${routeWindow} · ${formatDuration(route.durationSec)} · ${route.cameras.length} 鏡頭</p>
          <div class="badge-row">${eventBadges || '<span class="event-badge">沒有事件標記</span>'}</div>
        </div>
      </a>
    `;
  }

  function formatDuration(seconds) {
    const minutes = Math.floor(seconds / 60);
    const remaining = seconds % 60;
    return `${minutes}分 ${String(remaining).padStart(2, "0")}秒`;
  }

  function formatRouteWindow(startedAt, endedAt) {
    const start = startedAt?.slice(11, 16) || "--:--";
    const end = endedAt?.slice(11, 16) || "--:--";
    return `${start} → ${end}`;
  }

  function eventLabel(name) {
    return {
      hard_brake: "重煞車",
      stop: "停車",
      start: "起步",
      manual_steer: "人工轉向",
    }[name] || name;
  }
}
