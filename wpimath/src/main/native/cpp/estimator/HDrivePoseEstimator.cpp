// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "frc/estimator/HDrivePoseEstimator.h"

#include <wpi/timestamp.h>

#include "frc/StateSpaceUtil.h"
#include "frc/estimator/AngleStatistics.h"
#include "wpimath/MathShared.h"

using namespace frc;

HDrivePoseEstimator::InterpolationRecord
HDrivePoseEstimator::InterpolationRecord::Interpolate(
    HDriveKinematics& kinematics, InterpolationRecord endValue,
    double i) const {
  if (i < 0) {
    return *this;
  } else if (i > 1) {
    return endValue;
  } else {
    // Find the interpolated left distance.
    auto left = wpi::Lerp(this->leftDistance, endValue.leftDistance, i);
    // Find the interpolated right distance.
    auto right = wpi::Lerp(this->rightDistance, endValue.rightDistance, i);
    // Find the interpolated lateral distance.
    auto lateral =
        wpi::Lerp(this->lateralDistance, endValue.lateralDistance, i);

    // Find the new gyro angle.
    auto gyro = wpi::Lerp(this->gyroAngle, endValue.gyroAngle, i);

    // Create a twist to represent this changed based on the interpolated
    // sensor inputs.
    auto twist = kinematics.ToTwist2d(
        left - leftDistance, right - rightDistance, lateral - lateralDistance);
    twist.dtheta = (gyro - gyroAngle).Radians();

    return {pose.Exp(twist), gyro, left, right, lateral};
  }
}

HDrivePoseEstimator::HDrivePoseEstimator(HDriveKinematics& kinematics,
                                         const Rotation2d& gyroAngle,
                                         units::meter_t leftDistance,
                                         units::meter_t rightDistance,
                                         units::meter_t lateralDistance,
                                         const Pose2d& initialPose)
    : HDrivePoseEstimator{kinematics,         gyroAngle,       leftDistance,
                          rightDistance,      lateralDistance, initialPose,
                          {0.02, 0.02, 0.01}, {0.1, 0.1, 0.1}} {}

HDrivePoseEstimator::HDrivePoseEstimator(
    HDriveKinematics& kinematics, const Rotation2d& gyroAngle,
    units::meter_t leftDistance, units::meter_t rightDistance,
    units::meter_t lateralDistance, const Pose2d& initialPose,
    const wpi::array<double, 3>& stateStdDevs,
    const wpi::array<double, 3>& visionMeasurementStdDevs)
    : m_kinematics{kinematics},
      m_odometry{gyroAngle, leftDistance, rightDistance, lateralDistance,
                 initialPose} {
  for (size_t i = 0; i < 3; ++i) {
    m_q[i] = stateStdDevs[i] * stateStdDevs[i];
  }

  SetVisionMeasurementStdDevs(visionMeasurementStdDevs);
}

void HDrivePoseEstimator::SetVisionMeasurementStdDevs(
    const wpi::array<double, 3>& visionMeasurementStdDevs) {
  wpi::array<double, 3> r{wpi::empty_array};
  for (size_t i = 0; i < 3; ++i) {
    r[i] = visionMeasurementStdDevs[i] * visionMeasurementStdDevs[i];
  }

  // Solve for closed form Kalman gain for continuous Kalman filter with A = 0
  // and C = I. See wpimath/algorithms.md.
  for (size_t row = 0; row < 3; ++row) {
    if (m_q[row] == 0.0) {
      m_visionK(row, row) = 0.0;
    } else {
      m_visionK(row, row) =
          m_q[row] / (m_q[row] + std::sqrt(m_q[row] * r[row]));
    }
  }
}

void HDrivePoseEstimator::ResetPosition(const Rotation2d& gyroAngle,
                                        units::meter_t leftDistance,
                                        units::meter_t rightDistance,
                                        units::meter_t lateralDistance,
                                        const Pose2d& pose) {
  // Reset state estimate and error covariance
  m_odometry.ResetPosition(gyroAngle, leftDistance, rightDistance,
                           lateralDistance, pose);
  m_poseBuffer.Clear();
}

