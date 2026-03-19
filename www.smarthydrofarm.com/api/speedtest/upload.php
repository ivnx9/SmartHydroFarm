<?php
// speedtest_upload.php

// Allow large bodies if needed
@set_time_limit(0);

// Read the raw body (you don't need to save it)
$body = file_get_contents('php://input');
$bytesReceived = strlen($body);

// You can log it for debugging if you want:
// file_put_contents('speedtest_upload.log', date('c') . " - Received: {$bytesReceived} bytes\n", FILE_APPEND);

// Response
header('Content-Type: application/json');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');

echo json_encode([
    'status' => 'ok',
    'bytes_received' => $bytesReceived,
]);
exit;