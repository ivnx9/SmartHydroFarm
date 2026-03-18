<?php
// account-deletion.php — handles form, confirmation, and deletion in one file using timestamped HMAC token

require 'vendor/autoload.php';
require 'phpmailer_vendor/autoload.php';
require 'db.php';

use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;

define('SECRET_KEY', 'your-strong-secret-key-here'); // CHANGE THIS TO A STRONG SECRET

function sanitize($input) {
    return htmlspecialchars(strip_tags(trim($input)));
}

function emailExists($email) {
    global $conn;
    $stmt = $conn->prepare("SELECT id FROM users WHERE email = ?");
    $stmt->bind_param("s", $email);
    $stmt->execute();
    $stmt->store_result();
    $found = $stmt->num_rows > 0;
    $stmt->close();
    return $found;
}

function getUserId($email) {
    global $conn;
    $stmt = $conn->prepare("SELECT id FROM users WHERE email = ? LIMIT 1");
    $stmt->bind_param("s", $email);
    $stmt->execute();
    $stmt->bind_result($user_id);
    $stmt->fetch();
    $stmt->close();
    return $user_id;
}

function sendDeletionLink($email) {
    $timestamp = time();
    $token = hash_hmac('sha256', $email . $timestamp, SECRET_KEY);
    $url = "https://" . $_SERVER['HTTP_HOST'] . $_SERVER['PHP_SELF']
         . "?email=" . urlencode($email)
         . "&timestamp=$timestamp"
         . "&token=$token";

    $mail = new PHPMailer(true);
    try {
        $mail->isSMTP();
        $mail->Host = 'smtp.gmail.com';
        $mail->SMTPAuth = true;
        $mail->Username = 'otp.sender.online@gmail.com';
        $mail->Password = 'xyem aapx oezd npsj';
        $mail->SMTPSecure = PHPMailer::ENCRYPTION_STARTTLS;
        $mail->Port = 587;

        $mail->setFrom('no-reply@smarthydrofarm.com', 'SmartHydroFarm');
        $mail->addAddress($email);
        $mail->isHTML(true);
        $mail->Subject = 'Account Deletion Request';
        $mail->Body = "
        <div style='background:#1a1a1a;padding:20px;border-radius:10px;color:#e4e4e4;font-family:sans-serif;text-align:center'>
            <h2 style='color:#00ff8c;'>Confirm Account Deletion</h2>
            <p>If you wish to delete your account and all associated data, click below:</p>
            <a href='$url' style='background:#00ccff;color:#000;padding:10px 20px;border-radius:50px;text-decoration:none;margin-top:10px;display:inline-block;'>Confirm Deletion</a>
            <p style='font-size:12px;margin-top:15px;color:#999;'>Link is valid for 30 minutes only.</p>
        </div>";
        $mail->send();
    } catch (Exception $e) {
        echo '<p style="color:red;">Mailer error: ' . $mail->ErrorInfo . '</p>';
    }
}

function deleteUserData($user_id) {
    global $conn;
    $stmt1 = $conn->prepare("DELETE FROM sensor_data WHERE device_id IN (SELECT id FROM devices WHERE user_id = ?)");
    $stmt1->bind_param("s", $user_id);
    $stmt1->execute();
    $stmt1->close();

    $stmt2 = $conn->prepare("DELETE FROM devices WHERE user_id = ?");
    $stmt2->bind_param("s", $user_id);
    $stmt2->execute();
    $stmt2->close();

    $stmt3 = $conn->prepare("DELETE FROM users WHERE id = ?");
    $stmt3->bind_param("s", $user_id);
    $stmt3->execute();
    $stmt3->close();
}

// MAIN
$feedback = "";

if ($_SERVER["REQUEST_METHOD"] === "POST" && isset($_POST['email'])) {
    $email = sanitize($_POST['email']);
    if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        $feedback = "Invalid email format.";
    } elseif (!emailExists($email)) {
        $feedback = "Email not found in our system.";
    } else {
        sendDeletionLink($email);
        $feedback = "A confirmation link has been sent to your email.";
    }
}

if (isset($_GET['email'], $_GET['timestamp'], $_GET['token'])) {
    $email = sanitize($_GET['email']);
    $timestamp = (int) $_GET['timestamp'];
    $token = $_GET['token'];

    $expectedToken = hash_hmac('sha256', $email . $timestamp, SECRET_KEY);
    $now = time();

    if (!hash_equals($expectedToken, $token)) {
        $feedback = "Invalid deletion link.";
    } elseif ($now - $timestamp > 1800) {
        $feedback = "Deletion link has expired.";
    } elseif (!emailExists($email)) {
        $feedback = "Account not found.";
    } else {
        $user_id = getUserId($email);
        deleteUserData($user_id);
        $feedback = "Your account and data have been permanently deleted.";
    }
}
?>

<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>Request Account Deletion</title>
  <style>
    body {
      background-color: #0a0a0a;
      color: #e4e4e4;
      font-family: 'Exo 2', sans-serif;
      display: flex;
      align-items: center;
      justify-content: center;
      height: 100vh;
      margin: 0;
      flex-direction: column;
      padding: 20px;
    }

    .container {
      background-color: #1a1a1a;
      border-radius: 12px;
      padding: 30px;
      box-shadow: 0 8px 24px rgba(0,255,140,.15);
      max-width: 400px;
      width: 100%;
    }

    h2 {
      color: #00ff8c;
      margin-bottom: 15px;
      text-align: center;
    }

    label {
      display: block;
      margin-bottom: 8px;
      font-size: 14px;
    }

    input[type="email"] {
      width: 100%;
      padding: 12px;
      margin-bottom: 20px;
      border: 1px solid #333;
      border-radius: 8px;
      background-color: #111;
      color: #fff;
    }

    button {
      width: 100%;
      padding: 12px;
      background: linear-gradient(135deg, #00ff8c, #00ccff);
      border: none;
      border-radius: 50px;
      font-weight: bold;
      cursor: pointer;
      transition: 0.3s;
      color: #000;
    }

    button:hover {
      transform: translateY(-2px);
      box-shadow: 0 8px 24px rgba(0,255,140,.3);
    }

    .note {
      font-size: 12px;
      margin-top: 15px;
      text-align: center;
      opacity: 0.7;
    }

    .feedback {
      text-align: center;
      margin-bottom: 15px;
      color: #00ccff;
    }
  </style>
</head>
<body>

  <div class="container">
    <h2>Request Account Deletion</h2>
    <?php if (!isset($_GET['token'])): ?>
    <form method="POST" action="">
      <label for="email">Enter your registered email:</label>
      <input type="email" id="email" name="email" required placeholder="you@example.com">
      <button type="submit">Request Deletion</button>
    </form>
    <div class="note">
      You will receive a confirmation email with a secure link to proceed with account deletion.<br/>
      This link will expire after 30 minutes.<br/>
    </div>
    <?php endif; ?><br/>
    <div class="feedback"><?= $feedback ?></div>
  </div>

</body>
</html>
