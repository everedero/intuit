#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bumble central for pilot_wire on native_sim: central.py <hci transport>

Pairs in pairing mode (limited discoverable), writes the mode, then
reconnects with the bond through the non-discoverable advertising."""

import asyncio
import sys

from bumble.core import UUID
from bumble.device import Device, Peer
from bumble.hci import Address
from bumble.transport import open_transport

MODE = UUID("6f1e0002-5b2c-4d8a-9c3e-2a7b41c0d5e1")


async def find(dev):
    found = asyncio.get_running_loop().create_future()

    def on_adv(adv):
        name = adv.data.get(0x09) or adv.data.get(0x08)
        flags = adv.data.get(0x01)
        print(f"adv {adv.address} flags={flags} name={name}")
        if not found.done():
            found.set_result((adv.address, flags))

    dev.on("advertisement", on_adv)
    await dev.start_scanning(active=True)
    addr, flags = await asyncio.wait_for(found, 20)
    await dev.stop_scanning()
    dev.remove_listener("advertisement", on_adv)
    return addr, flags


async def session(dev, addr, value, pair):
    conn = await dev.connect(addr)
    print("connected", conn)
    if pair:
        await conn.pair()
        print("paired")
    else:
        await conn.encrypt()
        print("encrypted with the stored bond")
    peer = Peer(conn)
    await peer.discover_services()
    await peer.discover_characteristics()
    chrc = peer.get_characteristics_by_uuid(MODE)[0]
    print("mode read", (await chrc.read_value())[0])
    await chrc.write_value(bytes([value]), with_response=True)
    got = (await chrc.read_value())[0]
    print("mode written", value, "read back", got)
    assert got == value
    await conn.disconnect()


async def main():
    async with await open_transport(sys.argv[1]) as (src, sink):
        dev = Device.with_hci("central", Address("F0:F1:F2:F3:F4:F5"), src, sink)
        await dev.power_on()
        addr, flags = await find(dev)
        assert flags is not None and flags & 0x01, "expected limited discoverable"
        await session(dev, addr, 2, pair=True)
        await asyncio.sleep(1)
        addr, flags = await find(dev)
        assert not (flags or 0) & 0x03, "expected non-discoverable"
        await session(dev, addr, 1, pair=False)
        print("PASS")


asyncio.run(main())
