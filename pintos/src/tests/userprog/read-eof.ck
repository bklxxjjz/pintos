# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(read-eof) begin
(read-eof) open "sample.txt"
(read-eof) file size is 239
(read-eof) read return 0 bytes
(read-eof) end
read-eof: exit(0)
EOF
pass;
