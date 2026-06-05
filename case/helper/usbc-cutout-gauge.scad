// ============================================================
//  USB-C Cutout Gauge
//  Matches the oval hole cut in xiao-tub.scad exactly.
//  Print this to verify the cutout fits your USB-C connector
//  before printing the full tub.
//
//  Dimensions (from xiao-tub.scad):
//    Width:  usbc_hole_w = 8.94 + 1.4 = 10.34 mm
//    Height: usbc_conn_h = 4.00 mm  (radius = 2.00 mm)
//    Shape:  stadium (hull of two circles)
// ============================================================

// Mirror of xiao-tub.scad values — keep in sync if tub changes
usbc_conn_w  = 8.94;
usbc_conn_h  = 4;
usbc_hole_r  = usbc_conn_h / 2;
usbc_hole_w  = usbc_conn_w + 1.4;

gauge_depth  = 10.0;   // extrusion thickness of the gauge

linear_extrude(height = gauge_depth)
    hull() {
        translate([ usbc_hole_w/2 - usbc_hole_r, 0])
            circle(r = usbc_hole_r, $fn = 64);
        translate([-(usbc_hole_w/2 - usbc_hole_r), 0])
            circle(r = usbc_hole_r, $fn = 64);
    }
