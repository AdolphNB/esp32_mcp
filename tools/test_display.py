#!/usr/bin/env python3
"""Smoke test for the ESP32 LCD display MCP tools.

Install the MCP Python SDK first:
    python3 -m pip install 'mcp>=1.0.0'

Usage:
    python3 tools/test_display.py --url http://192.168.3.50:8080/mcp --token <token>
"""

import argparse
import asyncio
import json

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client


def show(name: str, result) -> None:
    print(f"--- {name} ---")
    for item in result.content:
        text = getattr(item, "text", None)
        if not text:
            continue
        try:
            print(json.dumps(json.loads(text), indent=2, ensure_ascii=False))
        except json.JSONDecodeError:
            print(text)
    if result.isError:
        raise SystemExit(f"{name} returned isError=true")


async def run(url: str, token: str) -> None:
    headers = {"Authorization": f"Bearer {token}"} if token else {}
    async with streamablehttp_client(url, headers=headers) as (read, write, _):
        async with ClientSession(read, write) as session:
            init = await session.initialize()
            print(f"server: {init.serverInfo.name} {init.serverInfo.version}")

            tools = await session.list_tools()
            names = [t.name for t in tools.tools]
            print(f"tools ({len(names)}): {', '.join(names)}")
            required = {"display_info", "display_quote", "display_todo", "display_calendar"}
            missing = required - set(names)
            if missing:
                raise SystemExit(f"missing display tools: {sorted(missing)}")

            show("display_info", await session.call_tool("display_info", {}))

            show("display_quote", await session.call_tool("display_quote", {
                "text": "慢慢来，比较快。",
                "author": "佚名",
                "size": 48,
            }))

            show("display_todo", await session.call_tool("display_todo", {
                "title": "今天",
                "items": [
                    {"text": "项目评审", "prio": "high", "due": "10:30"},
                    {"text": "交付固件", "prio": "normal"},
                    {"text": "买电池", "prio": "low", "done": True},
                ],
                "footer": "按轻重缓急排序",
            }))

            show("display_calendar", await session.call_tool("display_calendar", {
                "events": ["10:30 项目评审", "14:00 交付固件"],
            }))

            show("display_page", await session.call_tool("display_page", {"action": "next"}))
            show("display_info", await session.call_tool("display_info", {}))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://192.168.3.50:8080/mcp")
    parser.add_argument("--token", default="")
    args = parser.parse_args()
    asyncio.run(run(args.url, args.token))


if __name__ == "__main__":
    main()
