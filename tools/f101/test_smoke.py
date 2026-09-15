"""Exercise real MCP discovery and read-only hardware access; never flashes."""
import asyncio
from pathlib import Path
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from usb_console import Console


async def main():
    parameters = StdioServerParameters(command=sys.executable,
        args=[str(Path(__file__).with_name('server.py'))])
    async with stdio_client(parameters) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            listing = await session.list_tools()
            names = {tool.name for tool in listing.tools}
            assert names == {'astrolabe_inventory', 'astrolabe_console', 'astrolabe_monitor',
                             'astrolabe_build_cyber', 'astrolabe_flash_cyber', 'astrolabe_job',
                             'astrolabe_ble_scan', 'astrolabe_ble_status', 'astrolabe_ble_select_face',
                             'astrolabe_ble_wifi', 'astrolabe_sd_status',
                             'astrolabe_target_screen', 'astrolabe_target_click',
                             'astrolabe_target_type', 'astrolabe_target_keys'}
            inventory = await session.call_tool('astrolabe_inventory', {})
            assert not inventory.isError
            print('MCP initialized; fifteen tools discovered; inventory OK')
            invalid_job = await session.call_tool('astrolabe_job', {'job_id': '../../etc/passwd'})
            assert invalid_job.isError
            if '--no-usb' in sys.argv:
                print('USB hardware checks explicitly skipped')
                return
            board = await session.call_tool('astrolabe_console', {'command': 'qa board'})
            assert not board.isError, board
            assert 'flash=16MB' in str(board.content), board
            print('Live qa board: verified 16MB hardware')
            status = await session.call_tool('astrolabe_console', {'command': 'qa status'})
            assert not status.isError and 'qa:' in str(status.content), status
            print('Live qa status: received firmware response')
            invalid = await session.call_tool('astrolabe_console', {'command': 'qa status\nreboot'})
            assert invalid.isError
            with Console():
                busy = await session.call_tool('astrolabe_console', {'command': 'qa status'})
                assert busy.isError and 'busy' in str(busy.content)
            print('Rejected multiline input, invalid job ID, and concurrent USB access')


if __name__ == '__main__':
    asyncio.run(main())
