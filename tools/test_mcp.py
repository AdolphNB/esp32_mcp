#!/usr/bin/env python3
"""Smoke test for the ESP32-S3 MCP node.

Requires the official MCP Python SDK:
    python3 -m pip install 'mcp>=1.0.0'

Usage:
    python3 tools/test_mcp.py --url http://192.168.3.50:8080/mcp --token <token>
"""

import argparse
import asyncio
import json

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client


async def run(url: str, token: str) -> None:
    headers = {"Authorization": f"Bearer {token}"} if token else {}
    async with streamablehttp_client(url, headers=headers) as (read, write, _):
        async with ClientSession(read, write) as session:
            init = await session.initialize()
            print(f"server: {init.serverInfo.name} {init.serverInfo.version}")
            print(f"protocol: {init.protocolVersion}")

            tools = await session.list_tools()
            names = [t.name for t in tools.tools]
            print(f"tools ({len(names)}): {', '.join(names)}")

            for args in ({},):
                result = await session.call_tool("device_info", args)
                print("device_info:")
                for item in result.content:
                    text = getattr(item, "text", None)
                    if text:
                        try:
                            print(json.dumps(json.loads(text), indent=2, ensure_ascii=False))
                        except json.JSONDecodeError:
                            print(text)
                if result.isError:
                    raise SystemExit("device_info returned isError=true")

            result = await session.call_tool("ota_status", {})
            for item in result.content:
                text = getattr(item, "text", None)
                if text:
                    print("ota_status:", text)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://192.168.3.50:8080/mcp")
    parser.add_argument("--token", default="")
    args = parser.parse_args()
    asyncio.run(run(args.url, args.token))


if __name__ == "__main__":
    main()
