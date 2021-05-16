#!/usr/bin/env php
<?php
$x = 1;

function wait_a_moment() {
    sleep(1);
}

while($x <= 100) {
  $x++;
  wait_a_moment();
}
?>
