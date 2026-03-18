<?php
// api/device_admin.php

header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type, Authorization");
header("Access-Control-Allow-Methods: GET, POST, OPTIONS");
header("Content-Type: application/json");

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

require __DIR__ . '/db.php';
require __DIR__ . '/vendor/autoload.php';

use Firebase\JWT\JWT;
use Firebase\JWT\Key;

$secret_key = "SHVF-ivanabueg"; // same as login.php

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);

function json_response($arr, $code = 200) {
    http_response_code($code);
    echo json_encode($arr);
    exit;
}

function getAuthorizationHeader() {
    if (isset($_SERVER['HTTP_AUTHORIZATION'])) {
        return $_SERVER['HTTP_AUTHORIZATION'];
    }
    if (isset($_SERVER['REDIRECT_HTTP_AUTHORIZATION'])) {
        return $_SERVER['REDIRECT_HTTP_AUTHORIZATION'];
    }
    if (function_exists('apache_request_headers')) {
        $headers = apache_request_headers();
        if (isset($headers['Authorization'])) {
            return $headers['Authorization'];
        }
    }
    return null;
}

function getUserIdFromJwt($secret_key) {
    $auth = getAuthorizationHeader();
    if (!$auth || stripos($auth, 'Bearer ') !== 0) {
        json_response(['status' => 'error', 'message' => 'Missing or invalid Authorization header'], 401);
    }

    $jwt = trim(substr($auth, 7));
    if ($jwt === '') {
        json_response(['status' => 'error', 'message' => 'Empty token'], 401);
    }

    try {
        $decoded = JWT::decode($jwt, new Key($secret_key, 'HS256'));
    } catch (Throwable $e) {
        json_response(['status' => 'error', 'message' => 'Invalid token: ' . $e->getMessage()], 401);
    }

    if (!isset($decoded->user_id) || !$decoded->user_id) {
        json_response(['status' => 'error', 'message' => 'Token has no user_id'], 401);
    }

    return (string)$decoded->user_id;
}

try {
    $method  = $_SERVER['REQUEST_METHOD'];
    $user_id = getUserIdFromJwt($secret_key);   // <- user id from token (varchar 36)

    // Make sure this user really exists (avoids FK violation)
    $stmt = $conn->prepare("SELECT id FROM users WHERE id = ? LIMIT 1");
    $stmt->bind_param("s", $user_id);
    $stmt->execute();
    $stmt->store_result();
    if ($stmt->num_rows === 0) {
        $stmt->close();
        json_response([
            'status'  => 'error',
            'message' => 'User not found for this token (FK would fail).'
        ], 403);
    }
    $stmt->close();

    if ($method === 'POST') {
        $raw  = file_get_contents('php://input');
        $data = json_decode($raw, true);

        if (!is_array($data)) {
            json_response(['status' => 'error', 'message' => 'Invalid JSON body'], 400);
        }

        $action      = $data['action']      ?? '';
        $device_id   = trim($data['device_id']   ?? '');
        $device_code = trim($data['device_code'] ?? '');

        /* ---------- CREATE / UPDATE ---------- */
        if ($action === 'create') {
            if ($device_id === '' || $device_code === '') {
                json_response([
                    'status'  => 'error',
                    'message' => 'Missing device_id or device_code'
                ], 400);
            }

            // Check if device already exists for this user
            $stmt = $conn->prepare("SELECT id FROM devices WHERE user_id = ? AND device_id = ?");
            $stmt->bind_param("ss", $user_id, $device_id);
            $stmt->execute();
            $stmt->store_result();

            if ($stmt->num_rows > 0) {
                // Update only the code
                $stmt->close();
                $stmt = $conn->prepare(
                    "UPDATE devices SET code = ? WHERE user_id = ? AND device_id = ?"
                );
                $stmt->bind_param("sss", $device_code, $user_id, $device_id);
                $stmt->execute();
                $stmt->close();

                json_response(['status' => 'ok', 'message' => 'Device updated']);
            }

            $stmt->close();

            // Insert new row; generate id via UUID() to satisfy NOT NULL PK
            $stmt = $conn->prepare(
                "INSERT INTO devices (id, user_id, device_id, code)
                 VALUES (UUID(), ?, ?, ?)"
            );
            $stmt->bind_param("sss", $user_id, $device_id, $device_code);
            $stmt->execute();
            $stmt->close();

            json_response(['status' => 'ok', 'message' => 'Device created']);
        }

        /* ---------- DELETE ---------- */
        if ($action === 'delete') {
            if ($device_id === '') {
                json_response(['status' => 'error', 'message' => 'Missing device_id'], 400);
            }

            $stmt = $conn->prepare("DELETE FROM devices WHERE user_id = ? AND device_id = ?");
            $stmt->bind_param("ss", $user_id, $device_id);
            $stmt->execute();
            $stmt->close();

            json_response(['status' => 'ok', 'message' => 'Device deleted']);
        }

        json_response(['status' => 'error', 'message' => 'Unknown action'], 400);
    }

    /* ---------- LIST DEVICES (GET) ---------- */
    if ($method === 'GET') {
        $rows = [];
        $sql  = "SELECT id, device_id, code AS device_code, commands_rev, last_seen
                 FROM devices
                 WHERE user_id = ?
                 ORDER BY created_at ASC";
        $stmt = $conn->prepare($sql);
        $stmt->bind_param("s", $user_id);
        $stmt->execute();
        $res = $stmt->get_result();
        while ($row = $res->fetch_assoc()) {
            $rows[] = $row;
        }
        $stmt->close();

        json_response($rows);
    }

    json_response(['status' => 'error', 'message' => 'Unsupported method'], 405);

} catch (Throwable $e) {
    json_response(
        ['status' => 'error', 'message' => 'Server error: ' . $e->getMessage()],
        500
    );
}
