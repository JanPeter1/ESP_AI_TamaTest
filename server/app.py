import io
from typing import Dict, List
from fastapi import FastAPI, UploadFile, File, Form, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, JSONResponse
from PIL import Image, ImageOps

app = FastAPI(
    title="DeskConnect Cloud Hub",
    description="Zentraler Hub zur Kopplung von ESP32 CYD Displays über verschiedene Netzwerke (Internet / WAN)"
)

# Active connected displays per room: { "room_code": { "client_id": WebSocket } }
rooms: Dict[str, Dict[str, WebSocket]] = {}

HTML_PAGE = """<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>DeskConnect - Weltweites Display Portal</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #12121a; color: #fff; padding: 20px; max-width: 480px; margin: auto; }
        h1 { font-size: 1.4rem; color: #ffcc00; text-align: center; }
        .subtitle { font-size: 0.85rem; color: #888; text-align: center; margin-top: -10px; margin-bottom: 20px; }
        .card { background: #1f1f2e; border-radius: 12px; padding: 18px; margin-bottom: 20px; box-shadow: 0 4px 12px rgba(0,0,0,0.3); }
        label { display: block; margin: 10px 0 5px; font-weight: bold; font-size: 0.85rem; color: #aaa; }
        input[type="text"], input[type="file"], select, button { width: 100%; box-sizing: border-box; padding: 12px; border-radius: 8px; border: 1px solid #333; background: #2a2a3d; color: #fff; font-size: 1rem; }
        button { background: #ffcc00; color: #000; font-weight: bold; cursor: pointer; border: none; margin-top: 15px; }
        button:hover { background: #e6b800; }
        .status { margin-top: 10px; font-size: 0.85rem; color: #00ff88; text-align: center; }
        .badge { display: inline-block; background: #33334d; color: #ffcc00; padding: 3px 8px; border-radius: 6px; font-size: 0.8rem; }
    </style>
</head>
<body>
    <h1>📟 DeskConnect Hub</h1>
    <div class="subtitle">Weltweite Display-Kommunikation</div>

    <div class="card">
        <h3>🔑 Raum & Verbindung</h3>
        <label>Gemeinsamer Raum-Code:</label>
        <input type="text" id="roomCode" value="DeskRoom-1" placeholder="z. B. Familie-2026">
        <label>Ziel:</label>
        <select id="targetSelect">
            <option value="all">Alle Displays im Raum</option>
        </select>
    </div>

    <div class="card">
        <h3>📸 Foto senden</h3>
        <input type="file" id="photoInput" accept="image/*">
        <button onclick="uploadPhoto()">Foto an Display übertragen</button>
        <div id="photoStatus" class="status"></div>
    </div>

    <div class="card">
        <h3>⚡ Status-Nachrichten</h3>
        <div style="display:grid; grid-template-columns: 1fr 1fr; gap: 8px;">
            <button onclick="sendPreset('🍕', 'Ich habe Hunger!')" style="background:#252538; color:#fff; border:1px solid #FF8800; margin:0; padding:10px; font-size:0.9rem;">🍕 Hunger!</button>
            <button onclick="sendPreset('❤️', 'Ich mag dich sehr!')" style="background:#252538; color:#fff; border:1px solid #EF4444; margin:0; padding:10px; font-size:0.9rem;">❤️ Hab dich lieb</button>
            <button onclick="sendPreset('😊', 'Mir geht es gut!')" style="background:#252538; color:#fff; border:1px solid #22C55E; margin:0; padding:10px; font-size:0.9rem;">😊 Alles gut</button>
            <button onclick="sendPreset('💤', 'Ich gehe jetzt schlafen. Gute Nacht!')" style="background:#252538; color:#fff; border:1px solid #6366F1; margin:0; padding:10px; font-size:0.9rem;">💤 Schlafen</button>
            <button onclick="sendPreset('☕', 'Lust auf eine Kaffeepause?')" style="background:#252538; color:#fff; border:1px solid #D97706; margin:0; padding:10px; font-size:0.9rem;">☕ Kaffee?</button>
            <button onclick="sendPreset('🛑', 'Bitte nicht stören / Im Call!')" style="background:#252538; color:#fff; border:1px solid #DC2626; margin:0; padding:10px; font-size:0.9rem;">🛑 Bitte Ruhe</button>
            <button onclick="sendPreset('🏃', 'Bin in 5 Minuten da!')" style="background:#252538; color:#fff; border:1px solid #06B6D4; margin:0; padding:10px; font-size:0.9rem;">🏃 Gleich da</button>
            <button onclick="sendPreset('🎉', 'Feierabend für heute!')" style="background:#252538; color:#fff; border:1px solid #F59E0B; margin:0; padding:10px; font-size:0.9rem;">🎉 Feierabend</button>
        </div>
        <div id="presetStatus" class="status"></div>
    </div>

    <div class="card">
        <h3>💬 Freitext-Nachricht</h3>
        <input type="text" id="msgInput" placeholder="Z. B. Viele Grüße aus Berlin! ☕">
        <button onclick="sendMessage()">Nachricht senden</button>
        <div id="msgStatus" class="status"></div>
    </div>

    <script>
        const urlParams = new URLSearchParams(window.location.search);
        if (urlParams.has('room')) {
            document.getElementById('roomCode').value = urlParams.get('room');
        }

        async function sendPreset(icon, text) {
            const room = document.getElementById('roomCode').value.trim();
            const target = document.getElementById('targetSelect').value;
            document.getElementById('presetStatus').innerText = 'Wird gesendet...';

            const res = await fetch('/api/notify', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ room: room, target: target, text: text, icon: icon, type: 'preset' })
            });
            const data = await res.json();
            document.getElementById('presetStatus').innerText = `${icon} Gesendet an ${data.delivered_to} Display(s)!`;
            setTimeout(() => { document.getElementById('presetStatus').innerText = ''; }, 3000);
        }

        async function uploadPhoto() {
            const file = document.getElementById('photoInput').files[0];
            const room = document.getElementById('roomCode').value.trim();
            const target = document.getElementById('targetSelect').value;
            if (!file) return alert('Bitte ein Foto auswählen!');

            const formData = new FormData();
            formData.append('file', file);
            formData.append('room', room);
            formData.append('target', target);

            document.getElementById('photoStatus').innerText = 'Wird konvertiert (320x240) & gesendet...';
            const res = await fetch('/api/upload', { method: 'POST', body: formData });
            const data = await res.json();
            document.getElementById('photoStatus').innerText = data.message || 'Gesendet!';
        }

        async function sendMessage() {
            const text = document.getElementById('msgInput').value;
            const room = document.getElementById('roomCode').value.trim();
            const target = document.getElementById('targetSelect').value;
            if (!text) return;

            const res = await fetch('/api/notify', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ room: room, target: target, text: text, type: 'chat' })
            });
            const data = await res.json();
            document.getElementById('msgStatus').innerText = 'Nachricht übertragen!';
            document.getElementById('msgInput').value = '';
        }
    </script>
</body>
</html>
"""

