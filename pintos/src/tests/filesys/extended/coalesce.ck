# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(coalesce) begin
(coalesce) Create and open file0.
(coalesce) Writing 64 KB of data into file0 byte-by-byte...
(coalesce) Reading file0 byte by byte...
(coalesce) Checking block device's write_cnt...
(coalesce) Block device's write_cnt increased by approximately 128.
(coalesce) end
EOF
pass;