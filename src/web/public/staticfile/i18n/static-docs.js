(function () {
  'use strict';

  /* ── language detection ─────────────────────────────── */
  function detectLocale() {
    var urlLocale = getUrlLocale();
    if (urlLocale) return urlLocale;
    var primary = normalize(localStorage.getItem('cosmo.locale'));
    var legacy = normalize(localStorage.getItem('language'));
    if (primary !== legacy && legacy === 'en-US') return legacy;
    if (primary) return primary;
    if (legacy) return legacy;
    return normalize(navigator.language) || 'zh-CN';
  }

  function getUrlLocale() {
    var params = new URLSearchParams(location.search || '');
    var raw =
      params.get('locale') ||
      params.get('lang') ||
      params.get('language');
    return normalize(raw);
  }

  function normalize(raw) {
    if (!raw) return '';
    var s = raw.replace(/_/g, '-').toLowerCase();
    if (/^zh/.test(s)) return 'zh-CN';
    if (/^en/.test(s)) return 'en-US';
    return '';
  }

  /* ── page name from URL ─────────────────────────────── */
  function getPageName() {
    var m = location.pathname.match(/\/([^\/]+)\.html$/);
    return m ? m[1] : '';
  }

  /* ── fetch locale JSON ──────────────────────────────── */
  function fetchLocale(page, locale) {
    var base = location.pathname.replace(/\/[^\/]*$/, '/');
    var url = base + 'i18n/' + page + '.' + locale + '.json';
    return fetch(url).then(function (r) {
      if (!r.ok) throw new Error(r.status);
      return r.json();
    });
  }

  /* ── apply translations ─────────────────────────────── */
  function apply(dict, locale) {
    if (dict.docTitle) document.title = dict.docTitle;
    document.documentElement.lang = locale;

    var mappings = [
      ['data-i18n', 'textContent'],
      ['data-i18n-html', 'innerHTML'],
      ['data-i18n-title', 'title'],
      ['data-i18n-placeholder', 'placeholder']
    ];
    mappings.forEach(function (m) {
      var attr = m[0], prop = m[1];
      document.querySelectorAll('[' + attr + ']').forEach(function (el) {
        var key = el.getAttribute(attr);
        if (dict[key] === undefined) return;
        if (prop === 'textContent') el.textContent = dict[key];
        else if (prop === 'innerHTML') el.innerHTML = dict[key];
        else el.setAttribute(prop, dict[key]);
      });
    });

    updateSwitcher(locale);
  }

  /* ── language switcher ──────────────────────────────── */
  var bar;

  function injectStyles() {
    var sty = document.createElement('style');
    sty.textContent =
      '#i18n-switcher{position:sticky;top:0;z-index:100;display:flex;' +
        'justify-content:flex-end;gap:4px;padding:6px 0;margin-bottom:8px;' +
        'background:rgba(255,255,255,.92);backdrop-filter:blur(4px)}' +
      '#i18n-switcher button{border:1px solid #d9e2ec;border-radius:4px;' +
        'padding:3px 10px;font-size:13px;background:#fff;color:#1f2933;' +
        'cursor:pointer;transition:background .15s,color .15s}' +
      '#i18n-switcher button.active{background:#2563eb;color:#fff;' +
        'border-color:#2563eb}' +
      '#i18n-switcher button:hover:not(.active){background:#f0f4f8}';
    document.head.appendChild(sty);
  }

  function createSwitcher(locale) {
    injectStyles();
    bar = document.createElement('div');
    bar.id = 'i18n-switcher';
    bar.innerHTML =
      '<button data-locale="zh-CN">\u4E2D\u6587</button>' +
      '<button data-locale="en-US">English</button>';

    var main = document.querySelector('main') || document.body;
    main.insertBefore(bar, main.firstChild);

    bar.addEventListener('click', function (e) {
      var btn = e.target.closest
        ? e.target.closest('button[data-locale]')
        : null;
      if (!btn) return;
      var loc = btn.getAttribute('data-locale');
      localStorage.setItem('cosmo.locale', loc);
      localStorage.setItem('language', loc);
      loadAndApply(loc);
    });
    updateSwitcher(locale);
  }

  function updateSwitcher(locale) {
    if (!bar) return;
    var btns = bar.querySelectorAll('button');
    for (var i = 0; i < btns.length; i++) {
      if (btns[i].getAttribute('data-locale') === locale)
        btns[i].classList.add('active');
      else btns[i].classList.remove('active');
    }
  }

  /* ── load & apply ───────────────────────────────────── */
  var page = getPageName();
  var curLocale = detectLocale();

  function loadAndApply(loc) {
    curLocale = loc;
    if (!page) return;
    fetchLocale(page, loc)
      .catch(function () {
        if (loc !== 'zh-CN') return fetchLocale(page, 'zh-CN');
        throw new Error('no locale');
      })
      .then(function (dict) { apply(dict, loc); })
      .catch(function () { /* keep original HTML text */ });
  }

  createSwitcher(curLocale);
  loadAndApply(curLocale);
})();
