#include "diagnostics_http.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "c6_ota.hpp"
#include "esp_https_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mdns.h"
#include "system_diagnostics.hpp"
#include "web_security.hpp"
#include "web_zigbee_approval.hpp"

namespace {

constexpr char kTag[] = "http_diagnostics";
hub::wifi::Provisioning* provisioning_service = nullptr;
hub::radio::MultipanService* multipan_service = nullptr;
hub::radio::IZigbeeCoordinator* zigbee_coordinator = nullptr;
hub::diagnostics::WebSecurity* web_security = nullptr;
hub::diagnostics::WebZigbeeApproval* web_zigbee_approval = nullptr;
httpd_handle_t http_server_handle = nullptr;
httpd_handle_t https_server_handle = nullptr;
char session_cookie_response[128]{};
portMUX_TYPE thread_scan_mux = portMUX_INITIALIZER_UNLOCKED;
bool thread_scan_running = false;
std::uint8_t thread_border_agent_count = 0;
esp_err_t thread_scan_last_error = ESP_ERR_INVALID_STATE;

const char* thread_role_name(hub::radio::ThreadRole role) noexcept {
    switch (role) {
        case hub::radio::ThreadRole::uncommissioned: return "uncommissioned";
        case hub::radio::ThreadRole::disabled: return "disabled";
        case hub::radio::ThreadRole::detached: return "detached";
        case hub::radio::ThreadRole::child: return "child";
        case hub::radio::ThreadRole::router: return "router";
        case hub::radio::ThreadRole::leader: return "leader";
        default: return "unknown";
    }
}

const char* multipan_state_name(hub::radio::MultipanState state) noexcept {
    switch (state) {
        case hub::radio::MultipanState::disabled: return "disabled";
        case hub::radio::MultipanState::needs_uart_configuration: return "needs_uart_configuration";
        case hub::radio::MultipanState::starting: return "starting";
        case hub::radio::MultipanState::running: return "running";
        case hub::radio::MultipanState::failed: return "failed";
    }
    return "unknown";
}

const char* c6_audit_state_name(hub::C6OtaAuditState state) noexcept {
    switch (state) {
        case hub::C6OtaAuditState::started: return "started";
        case hub::C6OtaAuditState::pending_verify: return "pending_verify";
        case hub::C6OtaAuditState::verified: return "verified";
        case hub::C6OtaAuditState::failed: return "failed";
        default: return "not_run";
    }
}

const char kDiagnosticsPage[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>P4 Multi Hub</title>
<style>
:root{color-scheme:dark;--bg:#111a1e;--surface:#1b292d;--line:#34464a;--mint:#48d6a5;--text:#f1f5f2;--muted:#a8b8b5;--alert:#ffbd75;--red:#ff8b7b}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px system-ui,sans-serif;min-height:100vh}
main{max-width:900px;margin:0 auto;padding:24px 18px}header{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--line);padding-bottom:16px}
h1{font-size:24px;margin:0}h2{font-size:18px;margin:0 0 14px}button{border:0;border-radius:6px;padding:11px 15px;background:var(--mint);color:#082119;font-weight:700;cursor:pointer}button:disabled{opacity:.42;cursor:not-allowed}
section{margin-top:18px;padding:18px;background:var(--surface);border:1px solid var(--line);border-radius:8px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.item{padding:12px;border:1px solid var(--line);border-radius:6px;min-width:0}.label{color:var(--muted);font-size:13px}.value{margin-top:7px;font-size:16px;overflow-wrap:anywhere}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:14px}
input{max-width:100%;padding:10px 12px;border:1px solid var(--line);border-radius:5px;background:#0d1518;color:var(--text);font:inherit}form{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.notice{color:var(--muted);margin:10px 0 0}.error{color:var(--red)}.ok{color:var(--mint)}.hidden{display:none!important}.device-list{margin:12px 0 0;padding:0;list-style:none}.device-list li{padding:10px 0;border-top:1px solid var(--line)}
footer{color:var(--muted);font-size:13px;margin-top:18px}@media(max-width:580px){.grid{grid-template-columns:1fr}main{padding:16px 12px}section{padding:14px}form>*{flex:1 1 100%}form button{flex:1}}
</style>
</head>
<body><main>
<header><h1 data-i="title">P4 Multi Hub</h1><button id="language" type="button">PL</button></header>
<section id="loginSection"><h2 data-i="loginTitle">Hub login</h2><form id="loginForm"><input id="username" aria-label="Username" autocomplete="username" value="admin" required><input id="password" aria-label="Password" type="password" autocomplete="current-password" required><button data-i="login">Sign in</button></form><p id="authMessage" class="notice" role="status"></p><p class="notice" data-i="loginHint">The one-time first-use password is printed once to the P4 serial log. Verify the per-device TLS fingerprint there.</p></section>
<section id="changeSection" class="hidden"><h2 data-i="changeTitle">Set a new admin password</h2><form id="changeForm"><input id="newPassword" type="password" autocomplete="new-password" minlength="12" maxlength="64" required><button data-i="changePassword">Change password</button></form><p class="notice" data-i="changeHint">Use at least 12 characters. Change the one-time password before Zigbee actions.</p></section>
<div id="app" class="hidden">
<section><h2 data-i="searchTitle">Zigbee search</h2><div class="grid"><div class="item"><div class="label" data-i="coordinator">Coordinator</div><div class="value" id="coordinator">-</div></div><div class="item"><div class="label" data-i="network">Network</div><div class="value" id="network">-</div></div><div class="item"><div class="label" data-i="joinWindow">Join window</div><div class="value" id="joinWindow">-</div></div><div class="item"><div class="label" data-i="bdb">BDB signal/status</div><div class="value" id="bdb">-</div></div></div><div class="row"><button id="createNetwork" data-i="createNetwork" type="button">Create network</button><button id="openJoin" data-i="openJoin" type="button">Search for devices (180 s)</button></div><p id="requestStatus" class="notice" role="status"></p></section>
<section><h2 data-i="nearbyTitle">Nearby Zigbee devices</h2><div class="value" id="nearbyCount">-</div><ul id="deviceList" class="device-list"></ul><p id="nearbyEmpty" class="notice"></p></section>
<section><h2 data-i="systemTitle">System status</h2><div class="item"><div class="label" data-i="wifi">Wi-Fi state</div><div class="value" id="state">Loading</div><div id="address" class="value"></div></div><div class="grid" style="margin-top:10px"><div class="item"><div class="label" data-i="store">Encrypted credential store</div><div class="value" id="store">-</div></div><div class="item"><div class="label" data-i="saved">Credentials saved</div><div class="value" id="saved">-</div></div><div class="item"><div class="label" data-i="rssi">Wi-Fi signal</div><div class="value" id="rssi">-</div></div><div class="item"><div class="label" data-i="flash">Flash encryption</div><div class="value" id="encryption">-</div></div><div class="item"><div class="label" data-i="psram">Free PSRAM</div><div class="value" id="psram">-</div></div><div class="item"><div class="label" data-i="heap">Free internal heap</div><div class="value" id="heap">-</div></div><div class="item"><div class="label" data-i="radio">Radio service</div><div class="value" id="radio">-</div></div><div class="item"><div class="label" data-i="thread">Thread role</div><div class="value" id="thread">-</div></div><div class="item"><div class="label" data-i="tbr">Thread Border Router</div><div class="value" id="tbr">-</div></div><div class="item"><div class="label" data-i="c6version">C6 ESP-Hosted version</div><div class="value" id="c6version">-</div></div><div class="item"><div class="label" data-i="c6identity">C6 target</div><div class="value" id="c6identity">-</div></div><div class="item"><div class="label" data-i="c6stage">C6 staged image</div><div class="value" id="c6stage">-</div></div><div class="item"><div class="label" data-i="c6ota">C6 OTA audit</div><div class="value" id="c6ota">-</div></div></div><div class="row"><button id="refresh" type="button" data-i="refresh">Refresh</button><button id="logout" type="button" data-i="logout">Sign out</button></div></section>
</div>
<footer><span data-i="note">HTTPS local controls. Zigbee writes also require confirmation on the hub touchscreen.</span></footer>
</main>
<script>
const words={en:{title:'P4 Multi Hub',loginTitle:'Hub login',login:'Sign in',loginHint:'The one-time first-use password is printed once to the P4 serial log. Verify the per-device TLS fingerprint there.',changeTitle:'Set a new admin password',changePassword:'Change password',changeHint:'Use at least 12 characters. Change the one-time password before Zigbee actions.',searchTitle:'Zigbee search',coordinator:'Coordinator',network:'Network',joinWindow:'Join window',bdb:'BDB signal/status',createNetwork:'Create network',openJoin:'Search for devices (180 s)',nearbyTitle:'Nearby Zigbee devices',systemTitle:'System status',wifi:'Wi-Fi state',store:'Encrypted credential store',saved:'Credentials saved',rssi:'Wi-Fi signal',flash:'Flash encryption',psram:'Free PSRAM',heap:'Free internal heap',radio:'Radio service',thread:'Thread role',tbr:'Thread Border Router',c6version:'C6 ESP-Hosted version',c6identity:'C6 target',c6stage:'C6 staged image',c6ota:'C6 OTA audit',note:'HTTPS local controls. Zigbee writes also require confirmation on the hub touchscreen.',refresh:'Refresh',logout:'Sign out',yes:'Yes',no:'No',ready:'Ready',unavailable:'Unavailable',enabled:'Enabled',disabled:'Disabled',online:'Connected',connecting:'Connecting',scanning:'Scanning',failed:'Needs attention',loading:'Loading',formed:'Formed',notFormed:'Not formed',pending:'Awaiting touchscreen confirmation',searchOpen:'Join window open',closed:'Closed',deviceCount:'Announced devices',emptyDevices:'No Zigbee device announcements yet.',detailsPending:'Device details will appear after interview support is added.',confirmCreate:'Create a Zigbee network? The hub touchscreen must approve this request.',confirmJoin:'Open a 180-second Zigbee join window? The hub touchscreen must approve this request.',sent:'Request sent. Confirm it on the hub touchscreen.',rejected:'Request rejected on the hub touchscreen.',accepted:'Command accepted by the hub; coordinator status will update separately.',expired:'Touch confirmation expired.',requestFailed:'Request failed',authFailed:'Sign-in failed.',passwordMismatch:'Password must be at least 12 characters.',passwordChanged:'Password changed. Sign in again.',createDisabled:'Create network is unavailable in the current coordinator state.',joinDisabled:'A formed Zigbee network is required before devices can join.'},pl:{title:'P4 Multi Hub',loginTitle:'Logowanie do huba',login:'Zaloguj',loginHint:'Jednorazowe haslo startowe jest wyswietlane raz w logu szeregowym P4. Tam sprawdz tez odcisk TLS urzadzenia.',changeTitle:'Ustaw nowe haslo administratora',changePassword:'Zmien haslo',changeHint:'Haslo musi miec co najmniej 12 znakow. Zmien jednorazowe haslo przed operacjami Zigbee.',searchTitle:'Wyszukiwanie Zigbee',coordinator:'Koordynator',network:'Siec',joinWindow:'Okno dolaczania',bdb:'Sygnal/status BDB',createNetwork:'Utworz siec',openJoin:'Szukaj urzadzen (180 s)',nearbyTitle:'Urzadzenia Zigbee w poblizu',systemTitle:'Stan systemu',wifi:'Stan Wi-Fi',store:'Szyfrowany magazyn danych',saved:'Dane zapisane',rssi:'Sygnal Wi-Fi',flash:'Szyfrowanie flash',psram:'Wolny PSRAM',heap:'Wolna pamiec wewnetrzna',radio:'Usluga radiowa',thread:'Rola Thread',tbr:'Thread Border Router',c6version:'Wersja ESP-Hosted C6',c6identity:'Cel C6',c6stage:'Obraz C6 w stagingu',c6ota:'Audyt OTA C6',note:'Lokalne sterowanie HTTPS. Operacje Zigbee wymagaja tez zatwierdzenia na ekranie huba.',refresh:'Odswiez',logout:'Wyloguj',yes:'Tak',no:'Nie',ready:'Gotowy',unavailable:'Niedostepny',enabled:'Wlaczone',disabled:'Wylaczone',online:'Polaczono',connecting:'Laczenie',scanning:'Skanowanie',failed:'Wymaga uwagi',loading:'Ladowanie',formed:'Utworzona',notFormed:'Nieutworzona',pending:'Oczekuje na potwierdzenie na ekranie',searchOpen:'Okno dolaczania otwarte',closed:'Zamkniete',deviceCount:'Ogloszone urzadzenia',emptyDevices:'Brak ogloszen urzadzen Zigbee.',detailsPending:'Szczegoly pojawia sie po dodaniu obslugi interview.',confirmCreate:'Utworzyc siec Zigbee? Zatwierdz to zadanie na ekranie huba.',confirmJoin:'Otworzyc 180-sekundowe okno dolaczania? Zatwierdz to zadanie na ekranie huba.',sent:'Zadanie wyslane. Zatwierdz je na ekranie huba.',rejected:'Zadanie odrzucono na ekranie huba.',accepted:'Polecenie zaakceptowane; stan koordynatora zaktualizuje sie osobno.',expired:'Czas na potwierdzenie dotykiem minal.',requestFailed:'Zadanie nie powiodlo sie',authFailed:'Logowanie nie powiodlo sie.',passwordMismatch:'Haslo musi miec co najmniej 12 znakow.',passwordChanged:'Haslo zmienione. Zaloguj sie ponownie.',createDisabled:'Utworzenie sieci jest niedostepne w obecnym stanie koordynatora.',joinDisabled:'Aby dolaczac urzadzenia, najpierw musi powstac siec Zigbee.'}};
let lang='en',authHeader='',adminUser='',adminPassword='',mustChange=true,loggedIn=false,lastC6=null;
function setLanguage(){document.documentElement.lang=lang;document.querySelectorAll('[data-i]').forEach(e=>e.textContent=words[lang][e.dataset.i]);document.getElementById('language').textContent=lang==='en'?'PL':'EN'}
function showAuth(message=''){document.getElementById('app').classList.add('hidden');document.getElementById('changeSection').classList.add('hidden');document.getElementById('loginSection').classList.remove('hidden');document.getElementById('authMessage').textContent=message}
async function authCheck(){if(!authHeader&&!loggedIn)return showAuth();try{const headers=authHeader?{Authorization:authHeader}:{};const r=await fetch('/api/auth/status',{headers,cache:'no-store'});if(!r.ok)throw new Error('auth');const d=await r.json();mustChange=d.must_change_password;loggedIn=true;authHeader='';adminPassword='';document.getElementById('loginSection').classList.add('hidden');document.getElementById('changeSection').classList.toggle('hidden',!mustChange);document.getElementById('app').classList.toggle('hidden',mustChange);if(!mustChange){refreshAll();refreshRequest()}}catch(_){loggedIn=false;authHeader='';adminPassword='';showAuth(words[lang].authFailed)}}
document.getElementById('loginForm').addEventListener('submit',async e=>{e.preventDefault();adminUser=document.getElementById('username').value;adminPassword=document.getElementById('password').value;authHeader='Basic '+btoa(adminUser+':'+adminPassword);await authCheck()});
document.getElementById('changeForm').addEventListener('submit',async e=>{e.preventDefault();const p=document.getElementById('newPassword').value;if(p.length<12)return document.getElementById('authMessage').textContent=words[lang].passwordMismatch;const r=await fetch('/api/auth/password',{method:'POST',headers:{'Content-Type':'text/plain'},body:p});if(!r.ok)return document.getElementById('authMessage').textContent=words[lang].requestFailed;authHeader='';adminPassword='';loggedIn=false;document.getElementById('newPassword').value='';await authCheck()});
async function postAction(path,confirmText){if(!loggedIn||!confirm(words[lang][confirmText]))return;try{const r=await fetch(path,{method:'POST'});if(!r.ok)throw new Error('request');document.getElementById('requestStatus').textContent=words[lang].sent;refreshRequest()}catch(_){document.getElementById('requestStatus').textContent=words[lang].requestFailed;if(!loggedIn)showAuth(words[lang].authFailed)}}
async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error('status');const d=await r.json();const labels={starting:words[lang].loading,scanning:words[lang].scanning,networks_ready:words[lang].ready,connecting:words[lang].connecting,connected:words[lang].online,failed:words[lang].failed};document.getElementById('state').textContent=labels[d.state]||d.state;document.getElementById('address').textContent=d.ipv4?`IPv4: ${d.ipv4}`:'';document.getElementById('store').textContent=d.credential_store_ready?words[lang].ready:words[lang].unavailable;document.getElementById('saved').textContent=d.credentials_saved?words[lang].yes:words[lang].no;document.getElementById('rssi').textContent=d.rssi_valid?`${d.rssi_dbm} dBm`:words[lang].unavailable;document.getElementById('encryption').textContent=d.flash_encryption?words[lang].enabled:words[lang].disabled;document.getElementById('psram').textContent=`${d.psram_free_kb} KB`;document.getElementById('heap').textContent=`${d.internal_heap_free_kb} KB`;document.getElementById('radio').textContent=d.multipan_state;document.getElementById('thread').textContent=d.thread_role;document.getElementById('tbr').textContent=d.tbr_ready?words[lang].ready:words[lang].unavailable}catch(_){document.getElementById('state').textContent=words[lang].failed}}
async function refreshC6(){try{const r=await fetch('/api/c6/status',{cache:'no-store'});if(!r.ok)throw new Error('c6');const d=await r.json();lastC6=d;document.getElementById('c6version').textContent=d.version_valid?d.version:words[lang].unavailable;document.getElementById('c6identity').textContent=d.chip_id_valid?`0x${d.chip_id.toString(16).padStart(8,'0')}`:words[lang].unavailable;document.getElementById('c6stage').textContent=d.staged_image_valid?`${d.staged_image_bytes} B, ${d.staged_app_version}, SHA ${d.staged_image_sha256.slice(0,12)}`:`${words[lang].unavailable} (${d.staged_image_error})`;document.getElementById('c6ota').textContent=`${d.ota_state}, ${d.ota_attempts} attempts, err ${d.ota_last_error}`;document.getElementById('coordinator').textContent=!d.zigbee_status_available?words[lang].unavailable:(d.zigbee_stack_ready?words[lang].ready:words[lang].loading);document.getElementById('network').textContent=d.zigbee_network_formed?words[lang].formed:(d.zigbee_search_pending?words[lang].pending:words[lang].notFormed);document.getElementById('joinWindow').textContent=d.zigbee_join_seconds?`${d.zigbee_join_seconds} s`:words[lang].closed;document.getElementById('bdb').textContent=d.zigbee_status_available?`0x${Number(d.zigbee_last_signal_type).toString(16)} / ${d.zigbee_last_bdb_status} / err ${d.zigbee_last_error}`:words[lang].unavailable;document.getElementById('nearbyCount').textContent=`${words[lang].deviceCount}: ${d.zigbee_device_count}`;document.getElementById('nearbyEmpty').textContent=d.zigbee_device_count?words[lang].detailsPending:words[lang].emptyDevices;document.getElementById('deviceList').replaceChildren();document.getElementById('createNetwork').disabled=!d.zigbee_status_available||!d.zigbee_stack_ready||d.zigbee_network_formed||d.zigbee_search_pending;document.getElementById('openJoin').disabled=!d.zigbee_network_formed||d.zigbee_join_seconds>0||d.zigbee_search_pending}catch(_){document.getElementById('coordinator').textContent=words[lang].unavailable}}
async function refreshRequest(){if(!loggedIn||mustChange)return;try{const r=await fetch('/api/zigbee/request',{cache:'no-store'});if(r.status===401){loggedIn=false;showAuth(words[lang].authFailed);return}if(!r.ok)throw new Error('request');const d=await r.json();const text={awaiting_touch:words[lang].pending,executing:words[lang].pending,completed:words[lang].accepted,rejected:words[lang].rejected,expired:words[lang].expired,failed:`${words[lang].requestFailed}: ${d.result}`};document.getElementById('requestStatus').textContent=text[d.state]||''}catch(_){}}
async function refreshAll(){await Promise.all([refresh(),refreshC6()]);refreshRequest()}
document.getElementById('createNetwork').addEventListener('click',()=>postAction('/api/zigbee/create','confirmCreate'));document.getElementById('openJoin').addEventListener('click',()=>postAction('/api/zigbee/join','confirmJoin'));document.getElementById('language').addEventListener('click',()=>{lang=lang==='en'?'pl':'en';setLanguage();if(loggedIn)authCheck()});document.getElementById('refresh').addEventListener('click',refreshAll);document.getElementById('logout').addEventListener('click',()=>{authHeader='';adminPassword='';loggedIn=false;showAuth()});setLanguage();setInterval(()=>{if(loggedIn&&!mustChange)refreshAll()},5000);
</script></body></html>)HTML";

const char kSessionLifecycleScript[] = R"HTML(<script>
document.getElementById('logout').addEventListener('click',()=>fetch('/api/auth/logout',{method:'POST'}));
words.en.sent='Request sent. Awaiting approval.';
words.pl.sent='Wyslano zadanie. Oczekuje na zatwierdzenie.';
words.en.scanNearby='Scan nearby Zigbee devices';
words.pl.scanNearby='Skanuj pobliskie urzadzenia Zigbee';
words.en.scanThread='Find Thread Border Routers';
words.pl.scanThread='Wykryj Thread Border Routery';
words.en.threadSearching='Searching for Border Agent advertisements…';
words.pl.threadSearching='Szukam ogloszen Border Agent…';
words.en.threadNone='No Border Agent advertisements found; this is not a device list.';
words.pl.threadNone='Nie znaleziono ogloszen Border Agent; to nie jest lista urzadzen.';
words.en.threadFound='Border Agent advertisements found:';
words.pl.threadFound='Znalezione ogloszenia Border Agent:';
words.en.threadScanUnavailable='Thread Border Agent scan unavailable:';
words.pl.threadScanUnavailable='Skan Border Agent niedostepny:';
words.en.confirmScanNearby='Scan nearby? If the network is new, this will form it and open a 180-second join window.';
words.pl.confirmScanNearby='Rozpoczac skan pobliskich urzadzen? Dla nowej sieci utworzy ja i otworzy okno dolaczania na 180 s.';
words.en.refreshC6='Refresh C6 diagnostics';
words.pl.refreshC6='Odswiez diagnostyke C6';
words.en.openJoin='Open join window (180 s)';
words.pl.openJoin='Otworz okno dolaczania (180 s)';
words.en.confirmWebApproveScan='Approve the nearby-device scan?';
words.pl.confirmWebApproveScan='Zatwierdzic skan pobliskich urzadzen?';
words.en.formationInProgress='Network formation in progress';
words.pl.formationInProgress='Trwa tworzenie sieci';
words.en.formationTimedOut='Formation timed out; BDB still in progress';
words.pl.formationTimedOut='Przekroczono czas tworzenia; BDB nadal pracuje';
words.en.approvalPending='Awaiting approval';
words.pl.approvalPending='Oczekuje na zatwierdzenie';
words.en.approvalReceived='Approved; waiting to send command';
words.pl.approvalReceived='Zatwierdzono; wysylanie polecenia';
words.en.commandSending='Sending command to coordinator';
words.pl.commandSending='Wysylanie polecenia do koordynatora';
words.en.webApprove='Approve in webGUI';
words.pl.webApprove='Zatwierdz w webGUI';
words.en.webReject='Reject';
words.pl.webReject='Odrzuc';
words.en.confirmWebApproveCreate='Approve creating the Zigbee network?';
words.pl.confirmWebApproveCreate='Zatwierdzic utworzenie sieci Zigbee?';
words.en.confirmWebApproveJoin='Approve opening the Zigbee join window?';
words.pl.confirmWebApproveJoin='Zatwierdzic otwarcie okna dolaczania Zigbee?';

const webApprovalActions=document.createElement('div');
webApprovalActions.className='row';
webApprovalActions.style.display='none';
const webApproveButton=document.createElement('button');
webApproveButton.type='button';
const webRejectButton=document.createElement('button');
webRejectButton.type='button';
webApprovalActions.append(webApproveButton,webRejectButton);
document.getElementById('requestStatus').after(webApprovalActions);
const scanNearbyButton=document.createElement('button');
scanNearbyButton.type='button';scanNearbyButton.dataset.i='scanNearby';
scanNearbyButton.style.width='100%';
scanNearbyButton.addEventListener('click',()=>postAction('/api/zigbee/scan','confirmScanNearby'));
document.getElementById('deviceList').after(scanNearbyButton);
const threadScanRow=document.createElement('div');
threadScanRow.className='row';
const threadScanButton=document.createElement('button');
threadScanButton.type='button';threadScanButton.dataset.i='scanThread';
const threadScanStatus=document.createElement('p');
threadScanStatus.className='notice';threadScanStatus.setAttribute('role','status');
threadScanRow.append(threadScanButton,threadScanStatus);
document.getElementById('tbr').closest('section').append(threadScanRow);
let webApprovalRequestId=0,webApprovalAction='none',webApprovalBusy=false,loginBusy=false,c6RefreshBusy=false;
const manualRefreshButton=document.createElement('button');
manualRefreshButton.id='refreshC6';manualRefreshButton.type='button';
manualRefreshButton.dataset.i='refreshC6';
document.getElementById('refresh').after(manualRefreshButton);

authCheck=async function(showFailure=false){
    try{
        const wasLoggedIn=loggedIn;
        const headers=authHeader?{Authorization:authHeader}:{};
        const response=await fetch('/api/auth/status',{headers,cache:'no-store',credentials:'same-origin'});
        if(!response.ok)throw new Error('auth');
        const status=await response.json();
        mustChange=status.must_change_password;
        loggedIn=true;authHeader='';adminPassword='';
        document.getElementById('loginSection').classList.add('hidden');
        document.getElementById('changeSection').classList.toggle('hidden',!mustChange);
        document.getElementById('app').classList.toggle('hidden',mustChange);
        if(!mustChange){
            await refreshAll();
            if(!wasLoggedIn)void refreshDetailedStatus(false);
        }
        return true;
    }catch(_){
        loggedIn=false;authHeader='';adminPassword='';
        showAuth(showFailure?words[lang].authFailed:'');
        return false;
    }
};

document.getElementById('loginForm').addEventListener('submit',async event=>{
    event.preventDefault();event.stopImmediatePropagation();
    if(loginBusy)return;
    loginBusy=true;
    const form=event.currentTarget;
    const button=form.querySelector('button');
    button.disabled=true;
    try{
        adminUser=document.getElementById('username').value;
        adminPassword=document.getElementById('password').value;
        authHeader='Basic '+btoa(adminUser+':'+adminPassword);
        await authCheck(true);
    }catch(_){
        showAuth(words[lang].authFailed);
    }finally{
        adminPassword='';
        document.getElementById('password').value='';
        button.disabled=false;loginBusy=false;
    }
},true);

refreshRequest=async function(){
    if(!loggedIn||mustChange)return;
    try{
        const response=await fetch('/api/zigbee/request',{cache:'no-store'});
        if(response.status===401){loggedIn=false;showAuth(words[lang].authFailed);return;}
        if(!response.ok)throw new Error('request');
        const request=await response.json();
        webApprovalRequestId=Number(request.id)||0;
        webApprovalAction=request.action;
        const states={
            awaiting_touch:words[lang].approvalPending,
            approved:words[lang].approvalReceived,
            executing:words[lang].commandSending,
            completed:words[lang].accepted,
            rejected:words[lang].rejected,
            expired:words[lang].expired,
            failed:`${words[lang].requestFailed}: ${request.result}`
        };
        document.getElementById('requestStatus').textContent=states[request.state]||'';
        const pending=request.state==='awaiting_touch'&&webApprovalRequestId>0;
        webApprovalActions.style.display=pending?'flex':'none';
        webApproveButton.textContent=words[lang].webApprove;
        webRejectButton.textContent=words[lang].webReject;
        webApproveButton.disabled=webApprovalBusy;
        webRejectButton.disabled=webApprovalBusy;
    }catch(_){}
};

async function decideWebApproval(approve){
    if(!loggedIn||!webApprovalRequestId||webApprovalBusy)return;
    const confirmation=webApprovalAction==='create_network'
        ? words[lang].confirmWebApproveCreate
        : webApprovalAction==='scan_nearby'
            ? words[lang].confirmWebApproveScan : words[lang].confirmWebApproveJoin;
    if(approve&&!confirm(confirmation))return;
    webApprovalBusy=true;
    webApproveButton.disabled=true;webRejectButton.disabled=true;
    try{
        const response=await fetch(`/api/zigbee/${approve?'approve':'reject'}`,{
            method:'POST',headers:{'Content-Type':'text/plain'},
            body:String(webApprovalRequestId),cache:'no-store'
        });
        if(response.status===401){loggedIn=false;showAuth(words[lang].authFailed);return;}
        if(!response.ok)throw new Error('decision');
        await refreshRequest();
    }catch(_){
        document.getElementById('requestStatus').textContent=words[lang].requestFailed;
    }finally{
        webApprovalBusy=false;
        webApproveButton.disabled=false;webRejectButton.disabled=false;
    }
}
webApproveButton.addEventListener('click',()=>decideWebApproval(true));
webRejectButton.addEventListener('click',()=>decideWebApproval(false));

async function refreshZigbee(){
    try{
        const response=await fetch('/api/zigbee/status',{cache:'no-store'});
        if(response.status===401){loggedIn=false;showAuth(words[lang].authFailed);return;}
        if(!response.ok)throw new Error('zigbee');
        const status=await response.json();
        const coordinator=document.getElementById('coordinator');
        coordinator.textContent=status.stack_ready?words[lang].ready:words[lang].loading;
        document.getElementById('network').textContent=status.formation_timed_out
            ? words[lang].formationTimedOut
            : status.network_formed?words[lang].formed
            : status.search_pending?words[lang].formationInProgress:words[lang].notFormed;
        document.getElementById('joinWindow').textContent=status.join_seconds
            ? `${status.join_seconds} s`:words[lang].closed;
        document.getElementById('bdb').textContent=
            `0x${Number(status.last_signal_type).toString(16)} / ${status.last_bdb_status} / raw ${status.last_bdb_result} stage ${status.last_bdb_stage_name||status.last_bdb_stage} / err ${status.last_error}`;
        document.getElementById('nearbyCount').textContent=
            `${words[lang].deviceCount}: ${status.device_count}`;
        document.getElementById('nearbyEmpty').textContent=status.device_count
            ? words[lang].detailsPending:words[lang].emptyDevices;
        document.getElementById('deviceList').replaceChildren();
        document.getElementById('createNetwork').disabled=!status.available||
            !status.stack_ready||status.network_formed||status.search_pending||
            status.formation_timed_out;
        document.getElementById('openJoin').disabled=!status.network_formed||
            status.join_seconds>0||status.search_pending;
        scanNearbyButton.disabled=!status.available||!status.stack_ready||
            status.search_pending||status.formation_timed_out||status.join_seconds>0;
    }catch(_){
        document.getElementById('coordinator').textContent=words[lang].unavailable;
        scanNearbyButton.disabled=true;
    }
}

async function refreshThreadDiscovery(){
    try{
        const response=await fetch('/api/thread/status',{cache:'no-store'});
        if(response.status===401){loggedIn=false;showAuth(words[lang].authFailed);return;}
        if(!response.ok)throw new Error('thread');
        const status=await response.json();
        threadScanButton.disabled=status.scan_running;
        threadScanStatus.textContent=status.scan_running?words[lang].threadSearching
            :status.last_error!==0?`${words[lang].threadScanUnavailable} ${status.last_error}`
            :status.border_agent_count===0?words[lang].threadNone
            :`${words[lang].threadFound} ${status.border_agent_count} (not Thread devices)`;
    }catch(_){
        threadScanButton.disabled=true;
        threadScanStatus.textContent=words[lang].unavailable;
    }
}

async function scanThreadBorderRouters(){
    if(!loggedIn||threadScanButton.disabled)return;
    threadScanButton.disabled=true;
    threadScanStatus.textContent=words[lang].threadSearching;
    try{
        const response=await fetch('/api/thread/scan',{method:'POST',cache:'no-store'});
        if(response.status===401){loggedIn=false;showAuth(words[lang].authFailed);return;}
        if(!response.ok)throw new Error('thread scan');
        for(let attempt=0;attempt<6;attempt++){
            await new Promise(resolve=>setTimeout(resolve,500));
            await refreshThreadDiscovery();
            if(!threadScanButton.disabled)break;
        }
    }catch(_){
        threadScanButton.disabled=false;
        threadScanStatus.textContent=words[lang].requestFailed;
    }
}
threadScanButton.addEventListener('click',scanThreadBorderRouters);

refreshAll=async function(){
    await Promise.all([refresh(),refreshZigbee(),refreshThreadDiscovery()]);
    await refreshRequest();
};
async function refreshDetailedStatus(refreshWifi){
    if(!loggedIn||mustChange||c6RefreshBusy)return;
    c6RefreshBusy=true;manualRefreshButton.disabled=true;
    try{
        const requests=[refreshC6()];
        if(refreshWifi)requests.push(refresh());
        await Promise.all(requests);
        await refreshZigbee();
        await refreshRequest();
    }finally{
        c6RefreshBusy=false;manualRefreshButton.disabled=false;
    }
}
manualRefreshButton.addEventListener('click',()=>refreshDetailedStatus(false));
setLanguage();
void authCheck();
</script>)HTML";

