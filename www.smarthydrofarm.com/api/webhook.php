<?php
// webhook.php  — online control + one-shot dosing support

header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Headers: Content-Type");
header("Access-Control-Allow-Methods: GET, POST, OPTIONS");
header("Content-Type: application/json");
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { http_response_code(204); exit; }

require 'db.php'; // must create $conn = new mysqli(...)

function json_response($arr, $code = 200) {
    http_response_code($code);
    echo json_encode($arr);
    exit;
}

function read_input() {
    $data = [];
    $raw = file_get_contents('php://input');
    if ($raw) {
        $decoded = json_decode($raw, true);
        if (json_last_error() === JSON_ERROR_NONE && is_array($decoded)) $data = $decoded;
    }
    foreach ($_POST as $k => $v) $data[$k] = $v;
    return $data;
}

function get_device_row(mysqli $conn, $device_id, $device_code) {
    $sql = "SELECT * FROM devices WHERE device_id = ? AND code = ?";
    $stmt = $conn->prepare($sql);
    if (!$stmt) json_response(["status"=>"error","message"=>"Prepare failed: devices lookup"], 500);
    $stmt->bind_param("ss", $device_id, $device_code);
    $stmt->execute();
    $res = $stmt->get_result();
    $row = $res ? $res->fetch_assoc() : null;
    $stmt->close();
    return $row;
}

