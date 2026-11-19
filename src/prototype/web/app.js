'use strict';
const $ = (id) => document.getElementById(id);
const token = document.querySelector('meta[name="csrf-token"]').content;
let busy = false;
let approval = null;
let assistant = null;
let assistantText = '';
let approvalBusy = false;

function log(message) {
  const events = $('events');
  if (events.querySelector('.muted')) events.replaceChildren();
  const div = document.createElement('div');
  div.className = 'event';
  div.textContent = `${new Date().toLocaleTimeString()}  ${message}`;
  events.prepend(div);
  while (events.childElementCount > 35) events.lastElementChild.remove();
}

function bubble(role, body) {
  const node = document.createElement('div');
  node.className = 'message ' + role;
  const heading = document.createElement('div');
  heading.className = 'author';
  heading.textContent = role === 'user' ? 'Du' : 'Assistent';
  const text = document.createElement('div');
  text.className = 'body';
  text.textContent = body;
  node.append(heading, text);
  $('messages').append(node);
  $('messages').scrollTop = $('messages').scrollHeight;
  return text;
}

function toolMark(label) {
  const line = document.createElement('div');
  line.className = 'toolmark';
  line.textContent = '⚙ ' + label;
  $('messages').append(line);
  $('messages').scrollTop = $('messages').scrollHeight;
}

function setBusy(value) {
  busy = value;
  $('send').disabled = value;
  $('prompt').disabled = value;
  $('cancel').disabled = !value;
  $('reset').disabled = value;
  $('refresh').disabled = value;
  $('suggestions').querySelectorAll('button').forEach(b => b.disabled = value);
}

function snapshot(snapshot) {
  if (!snapshot) return;
  $('mode').textContent = snapshot.mode === 'mock' ? '● Offline Mock' : '● OpenAI live';
  const docs = $('documents');
  docs.replaceChildren();
  for (const doc of snapshot.documents ?? []) {
    const row = document.createElement('div'); row.className = 'doc';
    const id = document.createElement('span'); id.className='docid'; id.textContent = `#${doc.id}`;
    const detail = document.createElement('div');
    const title = document.createElement('div'); title.className='docname'; title.textContent = doc.title;
    const kind = document.createElement('div'); kind.className='doccategory'; kind.textContent = doc.category;
    detail.append(title, kind); row.append(id, detail); docs.append(row);
  }
  const types = $('reflection'); types.replaceChildren();
  const title = document.createElement('h3'); title.textContent='Registrierte Typen'; types.append(title);
  for (const type of snapshot.types ?? []) {
    const div = document.createElement('div'); div.className='info';
    div.textContent = `${type.type} · Felder: ${type.fields.length} · Methoden: ${type.methods.length}`;
    types.append(div);
  }
  const tools = document.createElement('h3'); tools.textContent='Explizit freigegebene Tools'; types.append(tools);
  for (const tool of snapshot.tools ?? []) {
    const row = document.createElement('div'); row.className='toolrow';
    const name = document.createElement('span'); name.textContent = tool.name;
    const access = document.createElement('span'); access.className = 'tag' + (tool.access === 'write' ? ' write' : '');
    access.textContent = tool.access === 'write' ? 'Bestätigung' : 'Lesen';
    row.append(name, access); types.append(row);
  }
  if (snapshot.failed) log('Chat-Sitzung fehlgeschlagen; Neustart des Chats erforderlich');
}

async function api(action) {
  const res = await fetch('/api/action', {
    method: 'POST',
    headers: {'Content-Type':'application/json', 'X-Local-Token':token},
    body: JSON.stringify(action),
  });
  if (!res.ok || !res.body) throw new Error(`HTTP ${res.status}`);
  const reader = res.body.getReader();
  const decoder = new TextDecoder();
  let remainder = '';
  let terminal = false;
  try {
    while (true) {
      const {value, done} = await reader.read();
      if (done) break;
      remainder += decoder.decode(value, {stream:true});
      let split;
      while ((split = remainder.indexOf('\n')) >= 0) {
        const line = remainder.slice(0, split); remainder = remainder.slice(split+1);
        if (!line) continue;
        const event = JSON.parse(line);
        renderEvent(event);
        if (['complete', 'approval', 'cancelled', 'error'].includes(event.type)) terminal = true;
      }
    }
    if (!terminal) throw new Error('Verbindung ohne Abschlussereignis beendet');
  } finally { reader.releaseLock(); }
}