#if CONFIG_HUB_WEB_DEVELOPMENT_MODE
const char kDevelopmentModeScript[] = R"HTML(<script>
document.getElementById('username').value='admin';
document.getElementById('password').value='admin';
words.en.loginHint='Development mode: admin/admin. Do not use this build in production.';
words.pl.loginHint='Tryb developerski: admin/admin. Nie uzywac w produkcji.';
setLanguage();
</script>)HTML";
#endif

const char* state_name(hub::wifi::ProvisioningState state) noexcept {
    switch (state) {
        case hub::wifi::ProvisioningState::Starting: return "starting";
        case hub::wifi::ProvisioningState::Scanning: return "scanning";
        case hub::wifi::ProvisioningState::NetworksReady: return "networks_ready";
        case hub::wifi::ProvisioningState::Connecting: return "connecting";
        case hub::wifi::ProvisioningState::Connected: return "connected";
        case hub::wifi::ProvisioningState::Failed: return "failed";
    }
    return "unknown";
}

esp_err_t http_root_redirect_handler(httpd_req_t* request) {
    char host[128]{};
    if (httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "host header required");
    }
    for (const char character : host) {
        if (character == '\0') {
            break;
        }
        const bool valid = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') ||
                           character == '.' || character == '-' ||
                           character == ':' || character == '[' || character == ']';
        if (!valid) {
            httpd_resp_set_status(request, "400 Bad Request");
            return httpd_resp_sendstr(request, "invalid host header");
        }
    }
    char location[160]{};
    const int length = std::snprintf(location, sizeof(location), "https://%s/", host);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(location)) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "invalid host header");
    }
    httpd_resp_set_status(request, "308 Permanent Redirect");
    httpd_resp_set_hdr(request, "Location", location);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "HTTPS required");
}

