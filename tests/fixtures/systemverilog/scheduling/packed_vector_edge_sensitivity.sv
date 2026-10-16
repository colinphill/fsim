// SPDX-License-Identifier: Apache-2.0
`timescale 1ns/1ps
module packed_vector_edge_sensitivity;
    logic [3:0] descending = 4'b0000;
    logic [0:3] ascending = 4'b0000;
    integer descending_pos = 0;
    integer descending_neg = 0;
    integer ascending_pos = 0;
    integer ascending_neg = 0;
    integer descending_wait_pos = 0;
    integer descending_wait_neg = 0;
    integer ascending_wait_pos = 0;
    integer ascending_wait_neg = 0;
    integer descending_dynamic_wait_pos = 0;
    integer descending_dynamic_wait_neg = 0;

    always @(posedge descending)
        descending_pos = descending_pos + 1;
    always @(negedge descending)
        descending_neg = descending_neg + 1;
    always @(posedge ascending)
        ascending_pos = ascending_pos + 1;
    always @(negedge ascending)
        ascending_neg = ascending_neg + 1;

    initial begin
        @(posedge descending);
        descending_wait_pos = 1;
        @(negedge descending);
        descending_wait_neg = 1;
    end

    initial begin
        @(posedge ascending);
        ascending_wait_pos = 1;
        @(negedge ascending);
        ascending_wait_neg = 1;
    end

    // Keep re-arming dynamic waits so their four-state behavior is observed
    // independently from the static always blocks below.
    initial begin
        forever begin
            @(posedge descending);
            descending_dynamic_wait_pos = descending_dynamic_wait_pos + 1;
        end
    end

    initial begin
        forever begin
            @(negedge descending);
            descending_dynamic_wait_neg = descending_dynamic_wait_neg + 1;
        end
    end

    initial begin
        // Ignore declaration-time initialization; the checks below begin from
        // a stable all-zero vector in both packed range directions.
        #1;
        descending_pos = 0;
        descending_neg = 0;
        ascending_pos = 0;
        ascending_neg = 0;
        descending_wait_pos = 0;
        descending_wait_neg = 0;
        ascending_wait_pos = 0;
        ascending_wait_neg = 0;
        descending_dynamic_wait_pos = 0;
        descending_dynamic_wait_neg = 0;

        // The leftmost bit changes while the LSB remains zero. Neither edge
        // event may trigger from this non-LSB-only transition.
        descending = 4'b1000;
        ascending = 4'b1000;
        #1;
        if (descending_pos != 0 || descending_neg != 0
            || ascending_pos != 0 || ascending_neg != 0
            || descending_wait_pos != 0 || descending_wait_neg != 0
            || ascending_wait_pos != 0 || ascending_wait_neg != 0
            || descending_dynamic_wait_pos != 0
            || descending_dynamic_wait_neg != 0)
            $fatal(1, "non-LSB packed-vector transition triggered an edge");

        descending = 4'b1001;
        ascending = 4'b1001;
        #1;
        if (descending_pos != 1 || descending_neg != 0
            || ascending_pos != 1 || ascending_neg != 0
            || descending_wait_pos != 1 || descending_wait_neg != 0
            || ascending_wait_pos != 1 || ascending_wait_neg != 0
            || descending_dynamic_wait_pos != 1
            || descending_dynamic_wait_neg != 0)
            $fatal(1, "packed-vector LSB rising edge was not detected");

        // The non-LSB bit falls while the LSB stays one.
        descending = 4'b0001;
        ascending = 4'b0001;
        #1;
        if (descending_pos != 1 || descending_neg != 0
            || ascending_pos != 1 || ascending_neg != 0
            || descending_wait_pos != 1 || descending_wait_neg != 0
            || ascending_wait_pos != 1 || ascending_wait_neg != 0
            || descending_dynamic_wait_pos != 1
            || descending_dynamic_wait_neg != 0)
            $fatal(1, "non-LSB falling transition triggered an edge");

        descending[0] = 1'b0;
        ascending[3] = 1'b0;
        #1;
        if (descending_pos != 1 || descending_neg != 1
            || ascending_pos != 1 || ascending_neg != 1
            || descending_wait_pos != 1 || descending_wait_neg != 1
            || ascending_wait_pos != 1 || ascending_wait_neg != 1
            || descending_dynamic_wait_pos != 1
            || descending_dynamic_wait_neg != 1)
            $fatal(1, "packed-vector LSB falling edge was not detected");

        // IEEE 1800-2017 9.4.2's four-state edge table applies to the LSB.
        descending[0] = 1'bx;
        ascending[3] = 1'bx;
        #1;
        if (descending_pos != 2 || descending_neg != 1
            || ascending_pos != 2 || ascending_neg != 1
            || descending_dynamic_wait_pos != 2
            || descending_dynamic_wait_neg != 1)
            $fatal(1, "0-to-X LSB transition was not a posedge");

        descending[0] = 1'bz;
        ascending[3] = 1'bz;
        #1;
        if (descending_pos != 2 || descending_neg != 1
            || ascending_pos != 2 || ascending_neg != 1
            || descending_dynamic_wait_pos != 2
            || descending_dynamic_wait_neg != 1)
            $fatal(1, "X-to-Z LSB transition incorrectly triggered an edge");

        descending[0] = 1'b1;
        ascending[3] = 1'b1;
        #1;
        if (descending_pos != 3 || descending_neg != 1
            || ascending_pos != 3 || ascending_neg != 1
            || descending_dynamic_wait_pos != 3
            || descending_dynamic_wait_neg != 1)
            $fatal(1, "Z-to-1 LSB transition was not a posedge");

        descending[0] = 1'bz;
        ascending[3] = 1'bz;
        #1;
        if (descending_pos != 3 || descending_neg != 2
            || ascending_pos != 3 || ascending_neg != 2
            || descending_dynamic_wait_pos != 3
            || descending_dynamic_wait_neg != 2)
            $fatal(1, "1-to-Z LSB transition was not a negedge");

        descending[0] = 1'b0;
        ascending[3] = 1'b0;
        #1;
        if (descending_pos != 3 || descending_neg != 3
            || ascending_pos != 3 || ascending_neg != 3
            || descending_dynamic_wait_pos != 3
            || descending_dynamic_wait_neg != 3)
            $fatal(1, "Z-to-0 LSB transition was not a negedge");

        descending[0] = 1'bz;
        ascending[3] = 1'bz;
        #1;
        if (descending_pos != 4 || descending_neg != 3
            || ascending_pos != 4 || ascending_neg != 3
            || descending_dynamic_wait_pos != 4
            || descending_dynamic_wait_neg != 3)
            $fatal(1, "0-to-Z LSB transition was not a posedge");

        descending[0] = 1'bx;
        ascending[3] = 1'bx;
        #1;
        if (descending_pos != 4 || descending_neg != 3
            || ascending_pos != 4 || ascending_neg != 3
            || descending_dynamic_wait_pos != 4
            || descending_dynamic_wait_neg != 3)
            $fatal(1, "Z-to-X LSB transition incorrectly triggered an edge");

        descending[0] = 1'b1;
        ascending[3] = 1'b1;
        #1;
        if (descending_pos != 5 || descending_neg != 3
            || ascending_pos != 5 || ascending_neg != 3
            || descending_dynamic_wait_pos != 5
            || descending_dynamic_wait_neg != 3)
            $fatal(1, "X-to-1 LSB transition was not a posedge");

        descending[0] = 1'bx;
        ascending[3] = 1'bx;
        #1;
        if (descending_pos != 5 || descending_neg != 4
            || ascending_pos != 5 || ascending_neg != 4
            || descending_dynamic_wait_pos != 5
            || descending_dynamic_wait_neg != 4)
            $fatal(1, "1-to-X LSB transition was not a negedge");

        descending[0] = 1'b0;
        ascending[3] = 1'b0;
        #1;
        if (descending_pos != 5 || descending_neg != 5
            || ascending_pos != 5 || ascending_neg != 5
            || descending_dynamic_wait_pos != 5
            || descending_dynamic_wait_neg != 5)
            $fatal(1, "X-to-0 LSB transition was not a negedge");

        descending[0] = 1'bx;
        ascending[3] = 1'bx;
        #1;
        if (descending_pos != 6 || descending_neg != 5
            || ascending_pos != 6 || ascending_neg != 5
            || descending_dynamic_wait_pos != 6
            || descending_dynamic_wait_neg != 5)
            $fatal(1, "0-to-X LSB transition was not a posedge");

        descending[0] = 1'bz;
        ascending[3] = 1'bz;
        #1;
        if (descending_pos != 6 || descending_neg != 5
            || ascending_pos != 6 || ascending_neg != 5
            || descending_dynamic_wait_pos != 6
            || descending_dynamic_wait_neg != 5)
            $fatal(1, "X-to-Z LSB transition incorrectly triggered an edge");

        descending[0] = 1'b0;
        ascending[3] = 1'b0;
        #1;
        if (descending_pos != 6 || descending_neg != 6
            || ascending_pos != 6 || ascending_neg != 6
            || descending_dynamic_wait_pos != 6
            || descending_dynamic_wait_neg != 6)
            $fatal(1, "Z-to-0 LSB transition was not a negedge");

        $display(
            "VECTOR EDGE pos=%0d neg=%0d ascending_pos=%0d ascending_neg=%0d wait_pos=%0d wait_neg=%0d ascending_wait_pos=%0d ascending_wait_neg=%0d dynamic_wait_pos=%0d dynamic_wait_neg=%0d",
            descending_pos, descending_neg, ascending_pos, ascending_neg,
            descending_wait_pos, descending_wait_neg,
            ascending_wait_pos, ascending_wait_neg,
            descending_dynamic_wait_pos, descending_dynamic_wait_neg);
        $finish;
    end
endmodule