// =========================
// GET endpoints
// =========================
if ($_SERVER['REQUEST_METHOD'] === 'GET' && isset($_GET['cmd'])) {
    $cmd = $_GET['cmd'];
    $device_id   = $_GET['device_id']  ?? '';
    $device_code = $_GET['device_code']?? '';
    if (!$device_id || !$device_code) json_response(["status"=>"error","message"=>"Missing credentials"], 400);

    $dev = get_device_row($conn, $device_id, $device_code);
    if (!$dev) json_response(["status"=>"error","message"=>"Device not found"], 404);

    if ($cmd === '1') {
        // Device poll: desired relay states, automation flag, plant profile, and (optional) dosing request
        $resp = [
            "rev"        => (int)($dev['commands_rev'] ?? 0),
            "updated_at" => $dev['commands_updated_at'] ?? null,

            // Desired states
            "auto"     => (bool)($dev['automation'] ?? 0),
            "water"    => (bool)($dev['water_pump'] ?? 0),
            "grow"     => (bool)($dev['growlight'] ?? 0),
            "solenoid" => (bool)($dev['solenoid'] ?? 0),
            "mixer"    => (bool)($dev['mixer'] ?? 0),

            // What the device last ACKed/applied
            "actual" => [
                "water"      => (bool)($dev['water_pump_actual'] ?? 0),
                "grow"       => (bool)($dev['growlight_actual'] ?? 0),
                "solenoid"   => (bool)($dev['solenoid_actual'] ?? 0),
                "mixer"      => (bool)($dev['mixer_actual'] ?? 0),
                "applied_at" => $dev['last_applied_at'] ?? null,
                "ack_rev"    => (int)($dev['last_ack_rev'] ?? 0),
                "dose_ack"   => (int)($dev['last_dose_ack'] ?? 0),
            ],

            // Plant profile
            "plant" => [
                "name"      => $dev['plant_name']      ?? 'lettuce',
                "ppm_min"   => (int)($dev['ppm_min']   ?? 560),
                "ppm_max"   => (int)($dev['ppm_max']   ?? 840),
                "ph_target" => (float)($dev['ph_target'] ?? 6.0),
                "ph_min"    => (float)($dev['ph_min']    ?? 5.5),
                "ph_max"    => (float)($dev['ph_max']    ?? 6.5),
                "on_h"      => (int)($dev['light_on_hour']   ?? 6),
                "on_m"      => (int)($dev['light_on_minute'] ?? 0),
                "off_h"     => (int)($dev['light_off_hour']  ?? 20),
                "off_m"     => (int)($dev['light_off_minute']?? 0),
            ],
        ];

        // Optional one-shot dosing command (only included if dose_req > last_dose_ack)
        $dose_req = isset($dev['dose_req']) ? (int)$dev['dose_req'] : 0;
        $last_ack = isset($dev['last_dose_ack']) ? (int)$dev['last_dose_ack'] : 0;
        if ($dose_req > 0 && $dose_req > $last_ack) {
            $resp["dose_req"]     = $dose_req;
            $resp["dose_channel"] = $dev['dose_channel'] ?? null;   // "A"|"B"|"PH_UP"|"PH_DOWN"
            if (isset($dev['dose_ml'])) $resp["dose_ml"] = (float)$dev['dose_ml'];
            if (isset($dev['dose_ms'])) $resp["dose_ms"] = (int)$dev['dose_ms'];
        }

        // heartbeat
        $stmt = $conn->prepare("UPDATE devices SET last_seen = NOW() WHERE device_id = ? AND code = ?");
        $stmt->bind_param("ss", $device_id, $device_code);
        $stmt->execute();
        $stmt->close();

        json_response($resp);
    }

    if ($cmd === 'panel') {
        // for dashboard UI
        $row = null;
        if ($stmt = $conn->prepare("SELECT * FROM sensor_data WHERE device_id = ? ORDER BY timestamp DESC, id DESC LIMIT 1")) {
            $stmt->bind_param("s", $device_id);
            $stmt->execute();
            $res = $stmt->get_result();
            $row = $res ? $res->fetch_assoc() : null;
            $stmt->close();
        }
        $resp = [
            "rev"        => (int)($dev['commands_rev'] ?? 0),
            "desired" => [
                "auto"     => (bool)($dev['automation'] ?? 0),
                "water"    => (bool)($dev['water_pump'] ?? 0),
                "grow"     => (bool)($dev['growlight'] ?? 0),
                "solenoid" => (bool)($dev['solenoid'] ?? 0),
                "mixer"    => (bool)($dev['mixer'] ?? 0),
            ],
            "actual" => [
                "water"      => (bool)($dev['water_pump_actual'] ?? 0),
                "grow"       => (bool)($dev['growlight_actual'] ?? 0),
                "solenoid"   => (bool)($dev['solenoid_actual'] ?? 0),
                "mixer"      => (bool)($dev['mixer_actual'] ?? 0),
                "applied_at" => $dev['last_applied_at'] ?? null,
                "ack_rev"    => (int)($dev['last_ack_rev'] ?? 0),
                "dose_ack"   => (int)($dev['last_dose_ack'] ?? 0),
            ],
            "plant" => [
                "name"      => $dev['plant_name']      ?? 'lettuce',
                "ppm_min"   => (int)($dev['ppm_min']   ?? 560),
                "ppm_max"   => (int)($dev['ppm_max']   ?? 840),
                "ph_target" => (float)($dev['ph_target'] ?? 6.0),
                "ph_min"    => (float)($dev['ph_min']    ?? 5.5),
                "ph_max"    => (float)($dev['ph_max']    ?? 6.5),
                "on_h"      => (int)($dev['light_on_hour']   ?? 6),
                "on_m"      => (int)($dev['light_on_minute'] ?? 0),
                "off_h"     => (int)($dev['light_off_hour']  ?? 20),
                "off_m"     => (int)($dev['light_off_minute']?? 0),
            ],
            "latest_sensor" => $row ?: null
        ];
        json_response($resp);
    }

    json_response(["status"=>"error","message"=>"Unknown cmd"], 400);
}

