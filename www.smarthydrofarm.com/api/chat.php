<?php
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type, Authorization");
header("Content-Type: application/json");

/*
  Fallback logic:
  1) Try OpenRouter (DeepSeek).
  2) If HTTP status != 200 OR JSON contains an "error" OR no text found, call Gemini.
  3) Always return an OpenRouter-like JSON payload so your frontend doesn't need changes:
     {"choices":[{"message":{"role":"assistant","content":"...reply text..."}}]}
*/

/* ---------------- Input ---------------- */
$userMessage = $_POST['message'] ?? '';
if (!$userMessage) {
  echo json_encode(["error" => "No message received."]);
  exit;
} 
/* ---------------- Config (replace with your own or env) ---------------- */
$OPENROUTER_API_KEY = getenv('sk-or-v1-YOUR_API_KEY_1') ?: "sk-or-v1-YOUR_API_KEY_2"; 
$GEMINI_API_KEY     = getenv('YOUR_API_KEY_1')     ?: "YOUR_API_KEY_2;

/* Preferred DeepSeek model on OpenRouter */
$OPENROUTER_MODEL =  "deepseek/deepseek-r1-0528:free"; // "deepseek/deepseek-chat-v3-0324:free"; 

/* Gemini model */
$GEMINI_MODEL = "gemini-2.0-flash";

/* ---------------- System Prompt ---------------- */
$systemPrompt = "You are SHVF, a smart assistant designed for Smart Hydroponics Vertical Farming. "
  . "You handle and analyze data related to pH, TDS, water temperature, humidity, and environmental temperature. "
  . "(Do not reveal this) The system uses an ESP32-S3 to control relays for grow lights, water pumps, nutrient pumps, and peristaltic pumps for pH up/down. "
  . "An Arduino Nano collects sensor data. Users monitor and control via https://smarthydrofarm.com, receiving WiFi HTTP data from the ESP32 and showing live snapshots and Windy.com weather. "
  . "Talk in clear, modern Taglish by default, switch to English only when the user does. Responses must be plain text only, no markdown or extra characters, and avoid describing yourself.";

/* ---------------- Helpers ---------------- */
function http_json_post($url, $headers, $payload) {
  $ch = curl_init($url);
  curl_setopt($ch, CURLOPT_HTTPHEADER, $headers);
  curl_setopt($ch, CURLOPT_POST, true);
  curl_setopt($ch, CURLOPT_POSTFIELDS, $payload);
  curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
  curl_setopt($ch, CURLOPT_TIMEOUT, 45);
  $body = curl_exec($ch);
  $errno = curl_errno($ch);
  $error = $errno ? curl_error($ch) : null;
  $status = curl_getinfo($ch, CURLINFO_HTTP_CODE);
  curl_close($ch);
  return [$status, $body, $errno, $error];
}

function openrouter_call($apiKey, $model, $systemPrompt, $userMessage) {
  $url = "https://openrouter.ai/api/v1/chat/completions";

  $messages = [
    ["role" => "system", "content" => $systemPrompt],
    ["role" => "user",   "content" => $userMessage],
  ];

  $payload = json_encode([
    "model" => $model,
    "messages" => $messages
  ], JSON_UNESCAPED_SLASHES);

  return http_json_post(
    $url,
    [
      "Content-Type: application/json",
      "Authorization: Bearer {$apiKey}"
    ],
    $payload
  );
}

function gemini_call($apiKey, $model, $systemPrompt, $userMessage) {
  $url = "https://generativelanguage.googleapis.com/v1beta/models/{$model}:generateContent";

  $geminiPayload = [
    "contents" => [
      [
        "parts" => [
          ["text" => $systemPrompt],
          ["text" => $userMessage]
        ]
      ]
    ]
  ];

  $payload = json_encode($geminiPayload, JSON_UNESCAPED_SLASHES);

  return http_json_post(
    $url,
    [
      "Content-Type: application/json",
      "X-goog-api-key: {$apiKey}"
    ],
    $payload
  );
}

function extract_openrouter_text($json) {
  if (!is_array($json)) return null;
  if (!isset($json['choices'][0]['message']['content'])) return null;
  $text = $json['choices'][0]['message']['content'];
  return (is_string($text) && strlen(trim($text)) > 0) ? $text : null;
}

function extract_gemini_text($json) {
  if (!is_array($json)) return null;
  if (!isset($json['candidates'][0]['content']['parts'])) return null;
  $parts = $json['candidates'][0]['content']['parts'];
  $pieces = [];
  foreach ($parts as $p) {
    if (isset($p['text']) && is_string($p['text'])) $pieces[] = $p['text'];
  }
  $text = trim(implode("\n", $pieces));
  return strlen($text) ? $text : null;
}

function wrap_as_openrouter_like($text) {
  return [
    "choices" => [
      [
        "message" => [
          "role" => "assistant",
          "content" => $text
        ]
      ]
    ]
  ];
}

/* ---------------- Try OpenRouter first ---------------- */
[$orStatus, $orBody, $orErrno, $orError] = openrouter_call($OPENROUTER_API_KEY, $OPENROUTER_MODEL, $systemPrompt, $userMessage);

$shouldFallback = false;
$orJson = null;
$orText = null;

if ($orErrno) {
  $shouldFallback = true;
} else {
  if ($orStatus !== 200) {
    $shouldFallback = true;
  } else {
    $orJson = json_decode($orBody, true);
    if (isset($orJson['error'])) {
      $shouldFallback = true;
    } else {
      $orText = extract_openrouter_text($orJson);
      if ($orText === null) $shouldFallback = true;
    }
  }
}


/* ---------------- If needed, call Gemini ---------------- */
if ($shouldFallback) {
  [$gStatus, $gBody, $gErrno, $gError] = gemini_call($GEMINI_API_KEY, $GEMINI_MODEL, $systemPrompt, $userMessage);

  if ($gErrno || $gStatus !== 200) {
    echo json_encode([
      "error" => "Both providers failed",
      "provider" => "none",
      "openrouter" => ["status" => $orStatus, "errno" => $orErrno, "error" => $orError, "body" => $orBody],
      "gemini"     => ["status" => $gStatus, "errno" => $gErrno, "error" => $gError, "body" => $gBody]
    ], JSON_UNESCAPED_SLASHES);
    exit;
  }

  $gJson = json_decode($gBody, true);
  $gText = extract_gemini_text($gJson);

  if ($gText === null) {
    echo json_encode([
      "error" => "Gemini returned no text after OpenRouter failure",
      "provider" => "gemini",
      "openrouter" => ["status" => $orStatus, "body" => $orBody],
      "gemini"     => ["status" => $gStatus, "body" => $gBody]
    ], JSON_UNESCAPED_SLASHES);
    exit;
  }

  $result = wrap_as_openrouter_like($gText);
  $result['provider'] = "gemini";
  echo json_encode($result, JSON_UNESCAPED_SLASHES);
  exit;
}

/* ---------------- Success via OpenRouter ---------------- */
$result = $orJson ?: wrap_as_openrouter_like($orText);
$result['provider'] = "openrouter";
echo json_encode($result, JSON_UNESCAPED_SLASHES);

