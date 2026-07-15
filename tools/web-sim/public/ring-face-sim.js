(function () {
  "use strict";

  const SPECIAL_FACE_VALUE = "ring24";
  const SIZE = 466;
  const CX = SIZE / 2;
  const CY = SIZE / 2;
  const TAU = Math.PI * 2;

  const state = {
    active: false,
    raf: 0,
    data: null,
    select: null,
    setFirmwareFace: null,
  };

  function clamp(value, lo, hi) {
    return Math.max(lo, Math.min(hi, value));
  }

  function minuteToAngle(minute) {
    return -Math.PI / 2 + (minute / 1440) * TAU;
  }

  function polar(radius, minute) {
    const a = minuteToAngle(minute);
    return {
      x: CX + Math.cos(a) * radius,
      y: CY + Math.sin(a) * radius,
    };
  }

  function drawArcText(ctx, text, radius, centerMinute, options = {}) {
    const flip = Boolean(options.flip);
    const centerAngle = minuteToAngle(centerMinute);

    ctx.save();
    ctx.font = options.font || "12px system-ui, sans-serif";
    ctx.fillStyle = options.color || "#eef3f6";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    const letterSpacing = options.letterSpacing ?? 1.2;
    const widths = Array.from(text, (ch) => ctx.measureText(ch).width + letterSpacing);
    const totalAngle = widths.reduce((sum, width) => sum + width / radius, 0);
    let angle = centerAngle - totalAngle / 2;
    for (let i = 0; i < text.length; ++i) {
      const charAngle = widths[i] / radius;
      const drawAngle = flip ? centerAngle + totalAngle / 2 - (angle + charAngle / 2 - (centerAngle - totalAngle / 2)) : angle + charAngle / 2;
      const x = CX + Math.cos(drawAngle) * radius;
      const y = CY + Math.sin(drawAngle) * radius;
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(drawAngle + (flip ? -Math.PI / 2 : Math.PI / 2));
      ctx.fillText(text[i], 0, 0);
      ctx.restore();
      angle += charAngle;
    }
    ctx.restore();
  }

  function drawTangentLabel(ctx, text, radius, minute, color) {
    const angle = minuteToAngle(minute);
    const p = polar(radius, minute);
    let rotation = angle + Math.PI / 2;
    while (rotation > Math.PI) rotation -= TAU;
    while (rotation < -Math.PI) rotation += TAU;
    if (rotation > Math.PI / 2 || rotation < -Math.PI / 2) {
      rotation += Math.PI;
    }
    ctx.save();
    ctx.translate(p.x, p.y);
    ctx.rotate(rotation);
    ctx.font = "700 9px system-ui, sans-serif";
    ctx.fillStyle = color;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(text, 0, 0);
    ctx.restore();
  }

  function colorForStress(value) {
    const t = clamp(value / 100, 0, 1);
    if (t < 0.34) return "#48c9a7";
    if (t < 0.67) return "#e6c65f";
    return "#e86f61";
  }

  function colorForHrv(value) {
    const t = clamp((value - 20) / 80, 0, 1);
    if (t < 0.34) return "#da6b62";
    if (t < 0.67) return "#d7bd64";
    return "#70d6a5";
  }

  function colorForSleep(stage) {
    switch (stage) {
      case "deep":
        return "#416bd6";
      case "rem":
        return "#a86ee8";
      case "awake":
        return "#e3a14c";
      case "light":
      default:
        return "#6aa6d9";
    }
  }

  function cyclePhase(cycleDay, cycleLength = 28) {
    if (!Number.isFinite(cycleDay) || cycleDay <= 0 || !Number.isFinite(cycleLength) || cycleLength < 21) {
      return "unknown";
    }
    const day = ((Math.round(cycleDay) - 1) % Math.round(cycleLength)) + 1;
    const ovulation = Math.max(11, Math.round(cycleLength) - 14);
    if (day <= 5) return "menstrual";
    if (day < ovulation - 2) return "follicular";
    if (day <= ovulation + 1) return "ovulatory";
    return "luteal";
  }

  function cycleContext(cycle = {}) {
    const length = Number.isFinite(cycle.length) ? cycle.length : 28;
    const day = Number.isFinite(cycle.day) && cycle.day > 0
      ? ((Math.round(cycle.day) - 1) % Math.round(length)) + 1
      : null;
    const phase = cycle.phase || (day ? cyclePhase(day, length) : "unknown");
    const source = cycle.source || (cycle.lastPeriodStart ? "calendar" : "unset");
    const confidence = source === "user-confirmed" ? "confirmed" : source === "calendar" ? "estimated" : "unknown";
    return { day, length, phase, source, confidence };
  }

  function makeDemoData(now = new Date()) {
    const stress = [];
    const hrv = [];
    const spo2 = [];
    const heart = [];
    const activity = [];
    for (let minute = 0; minute < 1440; minute += 30) {
      const dayWave = Math.sin((minute / 1440) * TAU - 1.1);
      const workPulse = Math.max(0, Math.sin(((minute - 540) / 540) * Math.PI));
      const eveningCalm = Math.max(0, Math.sin(((minute - 1110) / 330) * Math.PI));
      stress.push({
        minute,
        value: Math.round(clamp(34 + dayWave * 14 + workPulse * 38 - eveningCalm * 18, 10, 94)),
      });
      hrv.push({
        minute,
        value: Math.round(clamp(68 - workPulse * 28 + eveningCalm * 18 - dayWave * 8, 18, 110)),
      });
      spo2.push({
        minute,
        value: Math.round(clamp(97 - workPulse * 0.8 + Math.sin(minute / 91) * 0.4, 92, 100)),
      });
      activity.push({
        minute,
        value: Math.round(clamp(workPulse * 80 + Math.max(0, Math.sin(((minute - 1020) / 180) * Math.PI)) * 45, 0, 100)),
      });
    }
    for (let minute = 0; minute < 1440; minute += 5) {
      const exercise = Math.max(0, Math.sin(((minute - 1020) / 150) * Math.PI));
      const work = Math.max(0, Math.sin(((minute - 540) / 540) * Math.PI));
      heart.push({
        minute,
        value: Math.round(clamp(58 + work * 16 + exercise * 46 + Math.sin(minute / 37) * 3, 48, 132)),
      });
    }
    const sleep = [
      { start: 1395, end: 1440, stage: "light" },
      { start: 0, end: 70, stage: "deep" },
      { start: 70, end: 145, stage: "light" },
      { start: 145, end: 205, stage: "rem" },
      { start: 205, end: 315, stage: "deep" },
      { start: 315, end: 405, stage: "light" },
      { start: 405, end: 430, stage: "awake" },
    ];
    return {
      owner: "Camille",
      ring: "COLMI R10",
      battery: 72,
      charging: true,
      subjectiveMood: {
        emoji: "😐",
        label: "watchful",
        intensity: 0.58,
        updatedAgoMin: 12,
      },
      hrvBaseline: 78,
      restingHr: 54,
      cycle: {
        day: 23,
        length: 28,
        phase: cyclePhase(23, 28),
        source: "calendar",
        confidence: "estimated",
      },
      family: [
        { name: "Daniel", stress: 76, trend: 9, sleep: 338 },
      ],
      stress,
      hrv,
      spo2,
      heart,
      activity,
      sleep,
      nowMinute: now.getHours() * 60 + now.getMinutes() + now.getSeconds() / 60,
    };
  }

  function constantSeries(value, intervalMin) {
    const series = [];
    for (let minute = 0; minute < 1440; minute += intervalMin) {
      series.push({ minute, value });
    }
    return series;
  }

  function makeScenarioData(options = {}) {
    const hour = options.hour ?? 12;
    const minute = options.minute ?? 0;
    const stress = clamp(options.stress ?? 50, 0, 100);
    const trend30m = options.trend30m ?? 0;
    const hrv = clamp(options.hrv ?? 70, 10, 160);
    const spo2 = clamp(options.spo2 ?? 97, 70, 100);
    const hr = clamp(options.hr ?? 68, 35, 180);
    const activity = clamp(options.activity ?? 20, 0, 100);
    const sleep = clamp(options.sleepMinutes ?? 420, 0, 900);
    const cycleDay = options.cycleDay ?? 14;
    const cycleLength = options.cycleLength ?? 28;
    const cycleSource = options.cycleSource || "calendar";
    const nowMinute = hour * 60 + minute;
    const stressSeries = constantSeries(stress, 30);
    for (const sample of stressSeries) {
      if (sample.minute < nowMinute) {
        sample.value = clamp(stress - trend30m, 0, 100);
      }
    }
    return {
      owner: options.owner || "Camille",
      ring: options.ring || "COLMI R10",
      battery: options.battery ?? 72,
      charging: Boolean(options.charging),
      subjectiveMood: {
        emoji: options.moodEmoji || "😐",
        label: options.moodLabel || "watchful",
        intensity: options.moodIntensity ?? 0.5,
        updatedAgoMin: options.updatedAgoMin ?? 10,
      },
      hrvBaseline: options.hrvBaseline ?? 78,
      restingHr: options.restingHr ?? 54,
      cycle: {
        day: cycleDay,
        length: cycleLength,
        phase: cyclePhase(cycleDay, cycleLength),
        source: cycleSource,
        confidence: cycleSource === "user-confirmed" ? "confirmed" : "estimated",
      },
      family: [
        { name: "Daniel", stress: options.familyStress ?? 45, trend: options.familyTrend ?? 0, sleep: options.familySleep ?? 420 },
      ],
      stress: stressSeries,
      hrv: constantSeries(hrv, 30),
      spo2: constantSeries(spo2, 30),
      heart: constantSeries(hr, 5),
      activity: constantSeries(activity, 30),
      sleep: [{ start: 0, end: sleep, stage: "light" }],
      nowMinute,
    };
  }

  function latestAt(series, minute) {
    let latest = series[0];
    for (const sample of series) {
      if (sample.minute <= minute) latest = sample;
    }
    return latest;
  }

  function meanWindow(series, minute, windowMin) {
    const start = minute - windowMin;
    let sum = 0;
    let count = 0;
    for (const sample of series) {
      if (sample.minute >= start && sample.minute <= minute) {
        sum += sample.value;
        count += 1;
      }
    }
    return count ? sum / count : latestAt(series, minute).value;
  }

  function sleepMinutes(data) {
    return data.sleep.reduce((sum, item) => sum + (item.end >= item.start ? item.end - item.start : item.end + 1440 - item.start), 0);
  }

  function stressSummary(data) {
    const now = data.nowMinute;
    const stress = latestAt(data.stress, now).value;
    const previous = meanWindow(data.stress, now - 30, 90);
    const hrv = latestAt(data.hrv, now).value;
    const spo2 = data.spo2?.length ? latestAt(data.spo2, now).value : null;
    const hr = latestAt(data.heart, now).value;
    const activity = latestAt(data.activity, now).value;
    const sleep = sleepMinutes(data);
    const hrvDelta = Math.round(((hrv - data.hrvBaseline) / data.hrvBaseline) * 100);
    const hrDelta = hr - data.restingHr;
    const sleepDebt = Math.max(0, 420 - sleep);
    const activityContext = activity > 55;
    const familyHigh = data.family?.find((member) => member.stress >= 70);
    const subjectiveMood = data.subjectiveMood || { emoji: "😐", label: "watchful", intensity: 0.5 };
    const cycle = cycleContext(data.cycle);
    const phase = cycle.phase;
    const spo2Warn = spo2 !== null && spo2 < 94;
    let score = stress;
    score += hrvDelta < -12 ? 8 : hrvDelta > 8 ? -5 : 0;
    score += hrDelta > 18 && !activityContext ? 8 : 0;
    score += sleepDebt > 60 ? 7 : 0;
    score += familyHigh ? 4 : 0;
    score += spo2Warn ? 2 : 0;
    score = Math.round(clamp(score, 0, 100));

    const trend = Math.round(stress - previous);
    let stateLabel = "CALM";
    if (score >= 72) {
      stateLabel = "HIGH";
    } else if (score >= 58 || trend > 8) {
      stateLabel = "RISING";
    } else if (trend < -6) {
      stateLabel = "RECOVERING";
    }

    let action = "keep steady";
    if (spo2 !== null && spo2 < 92) {
      action = "check ring fit";
    } else if (stateLabel === "HIGH") {
      action = "2 min breathing";
    } else if (stateLabel === "RISING") {
      action = "slow exhale";
    } else if (stateLabel === "RECOVERING") {
      action = "stay low";
    } else if (familyHigh) {
      action = `check ${familyHigh.name}`;
    }

    const cycleTone = phase === "menstrual" || phase === "luteal" ? "context" : "ok";
    const cycleValue = phase === "unknown"
      ? "--"
      : `${phase.slice(0, 3).toUpperCase()}${cycle.day ? ` D${cycle.day}` : ""}`;
    const chips = [
      { label: "HRV", value: `${hrvDelta > 0 ? "+" : ""}${hrvDelta}%`, tone: hrvDelta < -12 ? "warn" : "ok" },
      { label: "HR", value: `+${hrDelta}`, tone: hrDelta > 18 && !activityContext ? "warn" : "ok" },
      { label: "SPO2", value: spo2 === null ? "--" : `${Math.round(spo2)}%`, tone: spo2Warn ? "warn" : "ok" },
      { label: "SLEEP", value: `${Math.floor(sleep / 60)}H${sleep % 60}`, tone: sleepDebt > 60 ? "warn" : "ok" },
      { label: cycle.confidence === "confirmed" ? "CYCLE" : "CYCLE*", value: cycleValue, tone: cycleTone },
      { label: "FAMILY", value: familyHigh ? `${familyHigh.name} ${familyHigh.stress}` : "OK", tone: familyHigh ? "warn" : "ok" },
    ];
    const moodStress = subjectiveMood.emoji === "🙂" || subjectiveMood.emoji === "😌" ? 20 :
      subjectiveMood.emoji === "😟" ? 68 :
        subjectiveMood.emoji === "😣" || subjectiveMood.emoji === "😰" ? 86 :
          subjectiveMood.emoji === "😡" ? 78 : 48;
    const mismatch = Math.abs(score - moodStress) >= 30;
    const avatar = {
      moodEmoji: subjectiveMood.emoji,
      moodLabel: subjectiveMood.label,
      measuredTension: clamp(score / 100, 0, 1),
      hrvTension: clamp((-hrvDelta - 4) / 42, 0, 1),
      mouth: clamp((50 - score) / 42, -1, 1),
      brow: clamp((score - 35) / 60, 0, 1),
      sweat: score >= 70 || (hrDelta > 18 && !activityContext),
      mismatch,
    };
    return { score, trend, stateLabel, action, chips, activityContext, familyHigh, avatar, hrvDelta, hrDelta, spo2, cyclePhase: phase, cycle };
  }

  function drawArcBand(ctx, radius, width, startMinute, endMinute, color, alpha = 1) {
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.lineCap = "butt";
    ctx.beginPath();
    ctx.arc(CX, CY, radius, minuteToAngle(startMinute), minuteToAngle(endMinute), false);
    ctx.stroke();
    ctx.restore();
  }

  function drawSegmentSeries(ctx, series, radius, width, intervalMin, colorFn, scaleFn) {
    drawArcBand(ctx, radius, width, 0, 1440, "#1e2932", 0.88);
    for (const sample of series) {
      const alpha = scaleFn ? scaleFn(sample.value) : 1;
      drawArcBand(ctx, radius, width, sample.minute + 1, sample.minute + intervalMin - 1, colorFn(sample.value), alpha);
    }
  }

  function drawTicks(ctx) {
    ctx.save();
    ctx.strokeStyle = "#40505d";
    ctx.lineWidth = 1;
    ctx.font = "12px system-ui, sans-serif";
    ctx.fillStyle = "#9baab4";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    for (let hour = 0; hour < 24; hour += 1) {
      const outer = polar(219, hour * 60);
      const inner = polar(hour % 6 === 0 ? 203 : 211, hour * 60);
      ctx.beginPath();
      ctx.moveTo(inner.x, inner.y);
      ctx.lineTo(outer.x, outer.y);
      ctx.stroke();
    }
    for (const [label, minute] of [["00", 0], ["06", 360], ["12", 720], ["18", 1080]]) {
      const p = polar(174, minute);
      ctx.fillText(label, p.x, p.y);
    }
    ctx.restore();
  }

  function drawSilverRing(ctx) {
    ctx.save();
    const outer = ctx.createRadialGradient(CX - 30, CY - 38, 24, CX, CY, 112);
    outer.addColorStop(0, "#ffffff");
    outer.addColorStop(0.2, "#d8dde0");
    outer.addColorStop(0.48, "#8e979d");
    outer.addColorStop(0.72, "#f4f6f5");
    outer.addColorStop(1, "#6b747a");
    ctx.fillStyle = outer;
    ctx.beginPath();
    ctx.arc(CX, CY, 68, 0, TAU);
    ctx.arc(CX, CY, 39, 0, TAU, true);
    ctx.fill("evenodd");

    ctx.strokeStyle = "rgba(255,255,255,0.88)";
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.arc(CX - 4, CY - 5, 59, Math.PI * 1.08, Math.PI * 1.78);
    ctx.stroke();

    ctx.strokeStyle = "rgba(30,36,40,0.52)";
    ctx.lineWidth = 5;
    ctx.beginPath();
    ctx.arc(CX + 3, CY + 6, 44, Math.PI * 0.05, Math.PI * 0.86);
    ctx.stroke();
    ctx.restore();
  }

  function drawNowHand(ctx, minute) {
    const outer = polar(218, minute);
    const inner = polar(182, minute);
    ctx.save();
    ctx.strokeStyle = "#eef3f6";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(inner.x, inner.y);
    ctx.lineTo(outer.x, outer.y);
    ctx.stroke();
    ctx.fillStyle = "#eef3f6";
    ctx.beginPath();
    ctx.arc(outer.x, outer.y, 4, 0, TAU);
    ctx.fill();
    ctx.restore();
  }

  function drawStressHistory(ctx, data, summary) {
    drawSegmentSeries(ctx, data.stress, 211, 16, 30, colorForStress, (v) => 0.5 + clamp(v / 100, 0, 1) * 0.5);
    drawArcBand(ctx, 189, 5, 0, 1440, "#1e2932", 0.88);
    for (const sleep of data.sleep) {
      if (sleep.end < sleep.start) {
        drawArcBand(ctx, 189, 5, sleep.start, 1440, colorForSleep(sleep.stage), 1);
        drawArcBand(ctx, 189, 5, 0, sleep.end, colorForSleep(sleep.stage), 1);
      } else {
        drawArcBand(ctx, 189, 5, sleep.start, sleep.end, colorForSleep(sleep.stage), 1);
      }
    }
    const nowStart = Math.max(0, data.nowMinute - 45);
    const nowEnd = Math.min(1440, data.nowMinute + 15);
    drawArcBand(ctx, 229, 4, nowStart, nowEnd, colorForStress(summary.score), 1);
    drawTangentLabel(ctx, "24H STRESS", 231, 1080, "#b8c4ca");
    drawTangentLabel(ctx, "SLEEP", 188, 690, "#6aa6d9");
  }

  function drawMemojiAvatar(ctx, x, y, summary) {
    const tone = colorForStress(summary.score);
    const tension = summary.avatar.measuredTension;
    const hrvTension = summary.avatar.hrvTension;
    const brow = summary.avatar.brow;
    const mouth = summary.avatar.mouth;
    const halo = ctx.createRadialGradient(x - 12, y - 22, 20, x, y, 78);
    halo.addColorStop(0, "rgba(255,255,255,0.18)");
    halo.addColorStop(0.42, `${tone}55`);
    halo.addColorStop(1, "rgba(2,4,6,0)");

    ctx.save();
    ctx.fillStyle = halo;
    ctx.beginPath();
    ctx.arc(x, y, 82, 0, TAU);
    ctx.fill();

    ctx.strokeStyle = "#20333d";
    ctx.lineWidth = 10;
    ctx.beginPath();
    ctx.arc(x, y, 72, 0, TAU);
    ctx.stroke();
    ctx.strokeStyle = tone;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.arc(x, y, 72, -Math.PI / 2, -Math.PI / 2 + TAU * summary.score / 100);
    ctx.stroke();

    ctx.fillStyle = "#3a2822";
    ctx.beginPath();
    ctx.ellipse(x, y - 14, 54, 57, 0, Math.PI, TAU);
    ctx.fill();

    const skin = ctx.createRadialGradient(x - 18, y - 22, 12, x, y + 6, 58);
    skin.addColorStop(0, "#ffe3c5");
    skin.addColorStop(0.5, "#efbd96");
    skin.addColorStop(1, "#c98565");
    ctx.fillStyle = skin;
    ctx.beginPath();
    ctx.ellipse(x, y + 3, 48, 52, 0, 0, TAU);
    ctx.fill();

    ctx.fillStyle = "#3a2822";
    ctx.beginPath();
    ctx.ellipse(x, y - 38, 50, 22, 0, Math.PI, TAU);
    ctx.fill();
    ctx.beginPath();
    ctx.arc(x - 31, y - 21, 20, Math.PI * 0.8, Math.PI * 1.7);
    ctx.arc(x + 31, y - 21, 20, Math.PI * 1.3, Math.PI * 0.2, true);
    ctx.fill();

    ctx.fillStyle = "rgba(255,136,122,0.22)";
    ctx.beginPath();
    ctx.ellipse(x - 27, y + 11, 10, 5, -0.1, 0, TAU);
    ctx.ellipse(x + 27, y + 11, 10, 5, 0.1, 0, TAU);
    ctx.fill();

    ctx.strokeStyle = "#2b1f1a";
    ctx.lineWidth = 4;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(x - 31, y - 16 - brow * 8);
    ctx.lineTo(x - 11, y - 13 + brow * 4);
    ctx.moveTo(x + 11, y - 13 + brow * 4);
    ctx.lineTo(x + 31, y - 16 - brow * 8);
    ctx.stroke();

    ctx.fillStyle = "#151c22";
    ctx.beginPath();
    ctx.ellipse(x - 20, y - 2, 5, 7 - tension * 2, 0, 0, TAU);
    ctx.ellipse(x + 20, y - 2, 5, 7 - tension * 2, 0, 0, TAU);
    ctx.fill();
    ctx.fillStyle = "#ffffff";
    ctx.beginPath();
    ctx.arc(x - 18, y - 5, 1.5, 0, TAU);
    ctx.arc(x + 22, y - 5, 1.5, 0, TAU);
    ctx.fill();

    if (hrvTension > 0.25) {
      ctx.strokeStyle = `rgba(80,145,220,${0.3 + hrvTension * 0.35})`;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(x - 20, y + 10, 9, 0.1, Math.PI - 0.1);
      ctx.arc(x + 20, y + 10, 9, 0.1, Math.PI - 0.1);
      ctx.stroke();
    }

    ctx.strokeStyle = "#743c36";
    ctx.lineWidth = 4;
    ctx.lineCap = "round";
    ctx.beginPath();
    const smile = mouth * 18;
    ctx.moveTo(x - 20, y + 29);
    ctx.quadraticCurveTo(x, y + 29 + smile, x + 20, y + 29);
    ctx.stroke();

    if (summary.avatar.sweat) {
      ctx.fillStyle = "#8fd4ff";
      ctx.beginPath();
      ctx.moveTo(x + 42, y - 8);
      ctx.quadraticCurveTo(x + 53, y + 8, x + 42, y + 17);
      ctx.quadraticCurveTo(x + 32, y + 8, x + 42, y - 8);
      ctx.fill();
    }

    if (summary.avatar.mismatch) {
      ctx.fillStyle = "#f2d66b";
      ctx.beginPath();
      ctx.arc(x + 52, y - 42, 10, 0, TAU);
      ctx.fill();
      ctx.fillStyle = "#111922";
      ctx.font = "900 12px system-ui, sans-serif";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText("!", x + 52, y - 42);
    }

    if (summary.familyHigh) {
      ctx.fillStyle = "#efbd96";
      ctx.beginPath();
      ctx.arc(x - 54, y + 42, 15, 0, TAU);
      ctx.fill();
      ctx.strokeStyle = "#e86f61";
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.arc(x - 54, y + 42, 18, 0, TAU);
      ctx.stroke();
      ctx.fillStyle = "#151c22";
      ctx.beginPath();
      ctx.arc(x - 59, y + 39, 1.7, 0, TAU);
      ctx.arc(x - 49, y + 39, 1.7, 0, TAU);
      ctx.fill();
      ctx.strokeStyle = "#743c36";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(x - 54, y + 49, 6, Math.PI * 1.12, Math.PI * 1.88);
      ctx.stroke();
    }

    ctx.restore();
  }

  function drawCore(ctx, data, summary) {
    const tone = colorForStress(summary.score);
    ctx.save();
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    drawArcText(ctx, `${data.owner.toUpperCase()} ${data.ring}`, 148, 0, {
      font: "700 13px system-ui, sans-serif",
      color: "#eef3f6",
      letterSpacing: 0.4,
    });

    drawMemojiAvatar(ctx, CX, CY - 6, summary);

    ctx.fillStyle = "#eef3f6";
    ctx.font = "800 18px system-ui, sans-serif";
    ctx.textAlign = "right";
    ctx.fillText(summary.stateLabel, CX + 14, CY + 82);
    ctx.fillStyle = tone;
    ctx.font = "900 24px system-ui, sans-serif";
    ctx.textAlign = "left";
    ctx.fillText(String(summary.score), CX + 20, CY + 82);
    ctx.textAlign = "center";
    ctx.font = "700 10px system-ui, sans-serif";
    ctx.fillStyle = "#b7c3ca";
    ctx.fillText(`${summary.trend >= 0 ? "+" : ""}${summary.trend}/30M`, CX, CY + 103);

    drawArcText(ctx, summary.action.toUpperCase(), 125, 720, {
      font: "800 12px system-ui, sans-serif",
      color: "#eef3f6",
      letterSpacing: 0.5,
      flip: true,
    });
    ctx.restore();
  }

  function chipColors(chip) {
    const warn = chip.tone === "warn";
    const context = chip.tone === "context";
    return {
      fill: warn ? "rgba(232,111,97,0.2)" : context ? "rgba(227,161,76,0.13)" : "rgba(72,201,167,0.13)",
      stroke: warn ? "#e86f61" : context ? "#e3a14c" : "#48c9a7",
      value: warn ? "#ffd0ca" : context ? "#ffe6bc" : "#dff6ed",
    };
  }

  function drawArcChip(ctx, chip, radius, centerMinute, spanMinutes = 88) {
    const colors = chipColors(chip);
    const flip = centerMinute >= 360 && centerMinute <= 1080;

    ctx.save();
    ctx.lineCap = "round";
    ctx.strokeStyle = colors.fill;
    ctx.lineWidth = 22;
    ctx.beginPath();
    ctx.arc(CX, CY, radius, minuteToAngle(centerMinute - spanMinutes / 2), minuteToAngle(centerMinute + spanMinutes / 2));
    ctx.stroke();

    ctx.strokeStyle = colors.stroke;
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.arc(CX, CY, radius - 11, minuteToAngle(centerMinute - spanMinutes / 2), minuteToAngle(centerMinute + spanMinutes / 2));
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(CX, CY, radius + 11, minuteToAngle(centerMinute - spanMinutes / 2), minuteToAngle(centerMinute + spanMinutes / 2));
    ctx.stroke();
    ctx.restore();

    drawArcText(ctx, chip.label, radius - 5, centerMinute, {
      font: "700 7px system-ui, sans-serif",
      color: "#8fa0aa",
      letterSpacing: 0.1,
      flip,
    });
    drawArcText(ctx, chip.value, radius + 5, centerMinute, {
      font: "800 9px system-ui, sans-serif",
      color: colors.value,
      letterSpacing: 0.1,
      flip,
    });
  }

  function drawEvidence(ctx, summary) {
    const chips = summary.chips;
    const placements = [
      { radius: 154, minute: 875 },
      { radius: 154, minute: 965 },
      { radius: 154, minute: 1055 },
      { radius: 154, minute: 385 },
      { radius: 154, minute: 475 },
      { radius: 154, minute: 565 },
    ];
    chips.forEach((chip, index) => {
      drawArcChip(ctx, chip, placements[index].radius, placements[index].minute);
    });
  }

  function draw(canvas, data = makeDemoData()) {
    const ctx = canvas.getContext("2d");
    ctx.clearRect(0, 0, SIZE, SIZE);

    const bg = ctx.createRadialGradient(CX, CY, 40, CX, CY, 238);
    bg.addColorStop(0, "#111922");
    bg.addColorStop(0.63, "#071016");
    bg.addColorStop(1, "#020406");
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, SIZE, SIZE);

    const summary = stressSummary(data);
    drawTicks(ctx);
    drawStressHistory(ctx, data, summary);
    drawCore(ctx, data, summary);
    drawEvidence(ctx, summary);
    drawNowHand(ctx, data.nowMinute);
  }

  function frame() {
    if (!state.active) {
      state.raf = 0;
      return;
    }
    const canvas = document.querySelector("#astrolabe-canvas");
    if (canvas) {
      state.data = makeDemoData(new Date());
      draw(canvas, state.data);
    }
    state.raf = window.requestAnimationFrame(frame);
  }

  function setActive(active) {
    state.active = Boolean(active);
    if (state.active && !state.raf) {
      state.raf = window.requestAnimationFrame(frame);
    }
  }

  function install({ select, setFirmwareFace }) {
    state.select = select;
    state.setFirmwareFace = setFirmwareFace;
    if (!select || select.querySelector(`option[value="${SPECIAL_FACE_VALUE}"]`)) {
      return;
    }
    const option = document.createElement("option");
    option.value = SPECIAL_FACE_VALUE;
    option.textContent = "R10 24h Ring Metrics";
    select.append(option);
  }

  window.AstrolabeRingFaceSim = {
    SPECIAL_FACE_VALUE,
    install,
    setActive,
    draw,
    makeDemoData,
    makeScenarioData,
    stressSummary,
    cyclePhase,
    cycleContext,
    minuteToAngle,
    colorForStress,
    colorForHrv,
  };
})();