Pose2d HDrivePoseEstimator::GetEstimatedPosition() const {
  return m_odometry.GetPose();
}

void HDrivePoseEstimator::AddVisionMeasurement(const Pose2d& visionRobotPose,
                                               units::second_t timestamp) {
  // Step 0: If this measurement is old enough to be outside the pose buffer's
  // timespan, skip.
  if (!m_poseBuffer.GetInternalBuffer().empty() &&
      m_poseBuffer.GetInternalBuffer().front().first - kBufferDuration >
          timestamp) {
    return;
  }

  // Step 1: Get the estimated pose from when the vision measurement was made.
  auto sample = m_poseBuffer.Sample(timestamp);

  if (!sample.has_value()) {
    return;
  }

  // Step 2: Measure the twist between the odometry pose and the vision pose.
  auto twist = sample.value().pose.Log(visionRobotPose);

  // Step 3: We should not trust the twist entirely, so instead we scale this
  // twist by a Kalman gain matrix representing how much we trust vision
  // measurements compared to our current pose.
  frc::Vectord<3> k_times_twist =
      m_visionK *
      frc::Vectord<3>{twist.dx.value(), twist.dy.value(), twist.dtheta.value()};

  // Step 4: Convert back to Twist2d.
  Twist2d scaledTwist{units::meter_t{k_times_twist(0)},
                      units::meter_t{k_times_twist(1)},
                      units::radian_t{k_times_twist(2)}};

  // Step 5: Reset Odometry to state at sample with vision adjustment.
  m_odometry.ResetPosition(
      sample.value().gyroAngle, sample.value().leftDistance,
      sample.value().rightDistance, sample.value().lateralDistance,
      sample.value().pose.Exp(scaledTwist));

  // Step 6: Record the current pose to allow multiple measurements from the
  // same timestamp
  m_poseBuffer.AddSample(
      timestamp, {GetEstimatedPosition(), sample.value().gyroAngle,
                  sample.value().leftDistance, sample.value().rightDistance,
                  sample.value().lateralDistance});

  // Step 7: Replay odometry inputs between sample time and latest recorded
  // sample to update the pose buffer and correct odometry.
  auto internal_buf = m_poseBuffer.GetInternalBuffer();

  auto first_newer_record =
      std::lower_bound(internal_buf.begin(), internal_buf.end(), timestamp,
                       [](const auto& pair, auto t) { return t > pair.first; });

  for (auto entry = first_newer_record + 1; entry != internal_buf.end();
       entry++) {
    UpdateWithTime(entry->first, entry->second.gyroAngle,
                   entry->second.leftDistance, entry->second.rightDistance,
                   entry->second.lateralDistance);
  }
}

Pose2d HDrivePoseEstimator::Update(const Rotation2d& gyroAngle,
                                   units::meter_t leftDistance,
                                   units::meter_t rightDistance,
                                   units::meter_t lateralDistance) {
  return UpdateWithTime(wpi::math::MathSharedStore::GetTimestamp(), gyroAngle,
                        leftDistance, rightDistance, lateralDistance);
}

Pose2d HDrivePoseEstimator::UpdateWithTime(units::second_t currentTime,
                                           const Rotation2d& gyroAngle,
                                           units::meter_t leftDistance,
                                           units::meter_t rightDistance,
                                           units::meter_t lateralDistance) {
  m_odometry.Update(gyroAngle, leftDistance, rightDistance, lateralDistance);

  // fmt::print("odo, {}, {}, {}, {}, {}, {}\n",
  //   gyroAngle.Radians(),
  //   leftDistance,
  //   rightDistance,
  //   GetEstimatedPosition().X(),
  //   GetEstimatedPosition().Y(),
  //   GetEstimatedPosition().Rotation().Radians()
  // );

  m_poseBuffer.AddSample(currentTime,
                         {GetEstimatedPosition(), gyroAngle, leftDistance,
                          rightDistance, lateralDistance});

  return GetEstimatedPosition();
}
