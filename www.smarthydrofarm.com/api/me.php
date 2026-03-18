<?php
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type");
header("Access-Control-Allow-Methods: GET, OPTIONS");
header("Content-Type: application/json");

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { exit; }
if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    http_response_code(405);
    echo json_encode(["status" => "error", "message" => "Method Not Allowed"]);
    exit;
}

require 'db.php';
require 'vendor/autoload.php';

use \Firebase\JWT\JWT;
use \Firebase\JWT\Key;

$secret_key = "SHVF-ivanabueg";

// ---- Read token from query string: ?token=... ----
$jwt = isset($_GET['token']) ? trim($_GET['token']) : '';
if ($jwt === '') {
    http_response_code(401);
    echo json_encode(["status" => "error", "message" => "Missing token parameter"]);
    exit;
}

// ---- Verify JWT ----
try {
    $decoded = JWT::decode($jwt, new Key($secret_key, 'HS256'));
} catch (Exception $e) {
    http_response_code(401);
    echo json_encode(["status" => "error", "message" => "Invalid token"]);
    exit;
}

$user_id = isset($decoded->user_id) ? (int)$decoded->user_id : 0;
$username = isset($decoded->username) ? (string)$decoded->username : "";

if ($user_id <= 0) {
    http_response_code(401);
    echo json_encode(["status" => "error", "message" => "Token missing user_id"]);
    exit;
}

// ---- Fetch user from DB ----
$stmt = $conn->prepare("SELECT id, username, email, name, activated FROM users WHERE id = ? LIMIT 1");
if (!$stmt) {
    http_response_code(500);
    echo json_encode(["status" => "error", "message" => "SQL Error: " . $conn->error]);
    exit;
}

$stmt->bind_param("i", $user_id);
$stmt->execute();

/*
If your server supports mysqlnd, you can use get_result().
If not, fallback to bind_result().

We’ll do bind_result() (most compatible).
*/
$stmt->store_result();

if ($stmt->num_rows === 0) {
    http_response_code(404);
    echo json_encode(["status" => "error", "message" => "User not found"]);
    $stmt->close();
    $conn->close();
    exit;
}

$stmt->bind_result($id, $db_username, $email, $name, $activated);
$stmt->fetch();

// Optional: detect mismatch (not required)
if ($username !== "" && $db_username !== $username) {
    // You can block if you want:
    // http_response_code(401);
    // echo json_encode(["status"=>"error","message"=>"Token username mismatch"]);
    // exit;
}

// If account got deactivated later
if ((int)$activated !== 1) {
    echo json_encode([
        "status"   => "unverified",
        "message"  => "Account not activated. Please confirm OTP.",
        "redirect" => "confirm_otp.html?email=" . urlencode($email) .
                      "&user_id=" . urlencode($id) .
                      "&username=" . urlencode($db_username)
    ]);
    $stmt->close();
    $conn->close();
    exit;
}

// Success
echo json_encode([
    "status" => "success",
    "user" => [
        "id"       => (int)$id,
        "username" => $db_username,
        "email"    => $email,
        "name"     => $name
    ]
]);

$stmt->close();
$conn->close();