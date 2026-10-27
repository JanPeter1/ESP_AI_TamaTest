# DeskConnect Server Interface

The current interface is **WebSocket JSON, version 1**, for ESP32 clients, browser clients and other room participants. The server routes messages between clients in the same room, returns a short in-memory history after connection, and exposes HTTP routes for sending and participant status. The browser page at `server/index.html` joins the same WebSocket room to display live messages and history.

## Connection

Connect to:

```text
ws://<host>:<port>/ws/<room>/<client_id>
wss://<host>/ws/<room>/<client_id>
```

Use `wss://` on TLS port 443. URL-encode each path segment. `room` identifies the shared room; `client_id` must be unique per device. On connect the server returns the most recent 8 room messages:

```json
{"type":"history","room":"DeskRoom-1","messages":[],"participants":[]}
```

Clients should then send a `hello` frame to set the display name and select one of the bundled avatars:

```json
{"type":"hello","sender_name":"Wohnzimmer","avatar_id":1}
```

`avatar_id` is `0` for the cat and `1` for the dog. The server broadcasts `presence` events with `action` equal to `joined`, `updated` or `left`.

## Message event

Send a UTF-8 JSON text frame:

```json
{
  "type":"message",
  "preset_id":"kaffee",
  "category":"alltag",
  "emotion":"curious",
  "text":"Lust auf eine Kaffeepause?"
}
```

The server supplies `id`, `room`, `sender_id`, `sender_name`, `avatar_id` and UTC `timestamp`, then broadcasts the normalized event to every client in the room, including the sender. A received event looks like:

```json
{
  "type":"message",
  "id":"<server-generated-id>",
  "room":"DeskRoom-1",
  "sender_id":"Wohnzimmer",
  "sender_name":"Wohnzimmer",
  "avatar_id":1,
  "preset_id":"kaffee",
  "category":"alltag",
  "emotion":"curious",
  "text":"Lust auf eine Kaffeepause?",
  "timestamp":"2026-10-27T12:00:00+00:00"
}
```

Supported categories are `stimmung`, `alltag`, `naehe`, and `pause`. `emotion` is a short display hint, not a clinical or authoritative reading of the sender. The first firmware avatar set is compiled into the device, so messages carry `avatar_id` rather than image bytes. Keep messages under 120 UTF-8 characters; each event is capped by the firmware receive buffer.

## HTTP routes

- `GET /` — dark, mobile-friendly browser sender.
- `GET /api/rooms/{room}/participants` — currently connected room members.
- `POST /api/messages` — send a message using the same fields, with `room` and `sender_id` required.
- `POST /api/notify` — compatibility endpoint for the previous prototype; accepts `room` and `text`.

The server keeps at most 8 messages per room in process memory. It has no authentication, database, or shared broker yet; run a single Uvicorn worker. Use it on a trusted network during development. Public deployment needs authentication, rate limits, durable storage and operational monitoring before carrying private messages.

Run locally from `server/` with `python -m pip install -r requirements.txt` followed by `python app.py` (default port 8000, bound to all network interfaces; `PORT` overrides the port). Configure each display's server host, port and room code through its setup portal. For devices on the same LAN, use the PC's LAN address; guest Wi-Fi or client isolation may block this traffic.
