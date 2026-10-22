#pragma once
// Gemeinsames Stylesheet beider Weboberflächen.

constexpr const char* WEB_STYLE = R"CSS(
:root{--bg:#f4f5f7;--card:#fff;--text:#1d2330;--muted:#5d6675;--line:#dde1e7;
--accent:#1f5fbf;--ok:#1b7a3c;--okbg:#e5f4ea;--err:#b3261e;--errbg:#fbe9e7;--warn:#8a5a00;--warnbg:#fff4dc}
@media (prefers-color-scheme:dark){:root{--bg:#14171c;--card:#1d2128;--text:#e7e9ee;--muted:#9aa3b2;
--line:#2f3540;--accent:#7fb0ff;--ok:#7fd69b;--okbg:#17301f;--err:#ff9b92;--errbg:#3a1a18;--warn:#ffd27a;--warnbg:#3a2d10}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{background:var(--card);border-bottom:1px solid var(--line);padding:12px 16px;display:flex;gap:16px;align-items:baseline;flex-wrap:wrap}
header h1{font-size:20px;margin:0}
header a{color:var(--accent);text-decoration:none}
main{max-width:980px;margin:0 auto;padding:16px;display:grid;gap:16px}
.grid{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px}
section h2{font-size:17px;margin:0 0 12px}
label{display:block;font-size:14px;color:var(--muted);margin:8px 0 4px}
input[type=text]{width:100%;padding:10px;font-size:17px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--text)}
.buttons{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
button{padding:10px 14px;font-size:15px;border-radius:8px;border:1px solid var(--accent);background:var(--accent);color:#fff;cursor:pointer}
@media (prefers-color-scheme:dark){button{color:#10141a}}
button.secondary{background:transparent;color:var(--accent)}
.msg{padding:10px 12px;border-radius:8px;margin-top:12px;font-weight:600}
.msg.ok{background:var(--okbg);color:var(--ok)}
.msg.err{background:var(--errbg);color:var(--err)}
.msg.warn{background:var(--warnbg);color:var(--warn)}
.stats{display:flex;gap:12px;flex-wrap:wrap}
.stat{flex:1;min-width:110px;border:1px solid var(--line);border-radius:8px;padding:10px}
.stat b{display:block;font-size:26px}
.stat span{font-size:13px;color:var(--muted)}
.table{overflow-x:auto}
table{border-collapse:collapse;width:100%;font-size:14px}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--muted);font-weight:600}
td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
.gate{display:flex;align-items:center;gap:10px;font-size:18px;font-weight:700}
.lamp{width:18px;height:18px;border-radius:50%;background:var(--line);border:1px solid var(--muted)}
.lamp.on{background:#2ecc55;box-shadow:0 0 10px #2ecc55}
.muted{color:var(--muted);font-size:14px}
pre{overflow-x:auto;font-size:13px;background:var(--bg);padding:10px;border-radius:8px}
button:disabled,input:disabled{opacity:.55;cursor:progress}
.msg.busy{background:var(--bg);color:var(--muted);display:flex;align-items:center;gap:10px}
.spin{width:16px;height:16px;border:2px solid var(--line);border-top-color:var(--accent);border-radius:50%;animation:spin .8s linear infinite;flex:none}
@keyframes spin{to{transform:rotate(360deg)}}
.pill{font-size:13px;padding:2px 10px;border-radius:999px;font-weight:600}
.pill.ok{background:var(--okbg);color:var(--ok)}
.pill.err{background:var(--errbg);color:var(--err)}
.spacer{flex:1}
.head-actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.head-actions button{padding:6px 12px;font-size:14px}
)CSS";
