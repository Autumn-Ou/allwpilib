// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

package edu.wpi.first.math.kinematics;

/** Represents the wheel speeds for a differential drive drivetrain. */
public class HDriveWheelSpeeds {
  /** Speed of the left side of the robot. */
  public double leftMetersPerSecond;

  /** Speed of the right side of the robot. */
  public double rightMetersPerSecond;

  /** Speed of the lateral movement of the robot. */
  public double lateralMetersPerSecond;

  /** Constructs a HDriveWheelSpeeds with zeros for left, right and lateral speeds. */
  public HDriveWheelSpeeds() {}

  /**
   * Constructs a HDriveWheelSpeeds.
   *
   * @param leftMetersPerSecond The left speed.
   * @param rightMetersPerSecond The right speed.
   * @param lateralMetersPerSecond The lateral speed.
   */
  public HDriveWheelSpeeds(
      double leftMetersPerSecond, double rightMetersPerSecond, double lateralMetersPerSecond) {
    this.leftMetersPerSecond = leftMetersPerSecond;
    this.rightMetersPerSecond = rightMetersPerSecond;
    this.lateralMetersPerSecond = lateralMetersPerSecond;
  }

  /**
   * Renormalizes the wheel speeds if any either side is above the specified maximum.
   *
   * <p>Sometimes, after inverse kinematics, the requested speed from one or more wheels may be
   * above the max attainable speed for the driving motor on that wheel. To fix this issue, one can
   * reduce all the wheel speeds to make sure that all requested module speeds are at-or-below the
   * absolute threshold, while maintaining the ratio of speeds between wheels.
   *
   * @param attainableMaxSpeedMetersPerSecond The absolute max speed that a wheel can reach.
   */
  public void desaturate(double attainableMaxSpeedMetersPerSecond) {
    double realMaxSpeed = Math.max(Math.abs(leftMetersPerSecond), Math.abs(rightMetersPerSecond));

    if (realMaxSpeed > attainableMaxSpeedMetersPerSecond) {
      leftMetersPerSecond = leftMetersPerSecond / realMaxSpeed * attainableMaxSpeedMetersPerSecond;
      rightMetersPerSecond =
          rightMetersPerSecond / realMaxSpeed * attainableMaxSpeedMetersPerSecond;
    }
  }

  @Override
  public String toString() {
    return String.format(
        "HDriveWheelSpeeds(Left: %.2f m/s, Right: %.2f m/s, Lateral %.2f m/s)",
        leftMetersPerSecond, rightMetersPerSecond, lateralMetersPerSecond);
  }
}
