--TEST--
COALESCE optimization
--INI--
opcache.enable=1
opcache.enable_cli=1
opcache.optimization_level=-1
opcache.opt_debug_level=0x20000
--EXTENSIONS--
opcache
--FILE--
<?php

function a() {
    $test = $test ?? true;
    return $test;
}

function b() {
    $test ??= true;
    return $test;
}

?>
--EXPECTF--
$_main:
     ; (lines=1, args=0, vars=0, tmps=0)
     ; (after optimizer)
     ; %s
0000 RETURN int(1)

a:
     ; (lines=3, args=0, vars=1, tmps=1)
     ; (after optimizer)
     ; %s
0000 T1 = COALESCE CV0($test) 0002
0001 T1 = QM_ASSIGN bool(true)
0002 RETURN bool(true)

b:
     ; (lines=3, args=0, vars=1, tmps=1)
     ; (after optimizer)
     ; %s
0000 T1 = COALESCE CV0($test) 0002
0001 T1 = ASSIGN CV0($test) bool(true)
0002 RETURN bool(true)
