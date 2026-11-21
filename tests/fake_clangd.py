#!/usr/bin/env python3
"""Exercise the IDE's LSP client without requiring an LLVM installation."""
import json
import sys

source, sink = sys.stdin.buffer, sys.stdout.buffer


def send(message):
    payload = json.dumps({"jsonrpc": "2.0", **message}).encode()
    sink.write(f"Content-Length: {len(payload)}\r\n\r\n".encode())
    sink.flush()
    sink.write(payload)
    sink.flush()


while True:
    headers = {}
    line = source.readline()
    while line not in (b"\r\n", b""):
        key, value = line.split(b":", 1)
        headers[key] = value.strip()
        line = source.readline()
    if not headers:
        break
    message = json.loads(source.read(int(headers[b"Content-Length"])))
    method = message.get("method", "")
    params = message.get("params", {})
    if method == "initialize":
        send({"id": message["id"], "result": {"capabilities": {
            "textDocumentSync": 2, "completionProvider": {}}}})
    elif method in ("textDocument/didOpen", "textDocument/didChange"):
        document = params["textDocument"]
        text = document["text"] if "text" in document else params["contentChanges"][0]["text"]
        diagnostics = []
        for line, text_line in enumerate(text.splitlines()):
            if "PICO_IDE_BAD" not in text_line:
                continue
            start = text_line.index("PICO_IDE_BAD")
            diagnostics.append({
                "message": "identificateur inconnu PICO_IDE_BAD",
                "severity": 1,
                "range": {"start": {"line": line, "character": start},
                          "end": {"line": line, "character": start + len("PICO_IDE_BAD")}}
            })
        send({"method": "textDocument/publishDiagnostics", "params": {
            "uri": document["uri"], "version": document["version"], "diagnostics": diagnostics}})
    elif method == "textDocument/completion":
        send({"id": message["id"], "result": {"isIncomplete": False, "items": [
            {"label": "PICO_IDE_AUTOCOMPLETE", "insertText": "PICO_IDE_AUTOCOMPLETE"}
        ]}})
