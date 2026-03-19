<?php
// speedtest_download.php

// --- CONFIG ---
$allowedSizes = [
    '1mb'  => 1 * 1024 * 1024,
    '3mb'  => 3 * 1024 * 1024,
    '5mb'  => 5 * 1024 * 1024,
    '10mb' => 10 * 1024 * 1024,
];

// Get ?size= from URL (ex: size=5mb)
$sizeKey = isset($_GET['size']) ? strtolower($_GET['size']) : '5mb';
$sizeBytes = isset($allowedSizes[$sizeKey]) ? $allowedSizes[$sizeKey] : $allowedSizes['5mb'];

// Disable time limit in case of slow network
@set_time_limit(0);

// Headers for binary download
header('Content-Type: application/octet-stream');
header('Content-Disposition: inline; filename="speedtest.bin"');
header('Content-Length: ' . $sizeBytes);
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');

// Stream random data in chunks so you don't eat RAM
$chunkSize = 64 * 1024; // 64KB
$bytesSent = 0;

while ($bytesSent < $sizeBytes) {
    $remaining = $sizeBytes - $bytesSent;
    $sendSize  = ($remaining > $chunkSize) ? $chunkSize : $remaining;

    // Generate random bytes
    $chunk = random_bytes($sendSize);

    echo $chunk;
    $bytesSent += $sendSize;

    // Flush to client
    if (function_exists('ob_flush')) {
        @ob_flush();
    }
    flush();
}

exit;