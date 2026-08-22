/* Material Everything site — vanilla JS, no dependencies. */
"use strict";

const MODULES = [
  { id:"api_client", name:"API Client", desc:"Send REST and GraphQL requests with environments, auth, history and response inspection.", tag:"Developer" },
  { id:"audio_editor", name:"Audio Editor", desc:"Multi-track waveform editing, effects, fades and export to common formats.", tag:"Media" },
  { id:"calculator", name:"Calculator", desc:"Standard, scientific and programmer modes with tape history and unit conversion.", tag:"Utility" },
  { id:"calendar", name:"Calendar", desc:"Month, week and day views with events, reminders, recurrence and ICS import/export.", tag:"Productivity" },
  { id:"chat", name:"Chat", desc:"Local-first messaging with channels, threads, markdown rendering and search.", tag:"Communication" },
  { id:"clipboard_manager", name:"Clipboard Manager", desc:"Searchable clipboard history with pinning, snippets and paste formatting.", tag:"Utility" },
  { id:"clock_timer", name:"Clock & Timer", desc:"World clock, stopwatch, countdown timers and alarms.", tag:"Utility" },
  { id:"database_client", name:"Database Client", desc:"Browse SQLite/MySQL/Postgres schemas, run queries and export results.", tag:"Developer" },
  { id:"download_manager", name:"Download Manager", desc:"Queue, pause, resume and schedule downloads with speed limits.", tag:"Network" },
  { id:"ftp_client", name:"FTP / SFTP / SCP Client", desc:"Dual-pane remote browsing with bookmarks, sync, queue and permissions editor.", tag:"Network" },
  { id:"git_client", name:"Git Client", desc:"Visual staging, branching, diffs, history graphs and commit authoring.", tag:"Developer" },
  { id:"hex_editor", name:"Hex Editor", desc:"Binary inspection with patterns, structure overlays, checksums and patching.", tag:"Developer" },
  { id:"media_player", name:"Media Player", desc:"Audio/video playback with playlists, chapters, subtitles and equalizer.", tag:"Media" },
  { id:"notes", name:"Notes", desc:"Markdown notes with tags, backlinks, full-text search and local versioning.", tag:"Productivity" },
  { id:"paint", name:"Paint", desc:"Raster drawing with layers, brushes, shapes, selection tools and export.", tag:"Creative" },
  { id:"password_manager", name:"Password Manager", desc:"Encrypted local vault with generator, TOTP support and auto-lock.", tag:"Security" },
  { id:"pdf_reader", name:"PDF Reader", desc:"Annotate, highlight, fill forms, extract pages and read PDFs offline.", tag:"Documents" },
  { id:"presentation", name:"Presentation", desc:"Slide decks with themes, speaker notes, transitions and export.", tag:"Productivity" },
  { id:"rss_reader", name:"RSS Reader", desc:"Feed subscriptions, unread tracking, folders and article view.", tag:"Reading" },
  { id:"screenshot_tool", name:"Screenshot Tool", desc:"Region, window and full-screen capture with annotation and delay.", tag:"Utility" },
  { id:"settings", name:"Settings", desc:"Central preferences for appearance, language, privacy, updates and per-module defaults.", tag:"System" },
  { id:"spreadsheet", name:"Spreadsheet", desc:"Formulas, charts, sorting/filtering, CSV/XLSX round-trip and frozen panes.", tag:"Productivity" },
  { id:"text_editor", name:"Text Editor", desc:"Fast plain-text/code editing with syntax highlighting, tabs, regex search and sessions.", tag:"Developer" },
  { id:"torrent_client", name:"Torrent Client", desc:"Magnet/torrent handling with queueing, ratio rules, peers view and scheduling.", tag:"Network" },
  { id:"video_editor", name:"Video Editor", desc:"Timeline cutting, transitions, audio mixing, titles and render presets.", tag:"Media" },
  { id:"web_browser", name:"Web Browser", desc:"Tabbed browsing with bookmarks, history, private windows and reader mode.", tag:"Internet" },
  { id:"word_processor", name:"Word Processor", desc:"Rich documents with styles, tables, headers, spellcheck and PDF export.", tag:"Documents" },
];

const DOCS = {};
MODULES.forEach(m => {
  DOCS[m.id] = {
    title: m.name,
    html: `<p>${m.desc}</p>
      <h3>Overview</h3><p>The <strong>${m.name}</strong> module is a first-class surface inside the Material Everything shell. It shares the app's design tokens, command palette, notification centre and settings store.</p>
      <h3>Getting started</h3><ol><li>Open the module from the tab strip or command palette (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>).</li><li>Use its toolbar for the primary actions; right-click any element for context actions.</li><li>All files stay on your machine unless you explicitly connect to a network service.</li></ol>
      <h3>Key capabilities</h3><ul><li>Fully keyboard accessible with visible focus.</li><li>Light and dark themes from one shared palette.</li><li>Export/import in open formats where applicable.</li></ul>`
  };
});
DOCS.getting_started = { title: "Getting Started",
  html:`<p>Material Everything is a single native C++ desktop application built with Qt and a custom Material Design 3 layer.</p>
  <h3>Install</h3><p>Download the Windows installer from the project's GitHub Releases once published. No external runtime or CDN dependency is required after install.</p>
  <h3>First launch</h3><p>Pick your modules from the tab strip, then personalise the theme, fonts and density under Settings. Every preference persists locally.</p>` };
