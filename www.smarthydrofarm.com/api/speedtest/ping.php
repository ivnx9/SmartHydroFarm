<?php
// speedtest_ping.php

header('Content-Type: application/json');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');

// Optional: tiny processing delay (usually KEEP THIS 0 for accuracy)
// usleep(1000); // 1ms

echo json_encode([
    'status' => 'ok',
    'time'   => microtime(true),
]);
exit;