#!/usr/bin/env -S deno run --allow-read

import { scoreLunaSayReadingQuality } from "../supabase/functions/_shared/lunasayReadingQuality.ts";
import type { LunaSayDailyPacket } from "../supabase/functions/_shared/lunasayDailyPacket.ts";

function usage(): never {
  console.error(
    "usage: deno run --allow-read scripts/lunasay_reading_quality.ts PACKET.json [FACTS.txt]",
  );
  Deno.exit(2);
}

const packetPath = Deno.args[0] || usage();
const factsPath = Deno.args[1];
const envelope = JSON.parse(await Deno.readTextFile(packetPath));
const packet = (envelope.packet ?? envelope) as LunaSayDailyPacket;
const facts = factsPath ? await Deno.readTextFile(factsPath) : "";
const report = scoreLunaSayReadingQuality(packet, facts);
console.log(JSON.stringify(report, null, 2));
Deno.exit(report.releaseGatePassed ? 0 : 1);
