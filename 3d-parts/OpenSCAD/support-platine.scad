/* Support platine */

ANGLE=39.82;

PCB_WIDTH=77.5;
PCB_HEIGHT=72;
PCB_THICK=1.8;

ENTRETOISE_HEIGHT=75;
ENTRETOISE_WIDTH=ENTRETOISE_HEIGHT * tan(ANGLE);

module PCB(){
    cube([PCB_WIDTH, PCB_THICK, PCB_HEIGHT]);
}

module PCB_hole(){
    cube([PCB_WIDTH - 2.4, 6, PCB_HEIGHT]);
}

module entretoise()
{
    path = [
        [0, 0],
        [ENTRETOISE_WIDTH, 0],
        [ENTRETOISE_WIDTH, ENTRETOISE_HEIGHT]
    ];


    difference()
    {
        linear_extrude(height=3)
        {
            polygon(path);
        }

        union()
        {
            rotate([0,0,90-ANGLE]) translate([10, -PCB_THICK-2,-1]) PCB();
            rotate([0, 0, 90-ANGLE]) translate([12, -PCB_THICK-1, -1]) PCB_hole();
        }
        
        rotate([0,0,90-ANGLE]) translate([10 + PCB_WIDTH+3, -10, -1]) cube([10, 20, 5]);
        translate([0,-.1,-.1]) cube([ENTRETOISE_WIDTH - 60, 6.1, 3.2]);
    }
}

entretoise();

//translate([0,0,50]) entretoise();
//rotate([0,0,90-ANGLE]) translate([10, -PCB_THICK-2,-10]) color("red") PCB();







