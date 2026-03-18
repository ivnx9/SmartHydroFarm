<?php
// auth.php
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type, Authorization");
header("Access-Control-Allow-Methods: POST, OPTIONS");
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') exit;

require 'vendor/autoload.php';
use \Firebase\JWT\JWT;
use \Firebase\JWT\Key;

$SECRET_KEY = "SHVF-ivanabueg";

function getBearerToken() {
    $hdr = '';
    if (isset($_SERVER['HTTP_AUTHORIZATION'])) $hdr = trim($_SERVER['HTTP_AUTHORIZATION']);
    elseif (function_exists('apache_request_headers')) {
        $headers = apache_request_headers();
        if (isset($headers['Authorization'])) $hdr = trim($headers['Authorization']);
        elseif (isset($headers['authorization'])) $hdr = trim($headers['authorization']);
    }
    if (stripos($hdr, 'Bearer ') === 0) return substr($hdr, 7);
    return null;
}

function verifyTokenOrFail() {
    global $SECRET_KEY;

    // Prefer Authorization: Bearer ...
    $jwt = getBearerToken();

    // Fallback: accept token from POST form (for clients that can’t set headers)
    if (!$jwt && isset($_POST['token'])) $jwt = $_POST['token'];

    if (!$jwt) {
        http_response_code(401);
        echo json_encode(["error"=>"NO_TOKEN"]);
        exit;
    }

    try {
        $decoded = JWT::decode($jwt, new Key($SECRET_KEY, 'HS256'));
        return $decoded; // -> user_id, username, exp, etc.
    } catch (Throwable $e) {
        http_response_code(401);
        echo json_encode(["error"=>"INVALID_TOKEN","detail"=>$e->getMessage()]);
        exit;
    }
}