// =========================
// POST endpoints
// =========================
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $in = read_input();
    $device_id   = $in['device_id']   ?? '';
    $device_code = $in['device_code'] ?? '';
    if (!$device_id || !$device_code) json_response(["status"=>"error","message"=>"Missing device credentials"], 400);

    $dev = get_device_row($conn, $device_id, $device_code);
    if (!$dev) json_response(["status"=>"unauthorized"], 403);

    // ---- Device ACK (relay actuals + optional one-shot dose ACK) ----
    if (($in['action'] ?? '') === 'ack') {
        $rev = (int)($in['rev'] ?? -1);
        $waterA = isset($in['water_actual'])    ? (int)!!$in['water_actual']    : null;
        $growA  = isset($in['grow_actual'])     ? (int)!!$in['grow_actual']     : null;
        $solA   = isset($in['solenoid_actual']) ? (int)!!$in['solenoid_actual'] : null;
        $mixA   = isset($in['mixer_actual'])    ? (int)!!$in['mixer_actual']    : null;
        $doseAck= isset($in['dose_ack'])        ? (int)$in['dose_ack']          : null;

        $sql = "UPDATE devices SET last_ack_rev=?, last_applied_at=NOW(), last_seen=NOW()";
        $vals=[$rev]; $types="i";
        if ($waterA!==null){$sql.=",water_pump_actual=?";$types.="i";$vals[]=$waterA;}
        if ($growA !==null){$sql.=",growlight_actual=?";$types.="i";$vals[]=$growA;}
        if ($solA  !==null){$sql.=",solenoid_actual=?";$types.="i";$vals[]=$solA;}
        if ($mixA  !==null){$sql.=",mixer_actual=?";$types.="i";$vals[]=$mixA;}
        if ($doseAck!==null){$sql.=",last_dose_ack=?";$types.="i";$vals[]=$doseAck;}
        $sql.=" WHERE device_id=? AND code=?";$types.="ss";$vals[]=$device_id;$vals[]=$device_code;

        $stmt=$conn->prepare($sql);
        if(!$stmt) json_response(["status"=>"error","message"=>"Prepare failed: ack update"],500);
        $stmt->bind_param($types,...$vals);
        $stmt->execute();$stmt->close();

        json_response(["status"=>"ok","rev"=>$rev, "dose_ack"=>$doseAck]);
    }

    // ---- Dashboard: set commands/config and/or queue a one-shot dose ----
    if (($in['action'] ?? '') === 'set_commands') {
        // Relay & plant config fields
        $map=['auto'=>'automation','water'=>'water_pump','grow'=>'growlight','solenoid'=>'solenoid','mixer'=>'mixer',
              'plant_name'=>'plant_name','ppm_min'=>'ppm_min','ppm_max'=>'ppm_max',
              'ph_target'=>'ph_target','ph_min'=>'ph_min','ph_max'=>'ph_max',
              'on_h'=>'light_on_hour','on_m'=>'light_on_minute','off_h'=>'light_off_hour','off_m'=>'light_off_minute'];

        $cols=[];$vals=[];$types='';

        foreach($map as $k=>$col){
            if(!array_key_exists($k,$in)) continue;
            $val=$in[$k];
            if(in_array($k,['auto','water','grow','solenoid','mixer'],true)){$val=(int)!!$val;$types.='i';}
            elseif(in_array($k,['ppm_min','ppm_max','on_h','on_m','off_h','off_m'],true)){$val=(int)$val;$types.='i';}
            elseif(in_array($k,['ph_target','ph_min','ph_max'],true)){$val=(float)$val;$types.='d';}
            else{$val=(string)$val;$types.='s';}
            $cols[]="$col=?";$vals[]=$val;
        }

        // Optional: queue a single dosing shot (monotonic dose_req and its parameters)
        // Example JSON from dashboard:
        //  { "action":"set_commands", ..., "dose_req":102, "dose_channel":"PH_DOWN", "dose_ml":1.5, "dose_ms":0 }
        $dose_fields_present = false;
        if (array_key_exists('dose_req',$in)) {
            $dose_req = (int)$in['dose_req'];
            if ($dose_req > 0) {
                $cols[]="dose_req=?"; $vals[]=$dose_req; $types.="i";
                $dose_fields_present = true;
            }
        }
        if (array_key_exists('dose_channel',$in)) {
            $cols[]="dose_channel=?"; $vals[]=(string)$in['dose_channel']; $types.="s";
            $dose_fields_present = true;
        }
        if (array_key_exists('dose_ml',$in)) {
            // allow null/empty → set to NULL by passing NULL in SQL? simplest: store as float or 0
            $cols[]="dose_ml=?"; $vals[]=(float)$in['dose_ml']; $types.="d";
            $dose_fields_present = true;
        }
        if (array_key_exists('dose_ms',$in)) {
            $cols[]="dose_ms=?"; $vals[]=(int)$in['dose_ms']; $types.="i";
            $dose_fields_present = true;
        }

        // Always bump commands_rev on any change so device re-polls once
        $cols[]="commands_rev=commands_rev+1";
        $cols[]="commands_updated_at=NOW()";
        $sql="UPDATE devices SET ".implode(",",$cols)." WHERE device_id = ? AND code = ?";
        $stmt=$conn->prepare($sql);
        if(!$stmt) json_response(["status"=>"error","message"=>"Prepare failed: update devices"],500);

        $types.='ss'; $vals[]=$device_id; $vals[]=$device_code;
        $stmt->bind_param($types, ...$vals);
        $stmt->execute(); $stmt->close();

        $row=get_device_row($conn,$device_id,$device_code);
        $resp=["status"=>"ok","rev"=>(int)$row['commands_rev']];

        // If dosing was queued, include the current dose_req for confirmation
        if ($dose_fields_present) {
            $resp["dose_req"]     = isset($row['dose_req']) ? (int)$row['dose_req'] : 0;
            $resp["dose_channel"] = $row['dose_channel'] ?? null;
            if (isset($row['dose_ml'])) $resp["dose_ml"] = (float)$row['dose_ml'];
            if (isset($row['dose_ms'])) $resp["dose_ms"] = (int)$row['dose_ms'];
        }

        json_response($resp);
    }
    
    // ---- Default: sensor data logging ----
    // Basic sensors
    $plant = $in['plant']      ?? null;
    $ph    = $in['ph']         ?? null;
    $tds   = $in['ppm']        ?? null;
    $wt    = $in['waterTemp']  ?? null;
    $air   = $in['temp']       ?? null;
    $hum   = $in['humidity']   ?? null;
    $h     = $in['hour']       ?? null;
    $m     = $in['minute']     ?? null;
    $s     = $in['second']     ?? null;
    
    // New ultrasonic water-level data
    $drumD      = $in['drumD']      ?? null;      // distance sensor -> water (cm)
    $drumDepth  = $in['drumDepth']  ?? null;      // water depth (cm)
    $drumLiters = $in['drumLiters'] ?? null;      // estimated liters
    
    $sql = "INSERT INTO sensor_data (
                device_id,
                plant,
                ph,
                tds,
                water_temp,
                air_temp,
                humidity,
                hour,
                minute,
                second,
                drumD,
                drumDepth,
                drumLiters
            ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)";
    
    $stmt = $conn->prepare($sql);
    if (!$stmt) {
        json_response(["status" => "error", "message" => "Prepare failed: insert sensor_data"], 500);
    }
    
    // types: s,s,d,d,d,d,d,i,i,i,d,d,d
    $stmt->bind_param(
        "ssdddddiiiddd",
        $device_id,
        $plant,
        $ph,
        $tds,
        $wt,
        $air,
        $hum,
        $h,
        $m,
        $s,
        $drumD,
        $drumDepth,
        $drumLiters
    );
    
    $stmt->execute();
    $stmt->close();
    
    json_response(["status" => "data_received"]);

}

json_response(["status"=>"error","message"=>"Unsupported request"],405);
