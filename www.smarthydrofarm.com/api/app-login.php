<?php
// login_get.php
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type");
header("Access-Control-Allow-Methods: GET, OPTIONS");
header("Content-Type: application/json");

// Only allow GET (and OPTIONS for CORS preflight)
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { exit; }
if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    http_response_code(405);
    echo json_encode(["status" => "error", "message" => "Method Not Allowed"]);
    exit;
}

require 'db.php';
require 'vendor/autoload.php'; // for firebase/php-jwt

use \Firebase\JWT\JWT;
use \Firebase\JWT\Key;

$secret_key = "YOUR_SECRET_KEY"; // Change this into your secret key

// Read credentials from query string: ?username=...&password=...
$username = isset($_GET['username']) ? trim($_GET['username']) : '';
$password = isset($_GET['password']) ? (string)$_GET['password'] : '';

if ($username === '' || $password === '') {
    echo json_encode(["status" => "error", "message" => "Missing username or password"]);
    exit;
}

// Prepare statement
$stmt = $conn->prepare("SELECT id, password_hash, activated, email FROM users WHERE username = ?");
if (!$stmt) {
    echo json_encode(["status" => "error", "message" => "SQL Error: " . $conn->error]);
    exit;
}

$stmt->bind_param("s", $username);
$stmt->execute();
$stmt->store_result();

if ($stmt->num_rows === 0) {
    echo json_encode(["status" => "error", "message" => "Invalid credentials"]);
    $stmt->close();
    $conn->close();
    exit;
}

$stmt->bind_result($user_id, $hashed_password, $activated, $email);
$stmt->fetch();

if ((int)$activated !== 1) {
    echo json_encode([
        "status"   => "unverified",
        "message"  => "Account not activated. Please confirm OTP.",
        "redirect" => "confirm_otp.html?email=" . urlencode($email) .
                      "&user_id=" . urlencode($user_id) .
                      "&username=" . urlencode($username)
    ]);
    $stmt->close();
    $conn->close();
    exit;
}

if (password_verify($password, $hashed_password)) {
    $payload = [
        "user_id"  => $user_id,
        "username" => $username,
        "exp"      => time() + (60 * 60 * 24) // 1 day
    ];
    $jwt = JWT::encode($payload, $secret_key, 'HS256');
    echo json_encode(["status" => "success", "token" => $jwt]);
} else {
    echo json_encode(["status" => "error", "message" => "Incorrect password"]);
}

$stmt->close();
$conn->close();
