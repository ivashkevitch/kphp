@ok
<?php
require_once 'kphp_tester_include.php';

function f() {
    $shape = [1, 2, 3];
    $fut = fork(function() { echo 1; } );

    $shape = [$fut => 1];

    wait($fut);
}

f();
