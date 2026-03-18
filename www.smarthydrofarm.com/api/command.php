<?php
// api/command.php — proxy to set_commands + dosing (hides device_code)
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

// 2) Parse incoming JSON from dashboard (manual relays, automation, plant, dosing)
$raw = file_get_contents("php://input");
$in  = json_decode($raw, true);
if (!is_array($in)) $in = [];

// device_id can come from body or session (prefer body so UI can switch devices)
$device_id = $in["device_id"] ?? ($_SESSION['current_device_id'] ?? '');
if (!$device_id) {
  http_response_code(400);
  echo json_encode(["error" => "missing device_id"]);
  exit;
}

// 3) Resolve device_code securely and ensure ownership
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

// 4) Build payload for webhook (never expose device_code to the browser)
$payload = $in;                // includes water/grow/solenoid/mixer/auto/plant/dose_*
$payload["device_id"]   = $dev["device_id"];
$payload["device_code"] = $dev["device_code"];
$payload["action"]      = "set_commands";

$webhook = dirname(__DIR__) . "/webhook.php";
$ch = curl_init();
curl_setopt_array($ch, [
  CURLOPT_URL => $webhook,
  CURLOPT_POST => true,
  CURLOPT_HTTPHEADER => ["Content-Type: application/json"],
  CURLOPT_POSTFIELDS => json_encode($payload),
  CURLOPT_RETURNTRANSFER => true,
  CURLOPT_TIMEOUT => 10,
]);
$out = curl_exec($ch);
$http = curl_getinfo($ch, CURLINFO_HTTP_CODE);
curl_close($ch);

if ($out === false || $http !== 200) {
  http_response_code(502);
  echo json_encode(["error" => "webhook set_commands upstream failed", "code" => $http]);
  exit;
}
echo $out;
