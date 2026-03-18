#!/usr/bin/env python3
"""Minimal MCP stdio server bridging to OpenViking HTTP API."""

import json
import sys
import urllib.request
import urllib.error
import urllib.parse

VIKING_URL = "http://127.0.0.1:1933"
TIMEOUT = 10

# --- HTTP helpers ---

def viking_get(path, params=None):
    url = f"{VIKING_URL}{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        return json.loads(resp.read())


def viking_post(path, body):
    url = f"{VIKING_URL}{path}"
    data = json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method="POST",
                                headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        return json.loads(resp.read())


# --- Tool definitions ---

TOOLS = [
    {
        "name": "search_find",
        "description": "Semantic vector search across the OpenViking knowledge base. Returns ranked results with abstracts.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "query": {"type": "string", "description": "Natural language search query"},
                "limit": {"type": "integer", "description": "Max results (default 10)", "default": 10},
                "target_uri": {"type": "string", "description": "Restrict search to a URI subtree (e.g. viking://resources/delve/)"},
            },
            "required": ["query"],
        },
    },
    {
        "name": "content_overview",
        "description": "Get a detailed overview/summary of a resource by URI.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "uri": {"type": "string", "description": "Resource URI (e.g. viking://resources/delve/terrain/)"},
            },
            "required": ["uri"],
        },
    },
    {
        "name": "content_abstract",
        "description": "Get a short abstract of a resource by URI.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "uri": {"type": "string", "description": "Resource URI"},
            },
            "required": ["uri"],
        },
    },
    {
        "name": "content_read",
        "description": "Read the full content of a resource by URI.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "uri": {"type": "string", "description": "Resource URI"},
            },
            "required": ["uri"],
        },
    },
    {
        "name": "fs_ls",
        "description": "List contents of a directory in the OpenViking filesystem.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "uri": {"type": "string", "description": "Directory URI to list"},
            },
            "required": ["uri"],
        },
    },
    {
        "name": "fs_tree",
        "description": "Get a tree view of the OpenViking filesystem from a URI root.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "uri": {"type": "string", "description": "Root URI for tree"},
                "depth": {"type": "integer", "description": "Max depth (default 3)", "default": 3},
            },
            "required": ["uri"],
        },
    },
    {
        "name": "search_grep",
        "description": "Search for a pattern in OpenViking resources (like grep).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "pattern": {"type": "string", "description": "Search pattern"},
                "uri": {"type": "string", "description": "Restrict to URI subtree"},
            },
            "required": ["pattern"],
        },
    },
]

# --- Tool handlers ---

def handle_tool(name, args):
    if name == "search_find":
        body = {"query": args["query"], "limit": args.get("limit", 10)}
        if args.get("target_uri"):
            body["target_uri"] = args["target_uri"]
        return viking_post("/api/v1/search/find", body)

    elif name == "content_overview":
        return viking_get("/api/v1/content/overview", {"uri": args["uri"]})

    elif name == "content_abstract":
        return viking_get("/api/v1/content/abstract", {"uri": args["uri"]})

    elif name == "content_read":
        return viking_get("/api/v1/content/read", {"uri": args["uri"]})

    elif name == "fs_ls":
        return viking_get("/api/v1/fs/ls", {"uri": args["uri"]})

    elif name == "fs_tree":
        params = {"uri": args["uri"]}
        if "depth" in args:
            params["depth"] = args["depth"]
        return viking_get("/api/v1/fs/tree", params)

    elif name == "search_grep":
        body = {"pattern": args["pattern"]}
        if args.get("uri"):
            body["uri"] = args["uri"]
        return viking_post("/api/v1/search/grep", body)

    else:
        raise ValueError(f"Unknown tool: {name}")


# --- JSON-RPC / MCP protocol ---

def make_response(id, result):
    return {"jsonrpc": "2.0", "id": id, "result": result}


def make_error(id, code, message):
    return {"jsonrpc": "2.0", "id": id, "error": {"code": code, "message": message}}


def handle_message(msg):
    method = msg.get("method")
    id = msg.get("id")
    params = msg.get("params", {})

    if method == "initialize":
        return make_response(id, {
            "protocolVersion": "2024-11-05",
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "openviking", "version": "0.1.0"},
        })

    elif method == "notifications/initialized":
        return None  # notification, no response

    elif method == "tools/list":
        return make_response(id, {"tools": TOOLS})

    elif method == "tools/call":
        tool_name = params.get("name")
        tool_args = params.get("arguments", {})
        try:
            result = handle_tool(tool_name, tool_args)
            text = json.dumps(result, indent=2, ensure_ascii=False)
            return make_response(id, {
                "content": [{"type": "text", "text": text}],
            })
        except urllib.error.URLError as e:
            return make_response(id, {
                "content": [{"type": "text", "text": f"OpenViking server unreachable: {e}"}],
                "isError": True,
            })
        except Exception as e:
            return make_response(id, {
                "content": [{"type": "text", "text": f"Error: {e}"}],
                "isError": True,
            })

    elif method == "ping":
        return make_response(id, {})

    elif method and method.startswith("notifications/"):
        return None  # ignore notifications

    else:
        return make_error(id, -32601, f"Method not found: {method}")


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue

        response = handle_message(msg)
        if response is not None:
            sys.stdout.write(json.dumps(response) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
