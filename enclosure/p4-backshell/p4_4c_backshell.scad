// Waveshare ESP32-P4-WIFI6-Touch-LCD-4C backshell
// Units: millimeters. Source dimensions from Waveshare's 4C mechanical drawings.
//
// Print orientation: flat back on the bed, open circular lip facing up.
// This is intentionally parametric: tune the connector cutouts after one test print.

$fn = 72;

// Published 4C dimensions
glass_od = 126.00;
visible_lcd_od = 101.52;
pcb_w = 85.50;
pcb_h = 65.00;
published_stack_depth = 15.00;

// Case fit and print tuning
glass_clearance = 0.85;
wall = 2.60;
back_thickness = 2.20;
case_depth = 24.00;
front_lip_depth = 4.00;
corner_radius = 7.00;

// Mount/post tuning. Approximate from Waveshare drawing; measure and tune if needed.
post_od = 6.40;
screw_clearance = 2.35;       // M2 clearance, or self-tapping pilot for small screws
post_h = 8.50;
post_locs = [
  [-36.2,  21.5],
  [ 36.2,  21.5],
  [-36.2, -21.5],
  [ 36.2, -21.5]
];

// Speaker grille for a 40 mm 8 ohm speaker, optional.
enable_speaker_grille = false;
speaker_center = [-27, -4];
speaker_cutout_d = 36.0;
speaker_ring_od = 44.0;
speaker_ring_id = 40.8;

// Closed-shell cutout policy. Keep these false for a product-like enclosure.
enable_usb_power_cutout = false;
enable_usb_uart_cutout = false;
enable_usb_otg_cutout = false;
enable_camera_cutout = false;
enable_sd_cutout = false;
enable_button_pinhole = false;
enable_speaker_wire_exit = false;

// Qi receiver mounting. Typical 5 V receiver coils are 40-50 mm OD with a small
// receiver PCB. Keep the coil against the plastic back and away from speaker magnets.
enable_qi_mount = true;
qi_coil_center = [26, -2];
qi_coil_od = 48.0;
qi_coil_relief_depth = 0.80;
qi_pcb_center = [25, 31];
qi_pcb_w = 27.0;
qi_pcb_h = 16.0;
qi_pcb_clip_w = 4.0;
qi_pcb_clip_h = 4.0;

// Dual microphone acoustic ports. Positions are approximate from the Waveshare 4C drawing.
// Tune mic_locs after test-fitting against MIC1/MIC2 on the lower PCB edge.
enable_mic_ports = false;
mic_port_d = 2.40;
mic_channel_w = 4.00;
mic_channel_h = 3.20;
mic_locs = [
  [-35.0, -31.0],
  [ 35.0, -31.0]
];

module rounded_rect_2d(w, h, r) {
  hull() {
    translate([ w / 2 - r,  h / 2 - r]) circle(r = r);
    translate([-w / 2 + r,  h / 2 - r]) circle(r = r);
    translate([ w / 2 - r, -h / 2 + r]) circle(r = r);
    translate([-w / 2 + r, -h / 2 + r]) circle(r = r);
  }
}

module rounded_box(w, h, z, r) {
  linear_extrude(height = z) rounded_rect_2d(w, h, r);
}

module cup_shell() {
  outer_d = glass_od + glass_clearance * 2 + wall * 2;
  inner_d = glass_od + glass_clearance * 2;
  difference() {
    cylinder(d = outer_d, h = case_depth);
    translate([0, 0, back_thickness]) cylinder(d = inner_d, h = case_depth + 0.2);

    // Rectangular PCB air volume behind the round display stack.
    translate([0, 0, back_thickness + 0.4])
      rounded_box(pcb_w + 4.0, pcb_h + 4.0, case_depth, corner_radius);

    // Small relief around the visible display back so the shell does not preload glass.
    translate([0, 0, case_depth - front_lip_depth])
      cylinder(d = visible_lcd_od + 8.0, h = front_lip_depth + 0.3);
  }
}

module screw_posts() {
  for (p = post_locs) {
    translate([p[0], p[1], back_thickness - 0.15])
      cylinder(d = post_od, h = post_h + 0.15);
  }
}

module speaker_ring() {
  if (enable_speaker_grille) {
    translate([speaker_center[0], speaker_center[1], back_thickness - 0.15])
      difference() {
        cylinder(d = speaker_ring_od, h = 2.15);
        translate([0, 0, -0.1]) cylinder(d = speaker_ring_id, h = 2.45);
      }
  }
}