DOCS.theming = { title: "Theming & Appearance",
  html:`<p>The suite uses Material Design 3 color roles: primary, surface, surface-variant and outline. Dark mode follows the same token set, so every module stays consistent.</p>
  <ul><li>Theme toggle lives in the header (and Settings).</li><li>Per-element appearance editors are planned via the shell's context menus.</li><li>Reduced-motion preferences are respected everywhere.</li></ul>` };
DOCS.keyboard = { title: "Keyboard & Accessibility",
  html:`<p>Every control is reachable by keyboard. Focus rings use the primary color at high contrast; screen readers receive names, roles and states through native Qt accessibility APIs.</p>
  <table><tr><th>Action</th><th>Shortcut</th></tr><tr><td>Command palette</td><td><kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd></td></tr><tr><td>Next tab</td><td><kbd>Ctrl</kbd>+<kbd>Tab</kbd></td></tr><tr><td>Site search focus</td><td><kbd>/</kbd></td></tr></table>` };

const CHANGELOG = [
  { date:"2026-08-22", type:"feature", ver:"0.4.0", title:"FTP/SFTP/SCP client lands", items:["Dual-pane browsing","Transfer queue","Bookmarks","Directory sync","Permissions editor"] },
  { date:"2026-08-22", type:"release", ver:"0.3.0", title:"Word processor + settings merge", items:["Rich text word processor","Centralized settings module"] },
  { date:"2026-08-21", type:"feature", ver:"0.2.0", title:"Screenshot tool", items:["Region capture","Annotation overlay"] },
  { date:"2026-08-21", type:"fix", ver:"0.1.1", title:"Stability fixes", items:["Fixed crash on empty workspace restore"] },
  { date:"2026-08-20", type:"feature", ver:"0.1.0", title:"Initial alpha", items:["Core shell","Module framework","Material 3 theming"] },
];

/* ---------- Theme ---------- */
const themeBtn = document.getElementById("theme-toggle");
function setTheme(t){ document.documentElement.dataset.theme = t; localStorage.setItem("me-theme", t); }
setTheme(localStorage.getItem("me-theme") || (matchMedia("(prefers-color-scheme: light)").matches ? "light":"dark"));
themeBtn.addEventListener("click", ()=> setTheme(document.documentElement.dataset.theme === "dark" ? "light" : "dark"));

/* ---------- Tabs ---------- */
const tabs = [...document.querySelectorAll('[role="tab"]')];
function selectTab(id){
  tabs.forEach(t => {
    const sel = t.id === "tab-" + id;
    t.setAttribute("aria-selected", sel);
    document.getElementById("panel-" + t.id.replace("tab-","")).classList.toggle("hidden", !sel);
    if (sel) t.focus();
  });
}
tabs.forEach((t,i) => {
  t.addEventListener("click", ()=> selectTab(t.id.replace("tab-","")));
  t.addEventListener("keydown", e=>{
    let j = null;
    if(e.key==="ArrowRight") j=(i+1)%tabs.length;
    if(e.key==="ArrowLeft") j=(i-1+tabs.length)%tabs.length;
    if(j!==null){ e.preventDefault(); selectTab(tabs[j].id.replace("tab-","")); }
  });
});
document.querySelectorAll("[data-goto]").forEach(a=>a.addEventListener("click",e=>{e.preventDefault();selectTab(a.dataset.goto);}));

/* ---------- Module grids ---------- */
function cardHTML(m){
  return `<div class="card feature-card" data-id="${m.id}"><span class="tag">${m.tag}</span><h3>${m.name}</h3><p>${m.desc}</p></div>`;
}
document.getElementById("home-modules").innerHTML = MODULES.slice(0,8).map(cardHTML).join("");
const fg = document.getElementById("feature-grid");
fg.innerHTML = MODULES.map(cardHTML).join("");
document.getElementById("module-filter").addEventListener("input", e=>{
  const q = e.target.value.toLowerCase();
  fg.querySelectorAll(".feature-card").forEach(c=>{
    c.classList.toggle("hidden", !c.textContent.toLowerCase().includes(q));
  });
});
fg.querySelectorAll(".feature-card").forEach(c=>c.addEventListener("click",()=>openDoc(c.dataset.id)));