bool read_session_cookie(httpd_req_t* request, char* token,
                         std::size_t capacity) {
    constexpr char kCookieName[] = "p4hub_session=";
    constexpr std::size_t kTokenLength = 32;
    const std::size_t header_length = httpd_req_get_hdr_value_len(request, "Cookie");
    if (token == nullptr || capacity < kTokenLength + 1 ||
        header_length == 0 || header_length >= 256) {
        return false;
    }
    char cookie_header[256]{};
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie_header,
                                    sizeof(cookie_header)) != ESP_OK) {
        return false;
    }
    char* value = std::strstr(cookie_header, kCookieName);
    if (value == nullptr ||
        (value != cookie_header && value[-1] != ';' && value[-1] != ' ')) {
        return false;
    }
    value += sizeof(kCookieName) - 1;
    for (std::size_t index = 0; index < kTokenLength; ++index) {
        const char character = value[index];
        const bool hexadecimal = (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');
        if (!hexadecimal) {
            return false;
        }
        token[index] = character;
    }
    if (value[kTokenLength] != '\0' && value[kTokenLength] != ';' &&
        value[kTokenLength] != ' ') {
        return false;
    }
    token[kTokenLength] = '\0';
    return true;
}

bool authenticate_request(httpd_req_t* request,
                          bool* password_authenticated = nullptr) {
    if (password_authenticated != nullptr) {
        *password_authenticated = false;
    }
    if (web_security == nullptr) {
        return false;
    }
    char session_token[33]{};
    if (read_session_cookie(request, session_token, sizeof(session_token)) &&
        web_security->verify_session(session_token, sizeof(session_token) - 1)) {
        std::memset(session_token, 0, sizeof(session_token));
        return true;
    }
    std::memset(session_token, 0, sizeof(session_token));

    const std::size_t header_length = httpd_req_get_hdr_value_len(request, "Authorization");
    if (header_length < 7 || header_length >= 256) {
        return false;
    }
    char header[256]{};
    if (httpd_req_get_hdr_value_str(request, "Authorization", header,
                                    sizeof(header)) != ESP_OK ||
        std::strncmp(header, "Basic ", 6) != 0) {
        return false;
    }
    unsigned char decoded[160]{};
    std::size_t decoded_length = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_length,
                              reinterpret_cast<const unsigned char*>(header + 6),
                              header_length - 6) != 0) {
        return false;
    }
    const auto* separator = static_cast<const unsigned char*>(
        std::memchr(decoded, ':', decoded_length));
    if (separator == nullptr) {
        return false;
    }
    const std::size_t username_length = static_cast<std::size_t>(separator - decoded);
    const std::size_t password_length = decoded_length - username_length - 1;
    const char* password = reinterpret_cast<const char*>(separator + 1);
    const bool authorized = web_security->verify_password(
        reinterpret_cast<const char*>(decoded), username_length,
        password, password_length);
    std::memset(decoded, 0, sizeof(decoded));
    if (authorized && password_authenticated != nullptr) {
        *password_authenticated = true;
    }
    return authorized;
}

