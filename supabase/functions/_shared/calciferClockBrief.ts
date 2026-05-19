/** CalDAV fetch + brief text for Castalia “Calcifer” schedule (server-side secrets only). */

import ICAL from "npm:ical.js@2.0.1";

export type CalendarEvent = {
  summary: string;
  start: Date;
  end: Date;
};

function basicAuthHeader(user: string, password: string): string {
  const raw = `${user}:${password}`;
  const b64 = btoa(raw);
  return `Basic ${b64}`;
}

function toCalDavUtc(d: Date): string {
  const s = d.toISOString().replace(/[-:]/g, "").replace(/\.\d{3}Z$/, "Z");
  return s;
}

function decodeXmlText(s: string): string {
  return s
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&amp;/g, "&");
}

/** Pull <calendar-data> bodies from a CalDAV multistatus XML response. */
export function extractCalendarDataBodies(xml: string): string[] {
  const out: string[] = [];
  const re =
    /<(?:[\w.]+:)?calendar-data\b[^>]*>([\s\S]*?)<\/(?:[\w.]+:)?calendar-data>/gi;
  let m: RegExpExecArray | null;
  while ((m = re.exec(xml)) !== null) {
    let inner = m[1].trim();
    const cdata = inner.match(/^<!\[CDATA\[([\s\S]*)\]\]>$/i);
    if (cdata) {
      inner = cdata[1].trim();
    }
    out.push(decodeXmlText(inner));
  }
  return out;
}

export async function fetchEventsViaCalendarQuery(params: {
  calendarUrl: string;
  user: string;
  password: string;
  rangeStart: Date;
  rangeEnd: Date;
}): Promise<CalendarEvent[]> {
  const { calendarUrl, user, password, rangeStart, rangeEnd } = params;
  const startAttr = toCalDavUtc(rangeStart);
  const endAttr = toCalDavUtc(rangeEnd);
  const xml =
    `<?xml version="1.0" encoding="utf-8"?>` +
    `<C:calendar-query xmlns:C="urn:ietf:params:xml:ns:caldav" xmlns:D="DAV:">` +
    `<D:prop><C:calendar-data/></D:prop>` +
    `<C:filter>` +
    `<C:comp-filter name="VCALENDAR">` +
    `<C:comp-filter name="VEVENT">` +
    `<C:time-range start="${startAttr}" end="${endAttr}"/>` +
    `</C:comp-filter>` +
    `</C:comp-filter>` +
    `</C:filter>` +
    `</C:calendar-query>`;

  const res = await fetch(calendarUrl, {
    method: "REPORT",
    headers: {
      Authorization: basicAuthHeader(user, password),
      "Content-Type": "application/xml; charset=utf-8",
      Depth: "1",
    },
    body: xml,
  });
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`CalDAV REPORT ${res.status}: ${text.slice(0, 200)}`);
  }
  const bodies = extractCalendarDataBodies(text);
  const events: CalendarEvent[] = [];
  for (const ics of bodies) {
    if (!ics.includes("BEGIN:VCALENDAR")) continue;
    const blocks = ics.split(/(?=BEGIN:VCALENDAR)/g).map((b) => b.trim()).filter(
      Boolean,
    );
    for (const block of blocks) {
      try {
        const comp = new ICAL.Component(ICAL.parse(block));
        for (const ve of comp.getAllSubcomponents("vevent")) {
          const status = String(ve.getFirstPropertyValue("status") ?? "")
            .toUpperCase();
          if (status === "CANCELLED") continue;
          const ev = new ICAL.Event(ve);
          const start = ev.startDate.toJSDate();
          const end = ev.endDate.toJSDate();
          const summary = String(ev.summary || "Busy").trim() || "Busy";
          if (Number.isFinite(start.getTime()) && Number.isFinite(end.getTime()) &&
            end > start) {
            events.push({ summary, start, end });
          }
        }
      } catch {
        // skip malformed fragment
      }
    }
  }
  return events;
}

export function pickCurrentAndNext(
  events: CalendarEvent[],
  now: Date,
): { current: CalendarEvent | null; next: CalendarEvent | null } {
  let current: CalendarEvent | null = null;
  for (const e of events) {
    if (e.start <= now && now < e.end) {
      if (!current || e.end > current.end) {
        current = e;
      }
    }
  }
  let next: CalendarEvent | null = null;
  for (const e of events) {
    if (e.start <= now) continue;
    if (!next || e.start < next.start) {
      next = e;
    }
  }
  return { current, next };
}