@app.get("/", response_class=HTMLResponse)
async def get_index():
    return HTML_PAGE

@app.post("/api/upload")
async def upload_image(file: UploadFile = File(...), room: str = Form("DeskRoom-1"), target: str = Form("all")):
    """Nimmt Handy-Fotos entgegen, skaliert & schneidet sie auf 320x240 Pixel zu und leitet sie im Raum weiter."""
    contents = await file.read()
    image = Image.open(io.BytesIO(contents)).convert("RGB")

    # Optimal auf 320x240 anpassen (Center Crop)
    target_size = (320, 240)
    resized_img = ImageOps.fit(image, target_size, method=Image.Resampling.LANCZOS)

    output_buf = io.BytesIO()
    resized_img.save(output_buf, format="JPEG", quality=80)
    jpeg_bytes = output_buf.getvalue()

    room_clients = rooms.get(room, {})
    delivered_count = 0
    for client_id, ws in list(room_clients.items()):
        if target == "all" or target == client_id:
            try:
                await ws.send_bytes(jpeg_bytes)
                delivered_count += 1
            except Exception:
                pass

    return {
        "status": "ok",
        "room": room,
        "message": f"Bild an {delivered_count} Display(s) in Raum '{room}' übertragen (320x240)",
        "bytes": len(jpeg_bytes)
    }

@app.post("/api/notify")
async def send_notification(payload: dict):
    """Webhook für Home Assistant oder Browser: { room: 'DeskRoom-1', target: 'all', text: 'Wäsche fertig' }"""
    room = payload.get("room", "DeskRoom-1")
    target = payload.get("target", "all")
    text = payload.get("text", "")
    msg_type = payload.get("type", "notification")

    room_clients = rooms.get(room, {})
    delivered_count = 0
    for client_id, ws in list(room_clients.items()):
        if target == "all" or target == client_id:
            try:
                await ws.send_json({"type": msg_type, "text": text})
                delivered_count += 1
            except Exception:
                pass

    return {"status": "ok", "delivered_to": delivered_count, "room": room}

@app.websocket("/ws/{room}/{client_id}")
async def websocket_endpoint(websocket: WebSocket, room: str, client_id: str):
    """Echtzeit-Verbindung für Displays weltweit über gemeinsamen Room-Code"""
    await websocket.accept()
    if room not in rooms:
        rooms[room] = {}
    rooms[room][client_id] = websocket
    print(f"[+] Display verbunden: {client_id} in Raum '{room}'")

    try:
        while True:
            data = await websocket.receive_text()
            # An Partner-Displays im selben Raum weiterleiten
            for other_id, other_ws in rooms[room].items():
                if other_id != client_id:
                    await other_ws.send_text(data)
    except WebSocketDisconnect:
        print(f"[-] Display getrennt: {client_id} aus Raum '{room}'")
        if room in rooms:
            rooms[room].pop(client_id, None)
            if not rooms[room]:
                rooms.pop(room, None)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
