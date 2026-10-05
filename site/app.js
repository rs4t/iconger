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

  // ---------- Headings: wrap each word so they can rise one after another ----------
  $$('section h2.reveal').forEach((h) => {
    const words = h.textContent.trim().split(/\s+/);
    h.textContent = '';
    words.forEach((word, i) => {
      const w = document.createElement('span');
      w.className = 'w';
      w.style.setProperty('--w', i);
      w.textContent = word;
      h.append(w, i < words.length - 1 ? ' ' : '');
    });
    h.classList.add('split');
  });

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

  // Demos play by themselves while in view, until the visitor picks something.
  // Hovering or focusing a demo pauses it; reduced motion turns autoplay off.
  function autoplay(root, ms, step) {
    if (reduceMotion.matches || !('IntersectionObserver' in window)) return { stop() {} };
    let visible = false, held = false, stopped = false, timer = 0;
    const schedule = () => {
      clearTimeout(timer);
      if (!stopped && visible && !held) timer = setTimeout(() => { step(); schedule(); }, ms);
    };
    new IntersectionObserver(([e]) => { visible = e.isIntersecting; schedule(); }, { threshold: 0.5 }).observe(root);
    root.addEventListener('pointerenter', () => { held = true; schedule(); });
    root.addEventListener('pointerleave', () => { held = false; schedule(); });
    root.addEventListener('focusin', () => { held = true; schedule(); });
    root.addEventListener('focusout', () => { held = false; schedule(); });
    return { stop() { stopped = true; clearTimeout(timer); } };
  }

  // Adjust demo: one-click looks on the Firefox icon
  const adjustImg = $('.adjust-img');
  const lookButtons = $$('[data-look]');
  const applyLook = (b) => { pressGroup(lookButtons, b); adjustImg.dataset.look = b.dataset.look; };
  const lookAuto = autoplay($('.cell-adjust'), 1800, () => {
    const i = lookButtons.findIndex((b) => b.getAttribute('aria-pressed') === 'true');
    applyLook(lookButtons[(i + 1) % lookButtons.length]);
  });
  lookButtons.forEach((b) => b.addEventListener('click', () => { lookAuto.stop(); applyLook(b); }));

  // Brand tile demo: colour and shape
  const brandTile = $('.brand-tile');
  const swatches = $$('[data-tile]');
  const shapes = $$('.cell-brand [data-shape]').filter((el) => el.tagName === 'BUTTON');
  const applyTile = (b) => { pressGroup(swatches, b); brandTile.style.setProperty('--tile', b.dataset.tile); };
  const applyShape = (b) => { pressGroup(shapes, b); brandTile.dataset.shape = b.dataset.shape; };
  let brandStep = 0;
  const brandAuto = autoplay($('.cell-brand'), 1700, () => {
    brandStep++;
    applyTile(swatches[brandStep % swatches.length]);
    applyShape(shapes[brandStep % shapes.length]);
  });
  swatches.forEach((b) => b.addEventListener('click', () => { brandAuto.stop(); applyTile(b); }));
  shapes.forEach((b) => b.addEventListener('click', () => { brandAuto.stop(); applyShape(b); }));

  // Soft light that follows the pointer over cards
  if (window.matchMedia('(hover: hover)').matches) {
    $$('.cell, .lib').forEach((card) => {
      const target = card.classList.contains('lib') ? $('.lib-icon', card) : card;
      card.addEventListener('pointermove', (e) => {
        const r = target.getBoundingClientRect();
        target.style.setProperty('--mx', (e.clientX - r.left) + 'px');
        target.style.setProperty('--my', (e.clientY - r.top) + 'px');
      });
    });
  }

  // ---------- One app, seven styles ----------
  // Simple Icons are one-colour logos: Iconger puts them on a tile in the brand's colour.
  const BRAND = { firefox: '#FF7139', discord: '#5865F2', steam: '#000000', obsidian: '#7C3AED' };
  const APPS = Object.keys(BRAND);
  const APP_NAMES = { firefox: 'Firefox', discord: 'Discord', steam: 'Steam', obsidian: 'Obsidian' };
  const libs = $$('.lib');
  libs.forEach((li, index) => {
    const slot = $('.lib-icon', li);
    const lib = li.dataset.lib;
    APPS.forEach((app, n) => {
      if (slot.querySelector('[data-app="' + app + '"]')) return; // already in the HTML
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
      slot.appendChild(el);
    });
  });
  const appButtons = $$('[data-app]').filter((el) => el.tagName === 'BUTTON');
  const showApp = (b) => {
    pressGroup(appButtons, b);
    const app = b.dataset.app;
    libs.forEach((li) => {
      $$('.lib-icon > *', li).forEach((el) => el.classList.toggle('is-on', el.dataset.app === app));
      $('.lib-icon', li).setAttribute('aria-label', APP_NAMES[app] + ' icon from ' + $('a', li).textContent);
    });
  };
  const appAuto = autoplay($('.libs'), 2600, () => {
    const i = appButtons.findIndex((b) => b.getAttribute('aria-pressed') === 'true');
    showApp(appButtons[(i + 1) % appButtons.length]);
  });
  appButtons.forEach((b) => b.addEventListener('click', () => { appAuto.stop(); showApp(b); }));

  // ---------- Screenshot tabs ----------
  // While the section is in view the tabs advance by themselves: the active tab's
  // progress bar fills, and when it ends the next tab opens. Hover pauses, a click stops it.
  const tabList = $('.tabs');
  const tabs = $$('[role="tab"]');
  const shots = $$('.shots > .shot');
  const panel = $('#shot-panel');
  let tabsStopped = reduceMotion.matches;
  function selectTab(tab, focus) {
    tabs.forEach((t) => {
      const on = t === tab;
      t.setAttribute('aria-selected', String(on));
      t.tabIndex = on ? 0 : -1;
    });
    const n = Number(tab.dataset.shot);
    shots.forEach((s, i) => {
      const was = s.classList.contains('is-on') && i !== n;
      s.classList.toggle('is-on', i === n);
      s.classList.toggle('was-on', was);
    });
    panel.setAttribute('aria-labelledby', tab.id);
    if (focus) tab.focus();
  }
  function stopTabs() {
    tabsStopped = true;
    tabList.classList.remove('is-playing', 'is-paused');
  }
  tabs.forEach((t, i) => {
    t.addEventListener('click', () => { stopTabs(); selectTab(t, false); });
    t.addEventListener('keydown', (e) => {
      let next = null;
      if (e.key === 'ArrowDown' || e.key === 'ArrowRight') next = tabs[(i + 1) % tabs.length];
      else if (e.key === 'ArrowUp' || e.key === 'ArrowLeft') next = tabs[(i - 1 + tabs.length) % tabs.length];
      else if (e.key === 'Home') next = tabs[0];
      else if (e.key === 'End') next = tabs[tabs.length - 1];
      if (next) { e.preventDefault(); stopTabs(); selectTab(next, true); }
    });
  });
  tabList.addEventListener('animationend', (e) => {
    if (tabsStopped || !e.target.classList.contains('tab-progress')) return;
    const i = tabs.findIndex((t) => t.getAttribute('aria-selected') === 'true');
    selectTab(tabs[(i + 1) % tabs.length], false);
  });
  if (!tabsStopped && 'IntersectionObserver' in window) {
    const stage = $('.shots-layout');
    new IntersectionObserver(([e]) => {
      if (!tabsStopped) tabList.classList.toggle('is-playing', e.isIntersecting);
    }, { threshold: 0.45 }).observe(stage);
    const hold = (on) => { if (!tabsStopped) tabList.classList.toggle('is-paused', on); };
    stage.addEventListener('pointerenter', () => hold(true));
    stage.addEventListener('pointerleave', () => hold(false));
  }

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

  // ---------- Older versions ----------
  // The whole changelog is plain HTML (built by scripts/render_site.py); versions after the
  // newest few start hidden behind this button.
  function wireOlderReleases() {
    const box = $('[data-releases]');
    const more = $('[data-releases-more]');
    const older = $$('.release[hidden]', box);
    if (!older.length) return;
    more.textContent = 'Show ' + older.length + ' older version' + (older.length === 1 ? '' : 's');
    more.hidden = false;
    more.addEventListener('click', () => {
      older.forEach((el, i) => {
        el.classList.remove('is-in');
        el.style.setProperty('--i', Math.min(i, 8));
        el.hidden = false;
      });
      requestAnimationFrame(() => requestAnimationFrame(() => older.forEach((el) => el.classList.add('is-in'))));
      more.hidden = true;
      $('summary', older[0]).focus({ preventScroll: true });
    }, { once: true });
  }
  wireOlderReleases();

  loadGitHub()
    .then((data) => {
      if (!data.releases.length) throw new Error('no releases');
      showStats(data);
    })
    .catch(hideLiveStats);
})();
