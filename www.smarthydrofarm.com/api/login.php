<?php
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type");

header("Content-Type: application/json");

require 'db.php';
require 'vendor/autoload.php'; // for firebase/php=jwt

use \Firebase\JWT\JWT;
use \Firebase\JWT\Key;

$secret_key = "SHVF-ivanabueg"; 

$username = $_POST['username'] ?? '';
$password = $_POST['password'] ?? '';

if (empty($username) || empty($password)) {
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
        "status" => "unverified",
        "message" => "Account not activated. Please confirm OTP.",
        "redirect" => "confirm_otp.html?email=" . urlencode($email) . "&user_id=" . urlencode($user_id) . "&username=" . urlencode($username)
    ]);
    $stmt->close();
    $conn->close();
    exit;
}

if (password_verify($password, $hashed_password)) {
	$payload = [
		"user_id" => $user_id,
		"username" => $username,
		"exp" => time() + (60*60*24) // Token expires in 1 day
		];
		$jwt = JWT::encode($payload, $secret_key, 'HS256');
		echo json_encode(["status" => "success", "token" => $jwt]);
} else {
    echo json_encode(["status" => "error", "message" => "Incorrect password"]);
}

$stmt->close();
$conn->close();
?>
