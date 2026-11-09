// SPDX-License-Identifier: Apache-2.0
// The arguments of `extends B(args)` are the vpiArgument objects of the
// class's extends object (IEEE 1800-2023 8.17, 37.32).
module top;
    class Base;
        int a;
        function new(int x, int y);
            a = x + y;
        endfunction
    endclass

    class Derived extends Base(3, 4);
    endclass

    // Named arguments bind to the base constructor's formals (13.5.4):
    // listed in formal order, x then y, whatever the call's order.
    class Named extends Base(.y(7), .x(5));
    endclass

    // A specialization's value parameters are its vpiParamAssign objects
    // (37.32 detail 3): W is assigned 8, D its default 2.
    class P #(int W = 4, int D = 2);
        int v;
    endclass

    P #(8) pv;

    // A lexical use of a class: the parameter assignments of the base
    // typespec an `extends` names are its explicit actuals, else the
    // defaults (37.32 detail 3).
    class PD extends P #(8);
    endclass

    Derived d;
    // The live object has Base's property a = 3 + 4 once constructed (8.17).
    // The run lasts until time 2 so the plug-in's callback at time 1 fires.
    initial begin
        d = new;
        #2;
    end
endmodule
