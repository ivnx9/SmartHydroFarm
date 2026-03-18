<?php
// upload.php

// ---- CORS / headers ----
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type");
header("Access-Control-Allow-Methods: POST, OPTIONS");
header("Content-Type: application/json");

// Preflight quick exit
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

require 'db.php'; // must create $conn = new mysqli(...)

function json_response($arr, $code = 200) {
    http_response_code($code);
    echo json_encode($arr);
    exit;
}

// Validate device
function get_device_row(mysqli $conn, $device_id, $device_code) {
    $sql = "SELECT * FROM devices WHERE device_id = ? AND code = ? LIMIT 1";
    $stmt = $conn->prepare($sql);
    if (!$stmt) json_response(["status" => "error", "message" => "Prepare failed"], 500);
    $stmt->bind_param("ss", $device_id, $device_code);
    $stmt->execute();
    $res = $stmt->get_result();
    $row = $res ? $res->fetch_assoc() : null;
    $stmt->close();
    return $row;
}

// =========================
// Handle Image Upload
// =========================
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $device_id   = $_POST['device_id']   ?? '';
    $device_code = $_POST['device_code'] ?? '';

    if (!$device_id || !$device_code) {
        json_response(["status" => "error", "message" => "Missing device credentials"], 400);
    }

    // Validate device
    $dev = get_device_row($conn, $device_id, $device_code);
    if (!$dev) {
        json_response(["status" => "unauthorized"], 403);
    }

    // Check if file exists
    if (!isset($_FILES['image']) || $_FILES['image']['error'] !== UPLOAD_ERR_OK) {
        json_response(["status" => "error", "message" => "No image uploaded"], 400);
    }

    // Directory to save images
    $uploadDir = __DIR__ . "/images/";
    if (!is_dir($uploadDir)) {
        mkdir($uploadDir, 0777, true);
    }

    // Always overwrite with device_id.jpg
    $targetFile = $uploadDir . $device_id . ".jpg";

    if (!move_uploaded_file($_FILES['image']['tmp_name'], $targetFile)) {
        json_response(["status" => "error", "message" => "Failed to save image"], 500);
    }

    json_response([
        "status" => "ok",
        "message" => "Image uploaded",
        "filename" => $device_id . ".jpg"
    ]);
}

json_response(["status" => "error", "message" => "Unsupported method"], 405);
?>