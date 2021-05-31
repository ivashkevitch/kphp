@kphp_should_fail
/but bool type is passed/
/but int\[\] type is passed/
/but Foo type is passed/
/but \?string type is passed/
/but string\|false type is passed/
<?php

class Foo {}

function data(): ?string {
    return null;
}

/**
 * @return string|false
 */
function data2() {
    return false;
}

function f() {
    $array = [1, 2, 3];

    echo $array[true];
    echo $array[$array];
    echo $array[new Foo];
    echo $array[data()];
    echo $array[data2()];
}

f();
