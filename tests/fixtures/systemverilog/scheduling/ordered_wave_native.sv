`timescale 1ns/1ns
module ordered_wave_member #(parameter W = 1);
    reg [W-1:0] stimulus;
    wire [W-1:0] complement_value = ~stimulus;
    wire [W-1:0] xor_value = stimulus ^ {W{1'b1}};
    wire [W-1:0] or_value = stimulus | {W{1'b1}};
    wire [W-1:0] and_value = stimulus & {W{1'b0}};
    initial begin
        stimulus = '0;
        #1 stimulus = '1;
        #1;
        if (complement_value !== {W{1'b0}} || xor_value !== {W{1'b0}}
            || or_value !== {W{1'b1}} || and_value !== {W{1'b0}})
            $fatal(1, "ordered known-value publication failed width=%0d", W);
        stimulus = 'x;
        #1;
        if (complement_value !== {W{1'bx}} || xor_value !== {W{1'bx}}
            || or_value !== {W{1'b1}} || and_value !== {W{1'b0}})
            $fatal(1, "ordered unknown-value publication failed width=%0d", W);
        stimulus = 'z;
        #1;
        if (complement_value !== {W{1'bx}} || xor_value !== {W{1'bx}}
            || or_value !== {W{1'b1}} || and_value !== {W{1'b0}})
            $fatal(1, "ordered high-impedance publication failed width=%0d", W);
    end
endmodule
module ordered_wave_native;
    ordered_wave_member #(1) scalar_case();
    ordered_wave_member #(65) wide65_case();
    ordered_wave_member #(129) wide129_case();
    ordered_wave_member #(256) wide256_case();
    ordered_wave_member #(1024) wide1024_case();
    initial begin
        #5;
        $display("ORDERED native publication PASS");
        $finish;
    end
endmodule
