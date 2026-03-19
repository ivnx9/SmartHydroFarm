-- phpMyAdmin SQL Dump
-- version 5.2.2
-- https://www.phpmyadmin.net/
--
-- Host: 127.0.0.1:3306
-- Generation Time: Mar 19, 2026 at 12:29 AM
-- Server version: 11.8.3-MariaDB-log
-- PHP Version: 7.2.34

SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
START TRANSACTION;
SET time_zone = "+00:00";


/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;

--
-- Database: `u485887040_shvf`
--

-- --------------------------------------------------------

--
-- Table structure for table `devices`
--

CREATE TABLE `devices` (
  `id` varchar(36) NOT NULL,
  `user_id` varchar(36) NOT NULL,
  `device_id` varchar(100) DEFAULT NULL,
  `code` text DEFAULT NULL,
  `created_at` datetime DEFAULT current_timestamp(),
  `automation` tinyint(1) NOT NULL DEFAULT 0,
  `water_pump` tinyint(1) NOT NULL DEFAULT 0,
  `growlight` tinyint(1) NOT NULL DEFAULT 0,
  `solenoid` tinyint(1) NOT NULL DEFAULT 0,
  `mixer` tinyint(1) NOT NULL DEFAULT 0,
  `plant_name` varchar(50) DEFAULT 'lettuce',
  `ppm_min` int(11) DEFAULT 560,
  `ppm_max` int(11) DEFAULT 840,
  `ph_target` float DEFAULT 6,
  `ph_min` float DEFAULT 5.5,
  `ph_max` float DEFAULT 6.5,
  `light_on_hour` tinyint(4) DEFAULT 6,
  `light_on_minute` tinyint(4) DEFAULT 0,
  `light_off_hour` tinyint(4) DEFAULT 20,
  `light_off_minute` tinyint(4) DEFAULT 0,
  `commands_rev` int(11) NOT NULL DEFAULT 0,
  `commands_updated_at` datetime DEFAULT NULL,
  `last_ack_rev` int(11) NOT NULL DEFAULT 0,
  `last_seen` datetime DEFAULT NULL,
  `water_pump_actual` tinyint(1) NOT NULL DEFAULT 0,
  `growlight_actual` tinyint(1) NOT NULL DEFAULT 0,
  `solenoid_actual` tinyint(1) NOT NULL DEFAULT 0,
  `mixer_actual` tinyint(1) NOT NULL DEFAULT 0,
  `last_applied_at` datetime DEFAULT NULL,
  `dose_req` int(11) NOT NULL DEFAULT 0,
  `dose_channel` varchar(16) DEFAULT NULL,
  `dose_ml` double DEFAULT NULL,
  `dose_ms` int(11) DEFAULT NULL,
  `last_dose_ack` int(11) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `otp_table`
--

CREATE TABLE `otp_table` (
  `email` varchar(100) NOT NULL,
  `otp` varchar(10) NOT NULL,
  `created_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `sensor_data`
--

CREATE TABLE `sensor_data` (
  `id` int(11) NOT NULL,
  `device_id` varchar(100) NOT NULL,
  `plant` varchar(100) DEFAULT NULL,
  `ph` float DEFAULT NULL,
  `tds` float DEFAULT NULL,
  `water_temp` float DEFAULT NULL,
  `air_temp` float DEFAULT NULL,
  `humidity` float DEFAULT NULL,
  `drumD` float DEFAULT NULL,
  `drumDepth` float DEFAULT NULL,
  `drumLiters` float DEFAULT NULL,
  `hour` int(11) DEFAULT NULL,
  `minute` int(11) DEFAULT NULL,
  `second` int(11) DEFAULT NULL,
  `timestamp` timestamp NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `users`
--

CREATE TABLE `users` (
  `id` varchar(36) NOT NULL DEFAULT uuid(),
  `username` varchar(50) NOT NULL,
  `email` varchar(100) NOT NULL,
  `password_hash` text NOT NULL,
  `name` varchar(100) NOT NULL,
  `activated` tinyint(1) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

--
-- Indexes for dumped tables
--

--
-- Indexes for table `devices`
--
ALTER TABLE `devices`
  ADD PRIMARY KEY (`id`),
  ADD KEY `user_id` (`user_id`),
  ADD KEY `idx_devices_deviceid` (`device_id`),
  ADD KEY `idx_devices_commandsrev` (`commands_rev`);

--
-- Indexes for table `otp_table`
--
ALTER TABLE `otp_table`
  ADD PRIMARY KEY (`email`);

--
-- Indexes for table `sensor_data`
--
ALTER TABLE `sensor_data`
  ADD PRIMARY KEY (`id`),
  ADD KEY `device_id` (`device_id`);

--
-- Indexes for table `users`
--
ALTER TABLE `users`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `username` (`username`),
  ADD UNIQUE KEY `email` (`email`);

--
-- AUTO_INCREMENT for dumped tables
--

--
-- AUTO_INCREMENT for table `sensor_data`
--
ALTER TABLE `sensor_data`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- Constraints for dumped tables
--

--
-- Constraints for table `devices`
--
ALTER TABLE `devices`
  ADD CONSTRAINT `devices_ibfk_1` FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE NO ACTION ON UPDATE NO ACTION;
COMMIT;

/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