esp_err_t send_unauthorized(httpd_req_t* request) {
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "authentication required");
}

esp_err_t issue_session_cookie(httpd_req_t* request) {
    char session_token[33]{};
    const esp_err_t status = web_security->create_session(
        session_token, sizeof(session_token));
    if (status != ESP_OK) {
        return status;
    }
    std::memset(session_cookie_response, 0, sizeof(session_cookie_response));
    const int cookie_length = std::snprintf(
        session_cookie_response, sizeof(session_cookie_response),
        "p4hub_session=%s; Path=/; Max-Age=600; Secure; HttpOnly; SameSite=Strict",
        session_token);
    std::memset(session_token, 0, sizeof(session_token));
    if (cookie_length < 0 || static_cast<std::size_t>(cookie_length) >=
        sizeof(session_cookie_response)) {
        web_security->revoke_session();
        return ESP_FAIL;
    }
    const esp_err_t header_status = httpd_resp_set_hdr(
        request, "Set-Cookie", session_cookie_response);
    if (header_status != ESP_OK) {
        web_security->revoke_session();
    }
    return header_status;
}

esp_err_t require_authenticated_admin(httpd_req_t* request,
                                      bool allow_password_change,
                                      bool* password_authenticated = nullptr) {
    bool authenticated_by_password = false;
    if (!authenticate_request(request, &authenticated_by_password)) {
        return send_unauthorized(request);
    }
    if (authenticated_by_password && issue_session_cookie(request) != ESP_OK) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "session creation failed");
    }
    if (password_authenticated != nullptr) {
        *password_authenticated = authenticated_by_password;
    }
    if (!allow_password_change && web_security->password_change_required()) {
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request, "change initial password first");
    }
    return ESP_OK;
}

