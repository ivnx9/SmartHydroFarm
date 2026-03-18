<?php
// profile.php
header("Content-Type: application/json");
require 'auth.php';

$u = verifyTokenOrFail(); // exits with 401 if invalid

// Example: return minimal profile data
echo json_encode([
  "user_id"  => $u->user_id ?? null,
  "username" => $u->username ?? null,
  "roles"    => ["user"],   // add your real roles here
  "status"   => "ok"
]);