module qi_mount_features() {
  if (enable_qi_mount) {
    // Shallow coil pocket: leaves a thin plastic window for better coupling.
    translate([qi_coil_center[0], qi_coil_center[1], back_thickness])
      difference() {
        cylinder(d = qi_coil_od + 3.0, h = 1.0);
        translate([0, 0, -0.1]) cylinder(d = qi_coil_od + 0.4, h = 1.3);
      }

    // Low receiver PCB retainers, sized as snap/tape stops rather than hard clamps.
    for (x = [-1, 1]) {
      translate([qi_pcb_center[0] + x * (qi_pcb_w / 2 + qi_pcb_clip_w / 2),
                 qi_pcb_center[1],
                 back_thickness])
        cube([qi_pcb_clip_w, qi_pcb_h + 3.0, qi_pcb_clip_h]);
    }
    translate([qi_pcb_center[0], qi_pcb_center[1] - qi_pcb_h / 2 - qi_pcb_clip_w / 2, back_thickness])
      cube([qi_pcb_w + 4.0, qi_pcb_clip_w, qi_pcb_clip_h]);
  }
}

module screw_holes() {
  for (p = post_locs) {
    translate([p[0], p[1], -0.1])
      cylinder(d = screw_clearance, h = back_thickness + post_h + 0.5);
  }
}

module qi_coil_relief() {
  if (enable_qi_mount) {
    // Reduce back thickness under the coil without making an outside hole.
    translate([qi_coil_center[0], qi_coil_center[1], back_thickness - qi_coil_relief_depth])
      cylinder(d = qi_coil_od, h = qi_coil_relief_depth + 0.4);
  }
}

module speaker_grille_holes() {
  if (enable_speaker_grille) {
    // Simple slot grille; much faster to render than dozens of small cylinders.
    for (x = [-14 : 7 : 14]) {
      translate([speaker_center[0] + x, speaker_center[1], back_thickness / 2])
        cube([2.6, speaker_cutout_d * 0.72, back_thickness + 0.8], center = true);
    }
  }
}

module mic_acoustic_channels() {
  if (enable_mic_ports) {
    for (p = mic_locs) {
      // Direct rear acoustic port through the back wall.
      translate([p[0], p[1], -0.2])
        cylinder(d = mic_port_d, h = back_thickness + 0.8);

      // Short open duct toward the lower edge so sound is not trapped under the PCB.
      translate([p[0], p[1] - 8.0, back_thickness + mic_channel_h / 2])
        cube([mic_channel_w, 18.0, mic_channel_h], center = true);
    }
  }
}

module side_cutouts() {
  // Top edge: keep normal USB-C power/programming access, close everything else by default.
  if (enable_camera_cutout) {
    translate([-6, pcb_h / 2 + 18, 8.0]) cube([24, 24, 10], center = true);
  }
  if (enable_usb_power_cutout) {
    translate([24, pcb_h / 2 + 18, 8.0]) cube([14, 22, 9], center = true);
  }
  if (enable_usb_uart_cutout) {
    translate([47, pcb_h / 2 + 18, 8.0]) cube([14, 22, 9], center = true);
  }

  if (enable_usb_otg_cutout) {
    translate([-pcb_w / 2 - 18, 9, 8.0]) cube([24, 18, 12], center = true);
  }
  if (enable_speaker_wire_exit) {
    translate([-pcb_w / 2 - 18, -19, 6.0]) cube([18, 8, 7], center = true);
  }

  if (enable_sd_cutout) {
    translate([pcb_w / 2 + 18, 5, 7.5]) cube([24, 18, 9], center = true);
  }
  if (enable_button_pinhole) {
    // Tool-access pinholes for BOOT/RESET instead of large exposed slots.
    translate([pcb_w / 2 + 17.5, 23.0, 7.0]) rotate([0, 90, 0]) cylinder(d = 2.2, h = 7.0);
    translate([pcb_w / 2 + 17.5, -17.0, 7.0]) rotate([0, 90, 0]) cylinder(d = 2.2, h = 7.0);
  }
}

difference() {
  union() {
    cup_shell();
    screw_posts();
    speaker_ring();
    qi_mount_features();
  }
  qi_coil_relief();
  screw_holes();
  speaker_grille_holes();
  mic_acoustic_channels();
  side_cutouts();
}