function renderEvent(event) {
  if (event.snapshot) snapshot(event.snapshot);
  if (event.type === 'model_request') {
    log(`LLM-Anfrage · ${event.available_tools} verfügbare Tools`);
  } else if (event.type === 'tool_requested') {
    toolMark(`LLM → ${event.name} ${event.arguments_json}`);
    log(`Tool angefordert: ${event.name} ${event.arguments_json}`);
  } else if (event.type === 'delta') {
    if (!assistant) assistant = bubble('assistant', '');
    assistantText += event.text;
    assistant.textContent = assistantText;
  } else if (event.type === 'tool') {
    const text = `${event.name} · ${event.code}`;
    toolMark(text);
    log('Tool: ' + text);
  } else if (event.type === 'approval') {
    approval = event;
    log('Bestätigung benötigt: ' + event.tool_name);
    $('tool-name').textContent = event.tool_name;
    $('tool-arguments').textContent = JSON.stringify(event.arguments, null, 2);
    $('overlay').classList.remove('hidden');
    $('approve').focus();
  } else if (event.type === 'complete') {
    if (event.answer !== undefined) {
      if (!assistant) assistant = bubble('assistant', event.answer);
      else assistant.textContent = event.answer || assistantText;
      log('Antwort abgeschlossen');
    } else if (event.status) log('Worker: ' + event.status);
  } else if (event.type === 'cancelled') {
    bubble('assistant', event.message ?? 'Abgebrochen.');
    log('Anfrage abgebrochen');
  } else if (event.type === 'error') {
    bubble('assistant', event.message ?? 'Unbekannter Fehler');
    log('Fehler: ' + (event.message ?? 'Unbekannt'));
  }
}

async function run(action) {
  setBusy(true);
  assistant = null; assistantText = '';
  try { await api(action); }
  catch (error) { bubble('assistant', 'Verbindungsfehler: ' + error.message); log('Verbindungsfehler'); }
  finally { setBusy(false); }
}

$('chat-form').addEventListener('submit', (e) => {
  e.preventDefault();
  if (busy || approval) return;
  const text = $('prompt').value.trim();
  if (!text) return;
  bubble('user', text);
  $('prompt').value='';
  run({action:'send',text});
});
$('prompt').addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {e.preventDefault(); $('chat-form').requestSubmit();}
});
$('suggestions').querySelectorAll('button').forEach(button => {
  button.addEventListener('click', () => {
    if (busy || approval) return;
    $('prompt').value = button.dataset.prompt;
    $('chat-form').requestSubmit();
  });
});
async function resolve(approved) {
  if (!approval || approvalBusy) return;
  approvalBusy = true;
  const approval_id = approval.approval_id;
  approval = null;
  $('overlay').classList.add('hidden');
  log(approved ? 'Benutzer bestätigt Änderung' : 'Benutzer lehnt Änderung ab');
  try { await run({action:'resume', approval_id, approved}); }
  finally { approvalBusy = false; }
}
$('approve').addEventListener('click', () => resolve(true));
$('deny').addEventListener('click', () => resolve(false));
$('refresh').addEventListener('click', async () => { if (!busy) await run({action:'snapshot'}); });
$('reset').addEventListener('click', async () => {
  if (busy || approval) {
    // Pending approval must be explicitly rejected; no hidden approval by reset.
    if (approval && !busy) await resolve(false);
    else return;
  }
  await run({action:'reset'});
  $('messages').replaceChildren();
  bubble('assistant', 'Neuer Chat gestartet. Die C++-Dokumente bleiben erhalten.');
});
$('cancel').addEventListener('click', async () => {
  if (!busy) return;
  log('Abbruch angefordert');
  // Separate request: may run concurrently with the streaming send request.
  try { await api({action:'cancel'}); }
  catch (error) { log('Abbruchfehler: ' + error.message); }
});
run({action:'snapshot'});
