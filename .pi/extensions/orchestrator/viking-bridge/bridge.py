#!/usr/bin/env python3
"""Thin CLI bridge between TypeScript orchestrator and OpenViking SDK.

Reads a JSON command from stdin, dispatches to SyncOpenViking, returns JSON on stdout.

Commands:
  status                         - health check
  search  {query, level, uri, limit}  - semantic search (L0/L1/L2)
  abstract {uri}                 - L0 tier read (summary)
  overview {uri}                 - L1 tier read (overview)
  read     {uri}                 - L2 tier read (full content)
  ls       {uri}                 - list directory contents
  add_resource {uri, content, metadata} - index a file
  create_session {name}          - start memory session
  add_message {session_id, role, content} - append to session
  commit_session {session_id}    - commit session (triggers self-iteration)
"""

import json
import sys

try:
    from openviking import SyncOpenViking
except ImportError:
    # If openviking isn't installed, return error for every command
    resp = {"ok": False, "error": "openviking package not installed"}
    json.dump(resp, sys.stdout)
    sys.exit(0)


def main() -> None:
    raw = sys.stdin.read().strip()
    if not raw:
        json.dump({"ok": False, "error": "empty stdin"}, sys.stdout)
        return

    try:
        msg = json.loads(raw)
    except json.JSONDecodeError as e:
        json.dump({"ok": False, "error": f"invalid JSON: {e}"}, sys.stdout)
        return

    command = msg.get("command", "")
    args = msg.get("args", {})

    try:
        client = SyncOpenViking()
        result = dispatch(client, command, args)
        json.dump({"ok": True, "result": result}, sys.stdout)
    except Exception as e:
        json.dump({"ok": False, "error": str(e)}, sys.stdout)


def dispatch(client: "SyncOpenViking", command: str, args: dict) -> object:
    if command == "status":
        client.is_healthy()
        return {"status": "healthy"}

    if command == "search":
        results = client.search(
            query=args["query"],
            level=args.get("level", "L1"),
            uri=args.get("uri"),
            limit=args.get("limit", 10),
        )
        return [
            {"uri": r.uri, "title": r.title, "snippet": r.snippet, "score": r.score}
            for r in results
        ]

    if command == "abstract":
        return {"content": client.abstract(args["uri"])}

    if command == "overview":
        return {"content": client.overview(args["uri"])}

    if command == "read":
        return {"content": client.read(args["uri"])}

    if command == "ls":
        entries = client.ls(args["uri"])
        return [{"uri": e.uri, "name": e.name, "type": e.type} for e in entries]

    if command == "add_resource":
        client.add_resource(
            uri=args["uri"],
            content=args["content"],
            metadata=args.get("metadata", {}),
        )
        return {"indexed": True}

    if command == "create_session":
        session_id = client.create_session(name=args.get("name", "orchestrator"))
        return {"session_id": session_id}

    if command == "add_message":
        client.add_message(
            session_id=args["session_id"],
            role=args.get("role", "assistant"),
            content=args["content"],
        )
        return {"added": True}

    if command == "commit_session":
        client.commit_session(args["session_id"])
        return {"committed": True}

    raise ValueError(f"unknown command: {command}")


if __name__ == "__main__":
    main()
