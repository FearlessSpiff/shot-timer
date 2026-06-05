// ============================================================
//  Seeed XIAO nRF52840 — MCU Tub / Cradle
//  Separate part to position freely alongside body.scad
//
//  Board dimensions (official):
//    PCB:       21.5 mm (L) × 17.5 mm (W) × 1.0 mm (thick)
//    Total H:   ~3.5 mm incl. components on top
//    USB-C:     on the +Y short end
//               connector body: 8.94 mm wide × 3.26 mm tall
//
//  Tub orientation:
//    X = width  (17.5 mm direction)
//    Y = length (21.5 mm, USB-C end at +Y)
//    Z = height (opens upward, board drops in from top)
// ============================================================

// --- Board dimensions ---
board_l       = 21.5;   // PCB length  (Y)
board_w       = 17.5;   // PCB width   (X)
board_h       = 1.0;    // PCB thickness
comp_h        = 2.5;    // tallest components above PCB (excl. USB-C)
tol           = 0.3;    // clearance around board on each side

// --- Tub wall & floor (defined early so other vars can use them) ---
wall          = 1.6;    // wall thickness
floor_t       = 1.2;    // floor thickness

// --- USB-C connector geometry ---
usbc_conn_w   = 8.94;              // connector width (IEC spec)
usbc_conn_h   = 4;             // connector height (typical SMD receptacle)
usbc_hole_r   = usbc_conn_h / 2;  // end radius fixed to connector height → keeps oval shape
usbc_hole_w   = usbc_conn_w + 1.4; // +1.4 mm total clearance
usbc_hole_h   = usbc_conn_h + 1.9; // +1.9 mm total clearance
// Z-center of connector measured from tub floor:
usbc_center_z = floor_t + board_h + usbc_conn_h / 2;
// Front wall height: floor + PCB + connector + 1.5 mm margin
front_wall_h  = floor_t + board_h + usbc_conn_h + 1.5;

// --- Battery solder cutout (-Y back wall) ---
batt_w        = 7.0;   // width — covers BAT+/BAT- pads
batt_h        = 3.5;   // height — clears PCB + some room
batt_z        = floor_t; // starts at tub floor level

// --- Derived internals ---
inner_l   = board_l + 2 * tol;
inner_w   = board_w + 2 * tol;
inner_h   = board_h + comp_h;

outer_l   = inner_l + 2 * wall;
outer_w   = inner_w + 2 * wall;
outer_h   = floor_t + inner_h;   // height of back/side walls (open top)

// --- Ledge parameters ---
ledge_t   = 1.0;   // protrusion into pocket
ledge_h   = board_h;
ledge_len = 5.0;
corner_gap = 2.0;

module xiao_tub() {
    difference() {
        union() {
            // Base box (back + side walls at normal height)
            cube([outer_w, outer_l, outer_h]);

            // Raised front wall (+Y) to enclose USB-C connector
            translate([0, outer_l - wall, 0])
                cube([outer_w, wall, front_wall_h]);
        }

        // --- Inner pocket ---
        translate([wall, wall, floor_t])
            cube([inner_w, inner_l, inner_h + 1]);

        // --- USB-C oval hole in raised front wall ---
        // rotate([90,0,0]) maps +Z → -Y, so starting at outer_l+1 cuts back through the wall
        translate([outer_w / 2, outer_l + 1, usbc_center_z])
            rotate([90, 0, 0])
                linear_extrude(height = wall + 3)
                    hull() {
                        translate([ usbc_hole_w/2 - usbc_hole_r, 0])
                            circle(r = usbc_hole_r, $fn = 64);
                        translate([-(usbc_hole_w/2 - usbc_hole_r), 0])
                            circle(r = usbc_hole_r, $fn = 64);
                    }

        // --- Battery solder cutout on -Y (back) wall ---
        translate([(outer_w - batt_w) / 2, -0.01, batt_z])
            cube([batt_w, wall + 2, batt_h]);
    }

    // --- Board retaining ledges (all four sides) ---
    // -Y wall (back) — two ledges either side of battery cutout
    translate([wall + corner_gap, wall, floor_t])
        cube([ledge_len, ledge_t, ledge_h]);
    translate([wall + inner_w - corner_gap - ledge_len, wall, floor_t])
        cube([ledge_len, ledge_t, ledge_h]);

    // +Y wall (front) — two ledges either side of USB-C hole
    translate([wall + corner_gap, wall + inner_l - ledge_t, floor_t])
        cube([ledge_len, ledge_t, ledge_h]);
    translate([wall + inner_w - corner_gap - ledge_len, wall + inner_l - ledge_t, floor_t])
        cube([ledge_len, ledge_t, ledge_h]);

    // -X wall (left) — one ledge centered
    translate([wall, wall + (inner_l - ledge_len) / 2, floor_t])
        cube([ledge_t, ledge_len, ledge_h]);

    // +X wall (right) — one ledge centered
    translate([wall + inner_w - ledge_t, wall + (inner_l - ledge_len) / 2, floor_t])
        cube([ledge_t, ledge_len, ledge_h]);
}

xiao_tub();

// ============================================================
//  Usage:
//    use <xiao_tub.scad>
//    translate([X,Y,Z]) rotate([RX,RY,RZ]) xiao_tub();
//
//  Key dimensions:
//    Outer (back/sides): outer_w × outer_l × outer_h  ≈ 21.7 × 25.3 × 4.7 mm
//    Front wall height:  front_wall_h                 ≈ 6.7 mm
//    Inner pocket:       inner_w × inner_l            ≈ 18.1 × 22.1 mm
//    USB-C hole:         9.34 × 3.66 mm oval, centered on connector
//    Battery cutout:     7.0 × 3.5 mm, centered, -Y wall
// ============================================================