bool request_origin_is_local_https(httpd_req_t* request) {
    char host[128]{};
    char origin[160]{};
    if (httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK ||
        httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK) {
        return false;
    }
    char expected[160]{};
    const int length = std::snprintf(expected, sizeof(expected), "https://%s", host);
    return length > 0 && static_cast<std::size_t>(length) < sizeof(expected) &&
           std::strcmp(origin, expected) == 0;
}

esp_err_t read_request_body(httpd_req_t* request, char* output,
                            std::size_t capacity, std::size_t* length) {
    if (output == nullptr || capacity == 0 || length == nullptr ||
        request->content_len == 0 ||
        request->content_len >= static_cast<int>(capacity)) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::size_t received = 0;
    const std::size_t required = static_cast<std::size_t>(request->content_len);
    while (received < required) {
        const int result = httpd_req_recv(request, output + received, required - received);
        if (result <= 0) {
            return ESP_FAIL;
        }
        received += static_cast<std::size_t>(result);
    }
    output[received] = '\0';
    *length = received;
    return ESP_OK;
}

const char* request_state_name(hub::diagnostics::WebZigbeeRequestState state) {
    using hub::diagnostics::WebZigbeeRequestState;
    switch (state) {
        case WebZigbeeRequestState::idle: return "idle";
        case WebZigbeeRequestState::awaiting_touch: return "awaiting_touch";
        case WebZigbeeRequestState::approved: return "approved";
        case WebZigbeeRequestState::executing: return "executing";
        case WebZigbeeRequestState::completed: return "completed";
        case WebZigbeeRequestState::rejected: return "rejected";
        case WebZigbeeRequestState::expired: return "expired";
        case WebZigbeeRequestState::failed: return "failed";
    }
    return "unknown";
}

