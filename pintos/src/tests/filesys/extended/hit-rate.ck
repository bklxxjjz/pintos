# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(hit-rate) begin
(hit-rate) Create, open and write to file0.
(hit-rate) Close file0 and reset cache.
(hit-rate) Open file0.
(hit-rate) Reading file0...
(hit-rate) The hit rate is now 74 percent.
(hit-rate) Close and reopen file0.
(hit-rate) Reading file0 again...
(hit-rate) The hit rate is now 87 percent.
(hit-rate) Hit rate increased.
(hit-rate) end
EOF
pass;