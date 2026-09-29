// Iconger website: reveals, small interactive demos, and live data from GitHub.
(() => {
  'use strict';

  const REPO = 'rs4t/iconger';
  const API = 'https://api.github.com/repos/' + REPO;
  const CACHE_KEY = 'iconger-site-gh-v1';
  const CACHE_MS = 15 * 60 * 1000;
  const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)');

  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  // ---------- Nav border once the page scrolls ----------
  const nav = $('.nav');
  const sentinel = $('.nav-sentinel');
  if (nav && sentinel && 'IntersectionObserver' in window) {
    new IntersectionObserver(([e]) => nav.classList.toggle('is-stuck', !e.isIntersecting)).observe(sentinel);
  }

  // ---------- Scroll reveals ----------
  const onReveal = new Map(); // element -> callback run once when it comes into view
  const revealTargets = $$('.reveal, .steps-line');
  if ('IntersectionObserver' in window) {
    const io = new IntersectionObserver((entries) => {
      for (const e of entries) {
        if (!e.isIntersecting) continue;
        e.target.classList.add('is-in');
        io.unobserve(e.target);
        const cb = onReveal.get(e.target);
        if (cb) cb();
      }
    }, { rootMargin: '0px 0px -8% 0px', threshold: 0.12 });
    revealTargets.forEach((el) => io.observe(el));
  } else {
    revealTargets.forEach((el) => el.classList.add('is-in'));
  }

  // ---------- Toggle groups (aria-pressed chips) ----------
  function pressGroup(buttons, pressed) {
    buttons.forEach((b) => b.setAttribute('aria-pressed', String(b === pressed)));
  }

  // Adjust demo: one-click looks on the Firefox icon
  const adjustImg = $('.adjust-img');
  const lookButtons = $$('[data-look]');
  lookButtons.forEach((b) => b.addEventListener('click', () => {
    pressGroup(lookButtons, b);
    adjustImg.dataset.look = b.dataset.look;
  }));

  // Brand tile demo: colour and shape
  const brandTile = $('.brand-tile');
  const swatches = $$('[data-tile]');
  const shapes = $$('.cell-brand [data-shape]').filter((el) => el.tagName === 'BUTTON');
  swatches.forEach((b) => b.addEventListener('click', () => {
    pressGroup(swatches, b);
    brandTile.style.setProperty('--tile', b.dataset.tile);
  }));
  shapes.forEach((b) => b.addEventListener('click', () => {
    pressGroup(shapes, b);
    brandTile.dataset.shape = b.dataset.shape;
  }));

  // ---------- One app, seven styles ----------
  // Simple Icons are one-colour logos: Iconger puts them on a tile in the brand's colour.
  const BRAND = { firefox: '#FF7139', discord: '#5865F2', steam: '#000000', obsidian: '#7C3AED' };
  const APPS = Object.keys(BRAND);
  const APP_NAMES = { firefox: 'Firefox', discord: 'Discord', steam: 'Steam', obsidian: 'Obsidian' };
  const libs = $$('.lib');
  libs.forEach((li, index) => {
    const slot = $('.lib-icon', li);
    const lib = li.dataset.lib;
    const libName = $('a', li).textContent;
    slot.setAttribute('role', 'img');
    APPS.forEach((app, n) => {
      const src = 'img/libraries/' + app + '-' + lib + '.svg';
      let el;
      if (lib === 'simple') {
        el = document.createElement('span');
        el.className = 'tile';
        el.style.setProperty('--tile', BRAND[app]);
        const glyph = document.createElement('span');
        glyph.className = 'glyph';
        glyph.style.setProperty('--glyph', 'url(' + src + ')');
        el.appendChild(glyph);
      } else {
        el = document.createElement('img');
        el.src = src;
        el.alt = '';
        el.width = 96;
        el.height = 96;
        el.decoding = 'async';
        el.loading = 'lazy';
      }
      el.dataset.app = app;
      el.style.setProperty('--i', index);
      if (n === 0) el.classList.add('is-on');
      slot.appendChild(el);
    });
    slot.setAttribute('aria-label', APP_NAMES[APPS[0]] + ' icon from ' + libName);
  });
  const appButtons = $$('[data-app]').filter((el) => el.tagName === 'BUTTON');
  appButtons.forEach((b) => b.addEventListener('click', () => {
    pressGroup(appButtons, b);
    const app = b.dataset.app;
    libs.forEach((li) => {
      $$('.lib-icon > *', li).forEach((el) => el.classList.toggle('is-on', el.dataset.app === app));
      $('.lib-icon', li).setAttribute('aria-label', APP_NAMES[app] + ' icon from ' + $('a', li).textContent);
    });
  }));

  // ---------- Screenshot tabs ----------
  const tabs = $$('[role="tab"]');
  const shots = $$('.shots > .shot');
  const panel = $('#shot-panel');
  const caption = $('.shot-caption');
  function selectTab(tab, focus) {
    tabs.forEach((t) => {
      const on = t === tab;
      t.setAttribute('aria-selected', String(on));
      t.tabIndex = on ? 0 : -1;
    });
    const n = Number(tab.dataset.shot);
    shots.forEach((s, i) => s.classList.toggle('is-on', i === n));
    panel.setAttribute('aria-labelledby', tab.id);
    caption.textContent = shots[n].dataset.caption || '';
    if (focus) tab.focus();
  }
  tabs.forEach((t, i) => {
    t.addEventListener('click', () => selectTab(t, false));
    t.addEventListener('keydown', (e) => {
      let next = null;
      if (e.key === 'ArrowRight') next = tabs[(i + 1) % tabs.length];
      else if (e.key === 'ArrowLeft') next = tabs[(i - 1 + tabs.length) % tabs.length];
      else if (e.key === 'Home') next = tabs[0];
      else if (e.key === 'End') next = tabs[tabs.length - 1];
      if (next) { e.preventDefault(); selectTab(next, true); }
    });
  });

  // ---------- Live data from GitHub ----------
  const fmtInt = new Intl.NumberFormat('en-US');
  const fmtDate = new Intl.DateTimeFormat('en-US', { month: 'short', day: 'numeric', year: 'numeric' });

  function readCache() {
    try {
      const raw = localStorage.getItem(CACHE_KEY);
      if (!raw) return null;
      const c = JSON.parse(raw);
      return Date.now() - c.t < CACHE_MS ? c : null;
    } catch { return null; }
  }
  function writeCache(data) {
    try { localStorage.setItem(CACHE_KEY, JSON.stringify(Object.assign({ t: Date.now() }, data))); } catch { /* private mode */ }
  }

  async function getJson(url) {
    const r = await fetch(url, { headers: { Accept: 'application/vnd.github+json' } });
    if (!r.ok) throw new Error(url + ': ' + r.status);
    return r.json();
  }

  async function loadGitHub() {
    const cached = readCache();
    if (cached) return cached;
    const [releases, repo] = await Promise.all([
      getJson(API + '/releases?per_page=100'),
      getJson(API).catch(() => null),
    ]);
    const data = {
      stars: repo ? repo.stargazers_count : null,
      releases: releases
        .filter((r) => !r.draft)
        .map((r) => {
          const exe = (r.assets || []).find((a) => a.name.toLowerCase() === 'iconger.exe');
          return {
            tag: r.tag_name,
            date: r.published_at,
            pre: r.prerelease,
            url: r.html_url,
            body: r.body || '',
            size: exe ? exe.size : 0,
            downloads: exe ? exe.download_count : 0,
          };
        }),
    };
    writeCache(data);
    return data;
  }

  // Count a number up once, when it scrolls into view.
  function countUp(el, value) {
    const text = (v) => fmtInt.format(Math.round(v));
    const holder = el.closest('.reveal');
    const run = () => {
      if (reduceMotion.matches || value < 10) { el.textContent = text(value); return; }
      const start = performance.now();
      const dur = 900;
      const step = (now) => {
        const p = Math.min(1, (now - start) / dur);
        el.textContent = text(value * (1 - Math.pow(1 - p, 3)));
        if (p < 1) requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
    };
    if (!holder || reduceMotion.matches || !('IntersectionObserver' in window) || holder.classList.contains('is-in')) run();
    else onReveal.set(holder, run);
  }

  function showStats(data) {
    const downloads = data.releases.reduce((sum, r) => sum + r.downloads, 0);
    const dl = $('[data-stat="downloads"]');
    const st = $('[data-stat="stars-num"]');
    countUp(dl, downloads);
    if (data.stars != null) {
      countUp(st, data.stars);
      $('[data-stat="stars"]').textContent = fmtInt.format(data.stars);
    } else {
      st.closest('.stat').remove();
    }
    const latest = data.releases.find((r) => !r.pre);
    if (latest) {
      const mb = latest.size ? (latest.size / 1048576).toFixed(1) + '\u00a0MB' : '';
      const parts = [latest.tag, mb, 'released ' + fmtDate.format(new Date(latest.date))].filter(Boolean);
      $('[data-release-meta]').textContent = parts.join(', ');
      if (mb) $('[data-release-size]').textContent = mb + ', one file';
    }
  }

  function hideLiveStats() {
    $$('[data-stat="downloads"], [data-stat="stars-num"]').forEach((el) => el.closest('.stat').remove());
  }

  // ---------- Release notes (a small, safe subset of Markdown) ----------
  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  }
  function inline(s) {
    return escapeHtml(s)
      .replace(/`([^`]+)`/g, '<code>$1</code>')
      .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
      .replace(/\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g, '<a href="$2">$1</a>');
  }
  // The notes are written for the release page; drop the line about the download there.
  function noteLines(body) {
    return body.replace(/\r/g, '').split('\n').filter((l) => !/Download `?iconger\.exe`? below/i.test(l));
  }
  function renderNotes(body) {
    let html = '';
    let para = [];
    let list = false;
    const flush = () => {
      if (para.length) { html += '<p>' + inline(para.join(' ')) + '</p>'; para = []; }
    };
    const closeList = () => { if (list) { html += '</ul>'; list = false; } };
    for (const raw of noteLines(body)) {
      const line = raw.trim();
      let m;
      if (!line) { flush(); closeList(); }
      else if ((m = line.match(/^#{1,6}\s+(.*)$/))) { flush(); closeList(); html += '<h3>' + inline(m[1]) + '</h3>'; }
      else if ((m = line.match(/^[-*]\s+(.*)$/))) { flush(); if (!list) { html += '<ul>'; list = true; } html += '<li>' + inline(m[1]) + '</li>'; }
      else { closeList(); para.push(line); }
    }
    flush(); closeList();
    return html;
  }
  function summaryOf(body) {
    const first = noteLines(body).map((l) => l.trim()).find((l) => l && !l.startsWith('#'));
    return first ? first.replace(/^[-*]\s+/, '').replace(/\*\*|`/g, '').replace(/\[([^\]]+)\]\([^)]*\)/g, '$1') : '';
  }

  function showReleases(data) {
    const box = $('[data-releases]');
    box.innerHTML = '';
    const latestTag = (data.releases.find((r) => !r.pre) || {}).tag;
    const SHOWN = 5;
    data.releases.forEach((r, i) => {
      const d = document.createElement('details');
      d.className = 'release' + (i > 0 ? ' reveal' : '');
      if (i >= SHOWN) d.hidden = true;
      if (i === 0) d.open = true;
      const date = r.date ? '<time class="release-date" datetime="' + escapeHtml(r.date) + '">' + fmtDate.format(new Date(r.date)) + '</time>' : '';
      d.innerHTML =
        '<summary><span class="release-version" translate="no">' + escapeHtml(r.tag) + '</span>' + date +
        (r.tag === latestTag ? '<span class="release-latest">Latest</span>' : '') +
        '<span class="release-summary">' + inline(summaryOf(r.body)) + '</span></summary>' +
        '<div class="release-body">' + renderNotes(r.body) + '</div>';
      box.appendChild(d);
    });
    box.setAttribute('aria-busy', 'false');
    const hiddenCount = data.releases.length - SHOWN;
    const more = $('[data-releases-more]');
    if (hiddenCount > 0) {
      more.textContent = 'Show ' + hiddenCount + ' older version' + (hiddenCount === 1 ? '' : 's');
      more.hidden = false;
      more.addEventListener('click', () => {
        const rows = $$('.release[hidden]', box);
        rows.forEach((el, i) => {
          el.classList.remove('is-in');
          el.style.setProperty('--i', Math.min(i, 8));
          el.hidden = false;
        });
        requestAnimationFrame(() => requestAnimationFrame(() => rows.forEach((el) => el.classList.add('is-in'))));
        more.hidden = true;
        if (rows[0]) $('summary', rows[0]).focus({ preventScroll: true });
      }, { once: true });
    }
    // older rows fade in as a group, the list is already in view
    requestAnimationFrame(() => $$('.release.reveal:not([hidden])', box).forEach((el, i) => {
      el.style.setProperty('--i', Math.min(i, 6));
      el.classList.add('is-in');
    }));
  }

  function showReleasesError() {
    const box = $('[data-releases]');
    box.innerHTML = '';
    box.setAttribute('aria-busy', 'false');
    $('[data-releases-error]').hidden = false;
  }

  loadGitHub()
    .then((data) => {
      if (!data.releases.length) throw new Error('no releases');
      showStats(data);
      showReleases(data);
    })
    .catch(() => {
      hideLiveStats();
      showReleasesError();
    });
})();
