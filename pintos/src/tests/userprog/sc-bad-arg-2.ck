# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(sc-bad-arg-2) begin
sc-bad-arg-2: exit(-1)
EOF
pass;
