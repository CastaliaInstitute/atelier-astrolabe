"""Exercise desktop MCP tools against the isolated local image-test VM."""
import asyncio
import base64
from pathlib import Path
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main():
    here = Path(__file__).resolve().parent
    parameters = StdioServerParameters(command=sys.executable, args=[str(here / 'server.py')])
    async with stdio_client(parameters) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            endpoint = {'host': '127.0.0.1', 'port': 22222}
            for tool, args in [('astrolabe_target_click', {'x': 120, 'y': 100}),
                               ('astrolabe_target_keys', {'keysyms': [0xffe3, ord('l')]}),
                               ('astrolabe_target_type', {'text': 'echo astrolabe-mcp-ok\n'}),
                               ('astrolabe_target_screen', {})]:
                result = await session.call_tool(tool, dict(endpoint, **args))
                assert not result.isError, result
                pictures = [item for item in result.content if item.type == 'image']
                assert len(pictures) == 1 and pictures[0].mimeType == 'image/png'
                data = base64.b64decode(pictures[0].data)
                assert data.startswith(b'\x89PNG\r\n\x1a\n')
                (here / '.state/boot-image/mcp-screen.png').write_bytes(data)
                print(tool, 'returned PNG', flush=True)


if __name__ == '__main__':
    asyncio.run(main())
