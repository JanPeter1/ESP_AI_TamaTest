"""DeskConnect room relay and browser control panel.

Room membership and the short message history are process-local. Run one
Uvicorn worker unless a shared broker/store is added.
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import os
from collections import deque
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from uuid import uuid4

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field, ValidationError

app = FastAPI(
    title="DeskConnect Room Server",
    description="Prototyping-Server für Nachrichten zwischen CYD-Geräten im selben Raum.",
    version="0.2.0",
)

MAX_HISTORY = 8
MAX_TEXT_LENGTH = 120
ALLOWED_CATEGORIES = {"stimmung", "alltag", "naehe", "pause"}
rooms: dict[str, dict[str, dict[str, Any]]] = {}
history: dict[str, deque[dict[str, Any]]] = {}
room_locks: dict[str, asyncio.Lock] = {}


class MessageRequest(BaseModel):
    room: str = Field(min_length=1, max_length=31)
    sender_id: str = Field(min_length=1, max_length=31)
    sender_name: str = Field(default="", max_length=31)
    avatar_id: int | None = Field(default=None, ge=0, le=1)
    preset_id: str = Field(default="", max_length=31)
    category: str = Field(default="alltag", max_length=16)
    emotion: str = Field(default="neutral", max_length=16)
    text: str = Field(min_length=1, max_length=MAX_TEXT_LENGTH)


def avatar_for(sender_id: str) -> int:
    """Return a stable CC0 avatar choice for a device identity."""
    return hashlib.sha256(sender_id.encode("utf-8")).digest()[0] % 2


def message_payload(request: MessageRequest) -> dict[str, Any]:
    category = request.category if request.category in ALLOWED_CATEGORIES else "alltag"
    return {
        "type": "message",
        "id": uuid4().hex,
        "room": request.room,
        "sender_id": request.sender_id,
        "sender_name": request.sender_name.strip() or request.sender_id,
        "avatar_id": request.avatar_id if request.avatar_id is not None else avatar_for(request.sender_id),
        "preset_id": request.preset_id,
        "category": category,
        "emotion": request.emotion,
        "text": request.text.strip()[:MAX_TEXT_LENGTH],
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }


async def broadcast(room: str, payload: dict[str, Any]) -> None:
    lock = room_locks.setdefault(room, asyncio.Lock())
    async with lock:
        peers = list(rooms.get(room, {}).items())
        for client_id, peer in peers:
            try:
                await peer["socket"].send_json(payload)
            except (WebSocketDisconnect, RuntimeError, OSError):
                rooms.get(room, {}).pop(client_id, None)


def participants_for(room: str) -> list[dict[str, Any]]:
    return [
        {"sender_id": client_id, "sender_name": peer["name"], "avatar_id": peer["avatar_id"]}
        for client_id, peer in rooms.get(room, {}).items()
    ]


WEB_INDEX = Path(__file__).with_name("index.html")


@app.get("/", response_class=HTMLResponse)
async def index() -> HTMLResponse:
    return HTMLResponse(WEB_INDEX.read_text(encoding="utf-8"))


@app.get("/api/rooms/{room}/participants")
async def get_participants(room: str) -> dict[str, Any]:
    return {"room": room, "participants": participants_for(room)}


@app.post("/api/messages")
async def post_message(request: MessageRequest) -> dict[str, Any]:
    if not request.text.strip():
        raise HTTPException(status_code=422, detail="text must not be blank")
    payload = message_payload(request)
    history.setdefault(request.room, deque(maxlen=MAX_HISTORY)).append(payload)
    await broadcast(request.room, payload)
    return {"status": "ok", "message": payload, "delivered_to": len(rooms.get(request.room, {}))}


@app.post("/api/notify")
async def legacy_notify(payload: dict[str, Any]) -> dict[str, Any]:
    """Compatibility endpoint for the previous browser page and integrations."""
    try:
        request = MessageRequest(
            room=str(payload.get("room", "DeskRoom-1")),
            sender_id=str(payload.get("sender_id", "webhook")),
            sender_name=str(payload.get("sender_name", "Benachrichtigung")),
            preset_id=str(payload.get("preset_id", payload.get("type", "notification"))),
            category=str(payload.get("category", "alltag")),
            emotion=str(payload.get("emotion", "neutral")),
            text=str(payload.get("text", "")),
        )
    except ValidationError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return await post_message(request)


@app.websocket("/ws/{room}/{client_id}")
async def websocket_room(websocket: WebSocket, room: str, client_id: str) -> None:
    if len(room) > 31 or not room or len(client_id) > 31 or not client_id:
        await websocket.close(code=1008)
        return

    await websocket.accept()
    room_peers = rooms.setdefault(room, {})
    peer = {"socket": websocket, "name": client_id, "avatar_id": avatar_for(client_id)}
    room_peers[client_id] = peer
    await websocket.send_json({
        "type": "history",
        "room": room,
        "messages": list(history.get(room, ())),
        "participants": participants_for(room),
    })
    await broadcast(room, {"type": "presence", "action": "joined", "participant": {
        "sender_id": client_id, "sender_name": peer["name"], "avatar_id": peer["avatar_id"]
    }})

    try:
        while True:
            raw = await websocket.receive_text()
            try:
                incoming = json.loads(raw)
            except json.JSONDecodeError:
                incoming = {"type": "message", "text": raw}

            if not isinstance(incoming, dict):
                continue
            if incoming.get("type") == "hello":
                peer["name"] = str(incoming.get("sender_name") or client_id)[:31]
                try:
                    peer["avatar_id"] = int(incoming.get("avatar_id", avatar_for(client_id))) % 2
                except (TypeError, ValueError):
                    peer["avatar_id"] = avatar_for(client_id)
                await broadcast(room, {"type": "presence", "action": "updated", "participant": {
                    "sender_id": client_id, "sender_name": peer["name"], "avatar_id": peer["avatar_id"]
                }})
                continue
            if incoming.get("type") not in {"message", "preset"}:
                continue
            text = str(incoming.get("text", "")).strip()[:MAX_TEXT_LENGTH]
            if not text:
                continue
            request = MessageRequest(
                room=room,
                sender_id=client_id,
                sender_name=peer["name"],
                avatar_id=peer["avatar_id"],
                preset_id=str(incoming.get("preset_id", ""))[:31],
                category=str(incoming.get("category", "alltag"))[:16],
                emotion=str(incoming.get("emotion", "neutral"))[:16],
                text=text,
            )
            message = message_payload(request)
            history.setdefault(room, deque(maxlen=MAX_HISTORY)).append(message)
            await broadcast(room, message)
    except WebSocketDisconnect:
        pass
    finally:
        room_peers = rooms.get(room, {})
        if room_peers.get(client_id, {}).get("socket") is websocket:
            room_peers.pop(client_id, None)
        if not room_peers:
            rooms.pop(room, None)
        await broadcast(room, {"type": "presence", "action": "left", "sender_id": client_id})


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        app,
        host="0.0.0.0",
        port=int(os.environ.get("PORT", "8000")),
    )
