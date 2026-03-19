<?php
// mail.php restricted OTP for signup only
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type");
header("Content-Type: application/json");

require 'vendor/autoload.php';
require 'phpmailer_vendor/autoload.php';
require 'db.php';

use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;

use \Firebase\JWT\JWT;
use \Firebase\JWT\Key;

$secret_key = "YOUR_SECRET_KEY"; // Change it to your secret key

function sanitize($input) {
    return htmlspecialchars(strip_tags(trim($input)));
}

function userExists($email) {
    global $conn;
    $stmt = $conn->prepare("SELECT 1 FROM users WHERE email = ? LIMIT 1");
    $stmt->bind_param("s", $email);
    $stmt->execute();
    $stmt->store_result();
    $found = $stmt->num_rows > 0;
    $stmt->close();
    return $found;
}


function testMailer($testEmail) {
    //require 'vendor/autoload.php'; // adjust path if vendor folder is renamed
	$mail = new PHPMailer(true);
	$otp = strval(random_int(100000, 999999));

    try {
        $mail->isSMTP();
        $mail->Host = 'smtp.gmail.com';
        $mail->SMTPAuth = true;
        $mail->Username = 'YOUR_GMAIL_ACCOUNT@gmail.com'; // Change it into your gmail account
        $mail->Password = 'abcd efgh ijkl mnop'; // Change it to your app password
        $mail->SMTPSecure = PHPMailer::ENCRYPTION_STARTTLS;
        $mail->Port = 587;

        $mail->setFrom('no-reply@smarthydrofarm.com', 'SHVF OTP Verification');
        $mail->addAddress($testEmail);
        $mail->isHTML(true);
        $mail->Subject = 'Mailer Test';

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
        echo json_encode(['status' => 'success', 'message' => 'Test email sent successfully']);
    } catch (Exception $e) {
        echo json_encode(['status' => 'error', 'message' => 'Test email failed: ' . $mail->ErrorInfo]);
    }
}


function sendOTP($email) {
    global $conn;

    $email = sanitize($email);
    if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        http_response_code(400);
        echo json_encode(['status' => 'error', 'message' => 'Invalid email']);
        return;
    }

	$stmt = $conn->prepare("SELECT activated FROM users WHERE email = ? LIMIT 1");
	$stmt->bind_param("s", $email);
	$stmt->execute();
	$stmt->bind_result($activated);
	if ($stmt->fetch()) {
 	   $stmt->close();
 	 	if ($activated == 1) {
       	 	  echo json_encode(['status' => 'error', 'message' => 'Email already activated']);
       	 	  return;
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
        $mail->Username = 'YOUR_GMAIL_ACCOUNT@gmail.com'; // Change it into your gmail account
        $mail->Password = 'abcd efgh ijkl mnop'; // Change it into your app password
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
        echo json_encode(['status' => 'success', 'message' => 'OTP sent']);
    } catch (Exception $e) {
        //http_response_code(500);
        echo json_encode(['status' => 'error', 'message' => 'Email failed to send']);
    }
}

function validateOTP($email, $otpInput, $user_id, $username) {
    global $conn, $secret_key;

    $email = sanitize($email);
    $otpInput = sanitize($otpInput);

    $stmt = $conn->prepare("SELECT otp FROM otp_table WHERE email = ?");
    $stmt->bind_param("s", $email);
    $stmt->execute();
    $stmt->bind_result($storedOtp);
    if ($stmt->fetch() && hash_equals($storedOtp, $otpInput)) {
    $stmt->close();

    // Delete used OTP
    $stmt = $conn->prepare("DELETE FROM otp_table WHERE email = ?");
    $stmt->bind_param("s", $email);
    $stmt->execute();
    $stmt->close();

    // Update user activation status
    $stmt = $conn->prepare("UPDATE users SET activated = 1 WHERE email = ?");
    $stmt->bind_param("s", $email);
    $stmt->execute();
    $stmt->close();

    // Create JWT token
    $payload = [
        "user_id" => $user_id,
        "username" => $username,
        "exp" => time() + (60*60*24)
    ];
    $jwt = JWT::encode($payload, $secret_key, 'HS256');

    echo json_encode(['status' => 'success','message' => 'OTP verified','token' => $jwt]);
}
 else {
        $stmt->close();
        //http_response_code(401);
        echo json_encode(['status' => 'error', 'message' => 'Invalid OTP']);
    }
}

function cleanupOTP($email) {
    global $conn;
    $email = sanitize($email);
    $stmt = $conn->prepare("DELETE FROM otp_table WHERE email = ?");
    $stmt->bind_param("s", $email);
    $stmt->execute();
    echo json_encode(['status' => 'success', 'message' => 'OTP cleared']);
}

// POST-only access
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = $_POST['action'] ?? '';
    $user_id = $_POST['user_id'] ?? '';
    $username = $_POST['username'] ?? '';
    $email = $_POST['email'] ?? '';
    $otp = $_POST['otp'] ?? '';

    if ($action === 'send') {
        sendOTP($email); // allowed only if not registered
    } elseif ($action === 'verify') {
        validateOTP($email, $otp, $user_id, $username);
    } elseif ($action === 'cleanup') {
        cleanupOTP($email);
    }elseif ($action === 'test') {
        testMailer($email);
    } else {
        http_response_code(400);
        echo json_encode(['status' => 'error', 'message' => 'Invalid action']);
    } 
}  else {
    http_response_code(405);
    echo json_encode(['status' => 'error', 'message' => 'Method not allowed']);
} 