export function formatTimePhrase(epochSeconds: number, timeZone: string): string {
  const d = new Date(epochSeconds * 1000);
  return new Intl.DateTimeFormat("en-US", {
    timeZone,
    dateStyle: "full",
    timeStyle: "short",
  }).format(d);
}

function formatEventWindow(
  e: CalendarEvent,
  timeZone: string,
  now: Date,
): string {
  const sameDay = (a: Date, b: Date) =>
    a.toLocaleDateString("en-CA", { timeZone }) ===
      b.toLocaleDateString("en-CA", { timeZone });
  const startFmt = new Intl.DateTimeFormat("en-US", {
    timeZone,
    hour: "numeric",
    minute: "2-digit",
  }).format(e.start);
  const endFmt = new Intl.DateTimeFormat("en-US", {
    timeZone,
    hour: "numeric",
    minute: "2-digit",
  }).format(e.end);
  if (sameDay(e.start, now) && sameDay(e.end, now)) {
    return `${e.summary}, from ${startFmt} to ${endFmt}`;
  }
  const startLong = new Intl.DateTimeFormat("en-US", {
    timeZone,
    weekday: "short",
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
  }).format(e.start);
  return `${e.summary}, starting ${startLong}`;
}

export function buildScheduleBriefText(params: {
  epochSeconds: number;
  timeZone: string;
  events: CalendarEvent[];
}): string {
  const { epochSeconds, timeZone, events } = params;
  const now = new Date(epochSeconds * 1000);
  const timePhrase = formatTimePhrase(epochSeconds, timeZone);
  const { current, next } = pickCurrentAndNext(events, now);

  let mid = "";
  if (current) {
    mid =
      `Right now: ${formatEventWindow(current, timeZone, now)}.`;
  } else {
    mid = "You have nothing on the calendar right now.";
  }
  let tail = "";
  if (next) {
    tail =
      ` Next up: ${formatEventWindow(next, timeZone, now)}.`;
  } else {
    tail = " There is nothing else on the calendar in this window.";
  }
  return `It is ${timePhrase}. ${mid}${tail}`;
}

export async function loadCalciferEvents(params: {
  calendarUrl: string;
  user: string;
  password: string;
  epochSeconds: number;
}): Promise<CalendarEvent[]> {
  const now = new Date(params.epochSeconds * 1000);
  const rangeStart = new Date(now.getTime() - 36 * 3600 * 1000);
  const rangeEnd = new Date(now.getTime() + 21 * 24 * 3600 * 1000);
  return await fetchEventsViaCalendarQuery({
    calendarUrl: params.calendarUrl,
    user: params.user,
    password: params.password,
    rangeStart,
    rangeEnd,
  });
}

export type CalciferEnv = {
  calendarUrl: string;
  user: string;
  password: string;
  displayTz: string;
};

/** Server-side Calcifer / Castalia calendar credentials (never sent from clients). */
export function readCalciferEnv(): CalciferEnv {
  const calendarUrl = Deno.env.get("CALCIFER_CALDAV_CALENDAR_URL")?.trim() ?? "";
  const user = Deno.env.get("CALCIFER_CALDAV_USER")?.trim() ?? "";
  const password = Deno.env.get("CALCIFER_CALDAV_PASSWORD")?.trim() ?? "";
  const displayTz =
    Deno.env.get("CALCIFER_DISPLAY_TZ")?.trim() || "America/New_York";
  return { calendarUrl, user, password, displayTz };
}

export async function buildClockAgendaTranscript(
  epochSeconds: number,
): Promise<string> {
  const { calendarUrl, user, password, displayTz } = readCalciferEnv();
  try {
    if (!calendarUrl || !user || !password) {
      return `It is ${formatTimePhrase(epochSeconds, displayTz)}. The Calcifer calendar is not configured on the server.`;
    }
    const events = await loadCalciferEvents({
      calendarUrl,
      user,
      password,
      epochSeconds,
    });
    return buildScheduleBriefText({
      epochSeconds,
      timeZone: displayTz,
      events,
    });
  } catch {
    return `It is ${formatTimePhrase(epochSeconds, displayTz)}. Calendar could not be read.`;
  }
}