const char* request_action_name(hub::diagnostics::WebZigbeeAction action) {
    using hub::diagnostics::WebZigbeeAction;
    switch (action) {
        case WebZigbeeAction::create_network: return "create_network";
        case WebZigbeeAction::open_join_window: return "open_join_window";
        case WebZigbeeAction::scan_nearby: return "scan_nearby";
        case WebZigbeeAction::none: return "none";
    }
    return "none";
}

const char* bdb_stage_name(std::uint8_t stage) noexcept {
    switch (stage) {
        case 1: return "initialization";
        case 2: return "formation_start";
        case 3: return "formation_signal";
        case 4: return "close_network";
        case 5: return "open_join_window";
        case 6: return "watchdog_timeout";
        default: return "none";
    }
}

void thread_border_agent_scan_task(void* context) {
    (void)context;
    mdns_result_t* results = nullptr;
    const esp_err_t status = mdns_query_ptr(
        "_meshcop", "_udp", 2000, 16, &results);
    std::uint8_t count = 0;
    if (status == ESP_OK) {
        for (const mdns_result_t* result = results;
             result != nullptr && count < 16; result = result->next) {
            ++count;
        }
    }
    if (results != nullptr) {
        mdns_query_results_free(results);
    }
    portENTER_CRITICAL(&thread_scan_mux);
    thread_border_agent_count = count;
    thread_scan_last_error = status;
    thread_scan_running = false;
    portEXIT_CRITICAL(&thread_scan_mux);
    ESP_LOGI(kTag, "Thread Border Agent DNS-SD scan completed: status=%s count=%u",
             esp_err_to_name(status), static_cast<unsigned>(count));
    vTaskDelete(nullptr);
}

esp_err_t root_handler(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t status = httpd_resp_send_chunk(request, kDiagnosticsPage,
                                             HTTPD_RESP_USE_STRLEN);
    if (status == ESP_OK) {
        status = httpd_resp_send_chunk(request, kSessionLifecycleScript,
                                       HTTPD_RESP_USE_STRLEN);
    }
#if CONFIG_HUB_WEB_DEVELOPMENT_MODE
    if (status == ESP_OK) {
        status = httpd_resp_send_chunk(request, kDevelopmentModeScript,
                                       HTTPD_RESP_USE_STRLEN);
    }
#endif
    if (status == ESP_OK) {
        status = httpd_resp_send_chunk(request, nullptr, 0);
    }
    return status;
}

esp_err_t auth_status_handler(httpd_req_t* request) {
    const esp_err_t auth_status = require_authenticated_admin(request, true);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    char response[64]{};
    const int length = std::snprintf(response, sizeof(response),
                                     "{\"must_change_password\":%s}",
                                     web_security->password_change_required()
                                         ? "true" : "false");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

esp_err_t password_change_handler(httpd_req_t* request) {
    const esp_err_t auth_status = require_authenticated_admin(request, true);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    if (!request_origin_is_local_https(request)) {
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request, "same-origin HTTPS request required");
    }
    char password[65]{};
    std::size_t password_length = 0;
    const esp_err_t body_status = read_request_body(
        request, password, sizeof(password), &password_length);
    if (body_status != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "invalid password body");
    }
    const esp_err_t status = web_security->set_password(password, password_length);
    std::memset(password, 0, sizeof(password));
    if (status != ESP_OK) {
        httpd_resp_set_status(request, status == ESP_ERR_INVALID_SIZE
                                          ? "400 Bad Request"
                                          : "500 Internal Server Error");
        return httpd_resp_sendstr(request, esp_err_to_name(status));
    }
    httpd_resp_set_hdr(request, "Set-Cookie",
                       "p4hub_session=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Strict");
    return httpd_resp_sendstr(request, "password changed");
}

esp_err_t auth_logout_handler(httpd_req_t* request) {
    const esp_err_t auth_status = require_authenticated_admin(request, true);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    if (!request_origin_is_local_https(request)) {
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request, "same-origin HTTPS request required");
    }
    web_security->revoke_session();
    httpd_resp_set_hdr(request, "Set-Cookie",
                       "p4hub_session=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Strict");
    return httpd_resp_sendstr(request, "signed out");
}

esp_err_t zigbee_request_status_handler(httpd_req_t* request) {
    const esp_err_t auth_status = require_authenticated_admin(request, true);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    hub::diagnostics::WebZigbeeRequestSnapshot snapshot{};
    if (web_zigbee_approval == nullptr ||
        !web_zigbee_approval->read_snapshot(&snapshot)) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "approval status unavailable");
    }
    char response[160]{};
    const int length = std::snprintf(
        response, sizeof(response),
        "{\"id\":%lu,\"action\":\"%s\",\"state\":\"%s\",\"result\":%ld}",
        static_cast<unsigned long>(snapshot.id), request_action_name(snapshot.action),
        request_state_name(snapshot.state), static_cast<long>(snapshot.result));
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

esp_err_t zigbee_status_handler(httpd_req_t* request) {
    const esp_err_t auth_status = require_authenticated_admin(request, true);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    hub::radio::ZigbeeCoordinatorSnapshot snapshot{};
    if (zigbee_coordinator == nullptr ||
        !zigbee_coordinator->read_snapshot(&snapshot) || !snapshot.available) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "Zigbee status unavailable");
    }
    char response[384]{};
    const int length = std::snprintf(
        response, sizeof(response),
        "{\"available\":true,\"stack_ready\":%s,\"network_formed\":%s,"
        "\"search_pending\":%s,\"formation_timed_out\":%s,"
        "\"device_count\":%u,\"join_seconds\":%u,\"last_error\":%ld,"
        "\"last_signal_type\":%u,\"last_bdb_status\":%u,"
        "\"last_bdb_result\":%ld,\"last_bdb_stage\":%u,"
        "\"last_bdb_stage_name\":\"%s\"}",
        snapshot.stack_ready ? "true" : "false",
        snapshot.network_formed ? "true" : "false",
        snapshot.search_pending ? "true" : "false",
        snapshot.formation_timed_out ? "true" : "false",
        static_cast<unsigned>(snapshot.device_count),
        static_cast<unsigned>(snapshot.pairing_seconds_remaining),
        static_cast<long>(snapshot.last_error),
        static_cast<unsigned>(snapshot.last_signal_type),
        static_cast<unsigned>(snapshot.last_bdb_status),
        static_cast<long>(snapshot.last_bdb_result),
        static_cast<unsigned>(snapshot.last_bdb_stage),
        bdb_stage_name(snapshot.last_bdb_stage));
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(response)) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "Zigbee status formatting failed");
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

esp_err_t thread_scan_status_handler(httpd_req_t* request) {
    const esp_err_t auth_status = require_authenticated_admin(request, true);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    bool running = false;
    std::uint8_t border_agent_count = 0;
    esp_err_t last_error = ESP_OK;
    portENTER_CRITICAL(&thread_scan_mux);
    running = thread_scan_running;
    border_agent_count = thread_border_agent_count;
    last_error = thread_scan_last_error;
    portEXIT_CRITICAL(&thread_scan_mux);

    char response[160]{};
    const int length = std::snprintf(
        response, sizeof(response),
        "{\"scope\":\"border_agent_advertisements_only\","
        "\"scan_running\":%s,\"border_agent_count\":%u,\"last_error\":%ld}",
        running ? "true" : "false",
        static_cast<unsigned>(border_agent_count),
        static_cast<long>(last_error));
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(response)) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "Thread status formatting failed");
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

