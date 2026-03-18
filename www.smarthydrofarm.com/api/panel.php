<?php
// api/panel.php — server-side proxy for panel data (hides device_code)
header("Content-Type: application/json");
session_start();
require_once __DIR__ . "/../db.php"; // provides $conn (mysqli)

// 1) Require auth
if (!isset($_SESSION['user_id'])) {
  http_response_code(401);
  echo json_encode(["error" => "unauthorized"]);
  exit;
}
$user_id = (int)$_SESSION['user_id'];

// 2) Resolve device_id to device_code securely
$device_id = $_GET['device_id'] ?? ($_SESSION['current_device_id'] ?? '');
if (!$device_id) {
  http_response_code(400);
  echo json_encode(["error" => "missing device_id"]);
  exit;
}

// Ensure the device belongs to the user (adjust join/column names to your schema)
$sql = "SELECT device_id, code AS device_code FROM devices WHERE device_id = ? AND user_id = ?";
$stmt = $conn->prepare($sql);
$stmt->bind_param("si", $device_id, $user_id);
$stmt->execute();
$res = $stmt->get_result();
$dev = $res ? $res->fetch_assoc() : null;
$stmt->close();

if (!$dev) {
  http_response_code(404);
  echo json_encode(["error" => "device not found"]);
  exit;
}

// 3) Call webhook.php as an internal HTTP request so we reuse consistent JSON
$webhook = dirname(__DIR__) . "/webhook.php";   // same host/path
$qs = http_build_query([
  "cmd" => "panel",
  "device_id" => $dev["device_id"],
  "device_code" => $dev["device_code"]
]);

$ch = curl_init();
curl_setopt_array($ch, [
  CURLOPT_URL => $webhook . "?" . $qs,
  CURLOPT_RETURNTRANSFER => true,
  CURLOPT_TIMEOUT => 10,
]);
$out = curl_exec($ch);
$http = curl_getinfo($ch, CURLINFO_HTTP_CODE);
curl_close($ch);

if ($out === false || $http !== 200) {
  http_response_code(502);
  echo json_encode(["error" => "webhook panel upstream failed", "code" => $http]);
  exit;
}
echo $out;