/* ---------- Docs ---------- */
const docList = document.getElementById("doc-list");
Object.entries(DOCS).forEach(([id,d])=>{
  const li=document.createElement("li");
  const b=document.createElement("button");
  b.textContent=d.title; b.dataset.doc=id;
  b.setAttribute("aria-current","false");
  b.addEventListener("click",()=>openDoc(id));
  li.appendChild(b); docList.appendChild(li);
});
function openDoc(id){
  selectTab("docs");
  const d = DOCS[id]; if(!d) return;
  document.getElementById("doc-article").innerHTML = `<h2>${d.title}</h2>` + d.html;
  docList.querySelectorAll("button").forEach(b=>b.setAttribute("aria-current", String(b.dataset.doc===id)));
}
openDoc("getting_started");

/* ---------- Changelog ---------- */
const clEl = document.getElementById("changelog");
function renderChangelog(){
  const q = document.getElementById("cl-text").value.toLowerCase();
  const dt = document.getElementById("cl-date").value;
  const ty = document.getElementById("cl-type").value;
  clEl.innerHTML = CHANGELOG
    .filter(e => (!dt || e.date===dt) && (!ty || e.type===ty) && (!q || (e.title+" "+e.items.join(" ")).toLowerCase().includes(q)))
    .map(e=>`<article class="card cl-entry"><h3>${e.title} <span class="cl-badge cl-${e.type}">${e.type} · v${e.ver}</span></h3><time datetime="${e.date}">${e.date}</time><ul>${e.items.map(i=>`<li>${i}</li>`).join("")}</ul></article>`)
    .join("") || `<p class="note">No entries match the current filters.</p>`;
}
["cl-date","cl-text","cl-type"].forEach(id=>document.getElementById(id).addEventListener("input",renderChangelog));
renderChangelog();

/* ---------- Search + regex builder ---------- */
const SEARCH_INDEX = [];
MODULES.forEach(m=>SEARCH_INDEX.push({text:m.name+" "+m.desc, goto:()=>{selectTab("features");}, label:"Feature · "+m.name}));
Object.entries(DOCS).forEach(([id,d])=>SEARCH_INDEX.push({text:d.title+" "+d.html.replace(/<[^>]*>/g," "), goto:()=>openDoc(id), label:"Doc · "+d.title}));
CHANGELOG.forEach(e=>SEARCH_INDEX.push({text:e.title+" "+e.items.join(" "), goto:()=>{selectTab("changelog");}, label:"Changelog · "+e.title}));

const searchInput = document.getElementById("site-search");
const results = document.getElementById("search-results");
let activeRegex = null;
function runSearch(){
  const q = searchInput.value.trim();
  results.innerHTML = ""; results.classList.toggle("hidden", !q);
  if(!q) return;
  let matches;
  try {
    if(activeRegex){
      const flags = [activeRegex.ignoreCase&&"i", activeRegex.multiline&&"m"].filter(Boolean).join("");
      const re = new RegExp(activeRegex.pattern, flags);
      matches = SEARCH_INDEX.filter(it=>re.test(it.text)).slice(0,10);
    } else {
      matches = SEARCH_INDEX.filter(it=>it.text.toLowerCase().includes(q.toLowerCase())).slice(0,10);
    }
  } catch(err){ matches=[]; }
  matches.forEach(m=>{
    const div=document.createElement("div");
    div.textContent=m.label; div.setAttribute("role","option");
    div.addEventListener("click",()=>{m.goto();results.classList.add("hidden");});
    results.appendChild(div);
  });
  if(!matches.length){ const p=document.createElement("div"); p.textContent="No matches."; results.appendChild(p);}
}
searchInput.addEventListener("input", runSearch);
searchInput.addEventListener("keydown", e=>{ if(e.key==="Escape"){searchInput.value="";runSearch();} });
document.addEventListener("keydown", e=>{ if(e.key==="/" && document.activeElement.tagName!=="INPUT"){e.preventDefault();searchInput.focus();} });

const rb = document.getElementById("regex-builder");
document.getElementById("regex-btn").addEventListener("click", ()=>rb.classList.toggle("hidden"));
document.getElementById("rb-close").addEventListener("click", ()=>rb.classList.add("hidden"));
document.getElementById("rb-pattern").addEventListener("input", ()=>{
  const pat = document.getElementById("rb-pattern").value;
  const st = document.getElementById("rb-status");
  st.textContent = "";
  if(!pat){ return; }
  const flags = ["i","m"].filter(f=>document.getElementById("rb-"+f).checked).join("");
  try { new RegExp(pat, flags); st.textContent="✓ Valid pattern"; st.style.color="var(--md-primary)"; }
  catch(err){ st.textContent="✗ Invalid pattern"; st.style.color="#b3261e"; }
});
document.getElementById("rb-apply").addEventListener("click", ()=>{
  const pat = document.getElementById("rb-pattern").value.trim();
  if(!pat){ activeRegex=null; runSearch(); return; }
  activeRegex = { pattern:pat, ignoreCase:document.getElementById("rb-i").checked, multiline:document.getElementById("rb-m").checked };
  runSearch(); rb.classList.add("hidden");
});
document.addEventListener("click", e=>{
  if(!e.target.closest(".search-wrap")){ results.classList.add("hidden"); rb.classList.add("hidden"); }
});