esp_err_t thread_scan_handler(httpd_req_t* request) {
    const esp_err_t auth_status = require_authenticated_admin(request, false);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    if (!request_origin_is_local_https(request)) {
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request, "same-origin HTTPS request required");
    }
    portENTER_CRITICAL(&thread_scan_mux);
    if (thread_scan_running) {
        portEXIT_CRITICAL(&thread_scan_mux);
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Thread discovery is already running");
    }
    thread_scan_running = true;
    thread_border_agent_count = 0;
    thread_scan_last_error = ESP_OK;
    portEXIT_CRITICAL(&thread_scan_mux);

    if (xTaskCreate(thread_border_agent_scan_task, "thread_mdns_scan", 4096,
                    nullptr, 3, nullptr) != pdPASS) {
        portENTER_CRITICAL(&thread_scan_mux);
        thread_scan_running = false;
        thread_scan_last_error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&thread_scan_mux);
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "Thread discovery task unavailable");
    }
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request,
        "{\"accepted\":true,\"scope\":\"border_agent_advertisements_only\"}");
}

esp_err_t decide_zigbee_web_request(httpd_req_t* request, bool approve) {
    const esp_err_t auth_status = require_authenticated_admin(request, false);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    if (!request_origin_is_local_https(request)) {
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request, "same-origin HTTPS request required");
    }
    if (web_zigbee_approval == nullptr) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "approval service unavailable");
    }

    char body[16]{};
    std::size_t body_length = 0;
    if (read_request_body(request, body, sizeof(body), &body_length) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "request id required");
    }
    std::uint32_t request_id = 0;
    bool valid_id = body_length > 0;
    for (std::size_t index = 0; index < body_length && valid_id; ++index) {
        if (body[index] < '0' || body[index] > '9') {
            valid_id = false;
            break;
        }
        const std::uint32_t digit =
            static_cast<std::uint32_t>(body[index] - '0');
        if (request_id >
            (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
            valid_id = false;
            break;
        }
        request_id = request_id * 10U + digit;
    }
    if (!valid_id || request_id == 0) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "invalid request id");
    }

    const esp_err_t result = approve
        ? web_zigbee_approval->approve(request_id)
        : web_zigbee_approval->reject(request_id);
    if (result != ESP_OK) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "request is stale or no longer pending");
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (approve) {
        httpd_resp_set_status(request, "202 Accepted");
        return httpd_resp_sendstr(request, "{\"state\":\"approved\"}");
    }
    return httpd_resp_sendstr(request, "{\"state\":\"rejected\"}");
}

esp_err_t zigbee_approve_handler(httpd_req_t* request) {
    return decide_zigbee_web_request(request, true);
}

esp_err_t zigbee_reject_handler(httpd_req_t* request) {
    return decide_zigbee_web_request(request, false);
}

esp_err_t submit_zigbee_web_request(httpd_req_t* request,
                                    hub::diagnostics::WebZigbeeAction action) {
    const esp_err_t auth_status = require_authenticated_admin(request, false);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    if (!request_origin_is_local_https(request)) {
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request, "same-origin HTTPS request required");
    }
    hub::radio::ZigbeeCoordinatorSnapshot zigbee_snapshot{};
    if (zigbee_coordinator == nullptr ||
        !zigbee_coordinator->read_snapshot(&zigbee_snapshot) ||
        !zigbee_snapshot.available || !zigbee_snapshot.stack_ready) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "Zigbee coordinator unavailable");
    }
    if (zigbee_snapshot.search_pending ||
        (action == hub::diagnostics::WebZigbeeAction::create_network &&
                 (zigbee_snapshot.network_formed ||
                    zigbee_snapshot.formation_timed_out)) ||
        (action == hub::diagnostics::WebZigbeeAction::open_join_window &&
         (!zigbee_snapshot.network_formed ||
                    zigbee_snapshot.pairing_seconds_remaining > 0)) ||
                (action == hub::diagnostics::WebZigbeeAction::scan_nearby &&
                 (zigbee_snapshot.formation_timed_out ||
                    zigbee_snapshot.pairing_seconds_remaining > 0))) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Zigbee action unavailable in current state");
    }
    std::uint32_t request_id = 0;
    const esp_err_t status = web_zigbee_approval->request(action, &request_id);
    if (status != ESP_OK) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "another request is awaiting confirmation");
    }
    char response[96]{};
    const int length = std::snprintf(
        response, sizeof(response),
        "{\"accepted\":true,\"id\":%lu,\"state\":\"awaiting_touch\"}",
        static_cast<unsigned long>(request_id));
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

esp_err_t zigbee_create_handler(httpd_req_t* request) {
    return submit_zigbee_web_request(
        request, hub::diagnostics::WebZigbeeAction::create_network);
}

esp_err_t zigbee_join_handler(httpd_req_t* request) {
    return submit_zigbee_web_request(
        request, hub::diagnostics::WebZigbeeAction::open_join_window);
}

esp_err_t zigbee_scan_handler(httpd_req_t* request) {
    return submit_zigbee_web_request(
        request, hub::diagnostics::WebZigbeeAction::scan_nearby);
}

esp_err_t status_handler(httpd_req_t* request) {
    hub::wifi::ProvisioningUpdate snapshot{};
    hub::radio::MultipanSnapshot radio_snapshot{};
    if (provisioning_service == nullptr || !provisioning_service->read_snapshot(&snapshot)) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "status unavailable");
    }
    if (multipan_service == nullptr || !multipan_service->read_snapshot(&radio_snapshot)) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "radio status unavailable");
    }

    char response[512]{};
    const auto system = hub::diagnostics::read_system_snapshot();
    const int response_length = std::snprintf(
        response, sizeof(response),
        "{\"state\":\"%s\",\"ipv4\":\"%s\",\"credential_store_ready\":%s,"
        "\"credentials_saved\":%s,\"rssi_valid\":%s,\"rssi_dbm\":%d,\"flash_encryption\":%s,"
        "\"psram_free_kb\":%u,\"internal_heap_free_kb\":%u,"
        "\"multipan_state\":\"%s\",\"thread_role\":\"%s\","
        "\"tbr_ready\":%s,\"zigbee_started\":%s,"
        "\"zigbee_network_formed\":%s,\"zigbee_network_start_requested\":%s,"
        "\"zigbee_device_count\":%u,"
        "\"pairing_seconds_remaining\":%u}",
        state_name(snapshot.state), snapshot.ipv4_address,
        snapshot.credential_store_ready ? "true" : "false",
        snapshot.credentials_saved ? "true" : "false",
        snapshot.rssi_valid ? "true" : "false",
        static_cast<int>(snapshot.rssi_dbm),
        system.flash_encryption_enabled ? "true" : "false",
        static_cast<unsigned>(system.psram_free_kb),
        static_cast<unsigned>(system.internal_heap_free_kb),
        multipan_state_name(radio_snapshot.state),
        thread_role_name(radio_snapshot.thread_role),
        radio_snapshot.thread_border_router_ready ? "true" : "false",
        radio_snapshot.zigbee_started ? "true" : "false",
        radio_snapshot.zigbee_network_formed ? "true" : "false",
        radio_snapshot.network_start_requested ? "true" : "false",
        static_cast<unsigned>(radio_snapshot.zigbee_device_count),
        static_cast<unsigned>(radio_snapshot.pairing_seconds_remaining));
    if (response_length < 0 || static_cast<std::size_t>(response_length) >= sizeof(response)) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "status formatting failed");
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, response_length);
}

