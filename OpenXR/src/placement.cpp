#include <glaze_openxr/session.h>

namespace glaze::openxr {

glaze::Placement toPlacement(const Pose& pose, double scale)
{
    const YawPitchRoll angles = toYawPitchRoll(pose.orientation);
    glaze::Placement placement;
    placement.at    = glaze::Point3{pose.position.x, pose.position.y, pose.position.z};
    placement.yaw   = static_cast<float>(angles.yawDegrees);
    placement.pitch = static_cast<float>(angles.pitchDegrees);
    placement.roll  = static_cast<float>(angles.rollDegrees);
    const auto s    = static_cast<float>(scale);
    placement.scale = glaze::Float3{s, s, s};
    return placement;
}

Pose fromPlacement(const glaze::Placement& placement)
{
    Pose pose;
    pose.position    = Vec3{placement.at.x, placement.at.y, placement.at.z};
    pose.orientation = fromYawPitchRoll(YawPitchRoll{placement.yaw, placement.pitch, placement.roll});
    return pose;
}

} // namespace glaze::openxr
