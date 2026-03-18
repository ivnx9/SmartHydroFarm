<?php

ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);
error_reporting(E_ALL);


header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type");
header("Content-Type: application/json");

require 'db.php';
require 'phpmailer_vendor/autoload.php';

use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;


// Collect inputs safely
$name = $_POST['name'] ?? '';
$email = $_POST['email'] ?? '';
$username = $_POST['username'] ?? '';
$password = $_POST['password'] ?? '';

// Validate inputs
if (!$name || !$email || !$username || !$password) {
    echo json_encode(["status" => "error", "message" => "Missing required fields"]);
    exit;
}

$hashed_password = password_hash($password, PASSWORD_DEFAULT);

// Check if user already exists
$check = $conn->prepare("SELECT id FROM users WHERE username = ? OR email = ?");
if (!$check) {
    echo json_encode(["status" => "error", "message" => "Server error (prepare check failed)"]);
    exit;
}
$check->bind_param("ss", $username, $email);
$check->execute();
$check->store_result();

if ($check->num_rows > 0) {
    echo json_encode(["status" => "error", "message" => "Username or email already exists"]);
    $check->close();
    exit;
}
$check->close();

function sanitize($input) {
    return htmlspecialchars(strip_tags(trim($input)));
}

function sendOTP($email) {
    global $conn;

    $email = sanitize($email);
    if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        //http_response_code(400);
        return false;
    }

	$stmt = $conn->prepare("SELECT activated FROM users WHERE email = ? LIMIT 1");
	$stmt->bind_param("s", $email);
	$stmt->execute();
	$stmt->bind_result($activated);
	if ($stmt->fetch()) {
 	   $stmt->close();
 	 	if ($activated == 1) {
       	 	  return false;
    	  	}
	} else {
        $stmt->close();
	}

    $otp = strval(random_int(100000, 999999));

    $stmt = $conn->prepare("REPLACE INTO otp_table (email, otp, created_at) VALUES (?, ?, NOW())");
    $stmt->bind_param("ss", $email, $otp);
    $stmt->execute();
    $stmt->close();

    $mail = new PHPMailer(true);
    try {
        $mail->isSMTP();
        $mail->Host = 'smtp.gmail.com';
        $mail->SMTPAuth = true;
        $mail->Username = 'otp.sender.online@gmail.com';
        $mail->Password = 'xyem aapx oezd npsj';
        $mail->SMTPSecure = PHPMailer::ENCRYPTION_STARTTLS;
        $mail->Port = 587;

        $mail->setFrom('no-reply@smarthydrofarm.com', 'SHVF OTP Verification');
        $mail->addAddress($email);
        $mail->isHTML(true);
        $mail->Subject = 'Signup OTP';
       $mail->Body = '
<!DOCTYPE html>
<html>
  <body style="background-color:#0a0a0a; color:#e4e4e4; font-family:\'Segoe UI\', sans-serif; padding:40px 0; display:flex; justify-content:center;">
    <div style="background-color:#1a1a1a; border:1px solid rgba(255,255,255,0.08); border-radius:12px; box-shadow:0 8px 24px rgba(0,255,140,0.15); padding:30px 35px; max-width:400px; width:100%; text-align:center;">
      <h2 style="color:#00ff8c; margin-bottom:20px;">SmartHydroFarm Verification</h2>
      <p style="font-size:16px; margin-bottom:15px;">
        Dear user,<br><br>
        We received a request to verify your email address. Please use the One-Time Password (OTP) below to complete your registration.
      </p>
      <div style="font-size:26px; background:rgba(255,255,255,0.05); border:1px solid rgba(255,255,255,0.08); border-radius:12px; color:#00ccff; padding:14px 18px; letter-spacing:5px; font-weight:bold; margin-bottom:20px;">
        ' . htmlspecialchars($otp) . '
      </div>
      <p style="font-size:14px; margin-bottom:20px; color:#ccc;">
        This code is valid for the next 5 minutes. Do not share this code with anyone. If you did not request this, please ignore this email.
      </p>
      <p style="font-size:14px; color:#777;">
        Thank you,<br>
        <strong>SmartHydroFarm Team</strong>
      </p>
    </div>
  </body>
</html>';


        $mail->send();
        //echo json_encode(['status' => 'success', 'message' => 'OTP sent']);
        return true;
    } catch (Exception $e) {
        return false;
    }
}


// Insert user
$stmt = $conn->prepare("INSERT INTO users (name, email, username, password_hash, activated) VALUES (?, ?, ?, ?, 0)");
if (!$stmt) {
    echo json_encode(["status" => "error", "message" => "Server error (prepare insert failed)"]);
    exit;
}
$stmt->bind_param("ssss", $name, $email, $username, $hashed_password);

if ($stmt->execute()) {
    $sent = sendOTP($email);
if ($sent) {
    echo json_encode([
        "status" => "success",
        "message" => "Signup success, OTP sent",
        "user_id" => $stmt->insert_id,
        "username" => $username
    ]);
} else {
    echo json_encode(["status" => "error", "message" => "OTP sending failed"]);
}

} else {
    echo json_encode(["status" => "error", "message" => "Signup failed"]);
}
$stmt->close();
$conn->close();
?>