esp_err_t c6_status_handler(httpd_req_t* request) {
    const esp_err_t auth_status = require_authenticated_admin(request, true);
    if (auth_status != ESP_OK) {
        return auth_status;
    }
    hub::C6DiagnosticsSnapshot snapshot{};
    hub::radio::ZigbeeCoordinatorSnapshot zigbee_snapshot{};
    const esp_err_t status = hub::read_c6_diagnostics(&snapshot);
    if (status != ESP_OK) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "C6 diagnostics unavailable");
    }
    if (zigbee_coordinator != nullptr) {
        (void)zigbee_coordinator->read_snapshot(&zigbee_snapshot);
    }

    char version[32]{};
    char staged_app_version[32]{};
    if (snapshot.version_valid) {
        std::snprintf(version, sizeof(version), "%lu.%lu.%lu",
                      static_cast<unsigned long>(snapshot.version_major),
                      static_cast<unsigned long>(snapshot.version_minor),
                      static_cast<unsigned long>(snapshot.version_patch));
    }
    if (snapshot.staged_image_valid) {
        std::size_t output_index = 0;
        for (const char character : snapshot.staged_app_version) {
            if (character == '\0' || output_index + 1 >= sizeof(staged_app_version)) {
                break;
            }
            const bool valid = (character >= 'a' && character <= 'z') ||
                               (character >= 'A' && character <= 'Z') ||
                               (character >= '0' && character <= '9') ||
                               character == '.' || character == '_' || character == '-';
            staged_app_version[output_index++] = valid ? character : '_';
        }
    }
    char image_sha256[65]{};
    if (snapshot.staged_image_hash_valid) {
        for (std::size_t index = 0; index < snapshot.staged_image_sha256.size(); ++index) {
            std::snprintf(image_sha256 + index * 2, 3, "%02x",
                          static_cast<unsigned>(snapshot.staged_image_sha256[index]));
        }
    }
    char response[768]{};
    const int response_length = std::snprintf(
        response, sizeof(response),
        "{\"version_valid\":%s,\"version\":\"%s\",\"version_error\":%ld,"
        "\"chip_id_valid\":%s,\"chip_id\":%lu,"
        "\"staging_partition_found\":%s,\"staging_partition_bytes\":%lu,"
        "\"staged_image_valid\":%s,\"staged_image_bytes\":%lu,"
        "\"staged_app_version\":\"%s\",\"staged_image_sha256\":\"%s\","
        "\"staged_image_error\":%ld,"
        "\"ota_in_progress\":%s,\"ota_state\":\"%s\","
        "\"ota_attempts\":%lu,\"ota_last_error\":%ld,"
        "\"zigbee_status_available\":%s,\"zigbee_stack_ready\":%s,"
        "\"zigbee_network_formed\":%s,\"zigbee_search_pending\":%s,"
        "\"zigbee_formation_timed_out\":%s,"
        "\"zigbee_device_count\":%u,\"zigbee_join_seconds\":%u,"
        "\"zigbee_last_error\":%ld,\"zigbee_last_signal_type\":%u,"
        "\"zigbee_last_bdb_status\":%u,\"zigbee_last_bdb_result\":%ld,"
        "\"zigbee_last_bdb_stage\":%u,\"zigbee_last_bdb_stage_name\":\"%s\"}",
        snapshot.version_valid ? "true" : "false", version,
        static_cast<long>(snapshot.version_error),
        snapshot.cp_info_valid ? "true" : "false",
        static_cast<unsigned long>(snapshot.chip_id),
        snapshot.staging_partition_found ? "true" : "false",
        static_cast<unsigned long>(snapshot.staging_partition_bytes),
        snapshot.staged_image_valid ? "true" : "false",
        static_cast<unsigned long>(snapshot.staged_image_bytes),
        staged_app_version,
        image_sha256,
        static_cast<long>(snapshot.staged_image_error),
        snapshot.ota_in_progress ? "true" : "false",
        c6_audit_state_name(snapshot.audit_state),
        static_cast<unsigned long>(snapshot.ota_attempts),
        static_cast<long>(snapshot.ota_last_error),
        zigbee_snapshot.available ? "true" : "false",
        zigbee_snapshot.stack_ready ? "true" : "false",
        zigbee_snapshot.network_formed ? "true" : "false",
        zigbee_snapshot.search_pending ? "true" : "false",
        zigbee_snapshot.formation_timed_out ? "true" : "false",
        static_cast<unsigned>(zigbee_snapshot.device_count),
        static_cast<unsigned>(zigbee_snapshot.pairing_seconds_remaining),
        static_cast<long>(zigbee_snapshot.last_error),
        static_cast<unsigned>(zigbee_snapshot.last_signal_type),
        static_cast<unsigned>(zigbee_snapshot.last_bdb_status),
        static_cast<long>(zigbee_snapshot.last_bdb_result),
        static_cast<unsigned>(zigbee_snapshot.last_bdb_stage),
        bdb_stage_name(zigbee_snapshot.last_bdb_stage));
    if (response_length < 0 || static_cast<std::size_t>(response_length) >= sizeof(response)) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "C6 status formatting failed");
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, response_length);
}

}

namespace hub::diagnostics {

esp_err_t start_http(wifi::Provisioning& provisioning,
                     radio::MultipanService& multipan,
                     radio::IZigbeeCoordinator& zigbee,
                     WebSecurity& security,
                     WebZigbeeApproval& approval) noexcept {
    if (http_server_handle != nullptr || https_server_handle != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    provisioning_service = &provisioning;
    multipan_service = &multipan;
    zigbee_coordinator = &zigbee;
    web_security = &security;
    web_zigbee_approval = &approval;

    esp_err_t status = security.start();
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "Web security initialization failed: %s", esp_err_to_name(status));
        return status;
    }

    std::size_t certificate_length = 0;
    std::size_t private_key_length = 0;
    const std::uint8_t* certificate = security.certificate(&certificate_length);
    const std::uint8_t* private_key = security.private_key(&private_key_length);
    if (certificate == nullptr || private_key == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ssl_config_t https_config = HTTPD_SSL_CONFIG_DEFAULT();
    https_config.httpd.max_open_sockets = 3;
    https_config.httpd.max_uri_handlers = 15;
    https_config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    https_config.servercert = certificate;
    https_config.servercert_len = certificate_length;
    https_config.prvtkey_pem = private_key;
    https_config.prvtkey_len = private_key_length;
    status = httpd_ssl_start(&https_server_handle, &https_config);
    if (status != ESP_OK) {
        https_server_handle = nullptr;
        ESP_LOGE(kTag, "HTTPS server start failed: %s", esp_err_to_name(status));
        return status;
    }

    const auto register_route = [](httpd_handle_t server, const char* uri,
                                   httpd_method_t method,
                                   esp_err_t (*handler)(httpd_req_t*)) {
        const httpd_uri_t route = {
            .uri = uri,
            .method = method,
            .handler = handler,
            .user_ctx = nullptr,
        };
        return httpd_register_uri_handler(server, &route);
    };

    status = register_route(https_server_handle, "/", HTTP_GET, root_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/status", HTTP_GET, status_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/c6/status", HTTP_GET, c6_status_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/auth/status", HTTP_GET, auth_status_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/auth/password", HTTP_POST, password_change_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/auth/logout", HTTP_POST, auth_logout_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/zigbee/request", HTTP_GET, zigbee_request_status_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/zigbee/create", HTTP_POST, zigbee_create_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/zigbee/join", HTTP_POST, zigbee_join_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/zigbee/status", HTTP_GET, zigbee_status_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/zigbee/approve", HTTP_POST, zigbee_approve_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/zigbee/reject", HTTP_POST, zigbee_reject_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/zigbee/scan", HTTP_POST, zigbee_scan_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/thread/status", HTTP_GET, thread_scan_status_handler);
    if (status == ESP_OK) status = register_route(https_server_handle, "/api/thread/scan", HTTP_POST, thread_scan_handler);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "HTTPS route registration failed: %s", esp_err_to_name(status));
        (void)httpd_ssl_stop(https_server_handle);
        https_server_handle = nullptr;
        return status;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    status = httpd_start(&http_server_handle, &config);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "HTTP server start failed: %s", esp_err_to_name(status));
        http_server_handle = nullptr;
        (void)httpd_ssl_stop(https_server_handle);
        https_server_handle = nullptr;
        return status;
    }

    status = register_route(http_server_handle, "/", HTTP_GET,
                            http_root_redirect_handler);
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "HTTP route registration failed: %s", esp_err_to_name(status));
        (void)httpd_stop(http_server_handle);
        http_server_handle = nullptr;
        (void)httpd_ssl_stop(https_server_handle);
        https_server_handle = nullptr;
        return status;
    }

    ESP_LOGI(kTag, "HTTPS Web UI listening on port 443; HTTP redirects to HTTPS");
    return ESP_OK;
}

}