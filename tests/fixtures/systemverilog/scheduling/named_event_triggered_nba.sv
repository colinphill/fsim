module named_event_triggered_nba;
    event changed;

    initial begin
        fork
            begin
                @changed;
                $display("WAKE triggered=%0d", changed.triggered);
                #0;
                $display("INACTIVE triggered=%0d", changed.triggered);
                #1;
                $display("NEXT_TIME triggered=%0d", changed.triggered);
            end
            begin
                #1;
                ->> changed;
                $display("REQUEST triggered=%0d", changed.triggered);
                #0;
                $display("INACTIVE_REQUEST triggered=%0d", changed.triggered);
            end
        join
        $finish;
    end
endmodule
