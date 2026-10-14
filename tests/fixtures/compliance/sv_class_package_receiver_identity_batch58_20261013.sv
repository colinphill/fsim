// fsim: top=class_package_receiver_identity_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0

package identity_pkg_a;
    class C;
        int value;

        task touch;
            value = 17;
        endtask

        function int read;
            return value;
        endfunction
    endclass

    C obj = new;

    class Argumented;
        int value;

        function new(int initial_value);
            value = initial_value;
        endfunction

        function int read;
            return value;
        endfunction
    endclass

    typedef Argumented ArgumentedHandle;
    ArgumentedHandle argumented_obj = new(41);
    ArgumentedHandle null_obj = null;
    Argumented direct_obj;
endpackage

package identity_pkg_b;
    class C;
        function int read;
            return 29;
        endfunction
    endclass

    C obj = new;
endpackage

module class_package_receiver_identity_batch58;
    import identity_pkg_a::*;

    initial begin
        obj.touch();
        if (obj.value !== 17) begin
            $fatal(1, "imported package class property read failed");
        end
        if (obj.read() !== 17) begin
            $fatal(1, "imported package class task/function receiver failed");
        end
        if (argumented_obj == null) begin
            $fatal(1, "typedef class handle new(argument) was left null");
        end
        direct_obj = argumented_obj;
        if (direct_obj.read() !== 41) begin
            $fatal(1, "typedef class handle new(argument) lost its value");
        end
        if (null_obj != null) begin
            $fatal(1, "package class handle null initializer was not null");
        end
        if (identity_pkg_b::obj.read() !== 29) begin
            $fatal(1, "qualified package class identity was confused");
        end
        $display("PASS");
        $finish;
    end
endmodule
