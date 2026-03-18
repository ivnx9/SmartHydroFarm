<?php
$servername = "localhost";
$username = ""; // put your username here
$password = ""; // put your password here
$dbname = ""; // put your database name here

// Create connection
$conn = new mysqli($servername, $username, $password, $dbname);

// Check connection
if ($conn->connect_error) {
    //die("Connection failed: " . $conn->connect_error);
 die(json_encode(["status" => "error", "message" => "Database connection failed: " . $conn->connect_error]));
}
?>
