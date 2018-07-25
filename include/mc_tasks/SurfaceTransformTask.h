#pragma once

#include <mc_tasks/TrajectoryTaskGeneric.h>

namespace mc_tasks
{

/*! \brief Control the Surface of a body

 * This task is thin wrapper around the appropriate tasks in Tasks.
 *
 */
struct MC_TASKS_DLLAPI SurfaceTransformTask : public TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>
{
public:
  /*! \brief Constructor
   *
   * \param surfaceName Name of the surface frame to control
   *
   * \param robots Robots controlled by this task
   *
   * \param robotIndex Index of the robot controlled by this task
   *
   * \param stiffness Task stiffness
   *
   * \param weight Task weight
   *
   */
  SurfaceTransformTask(const std::string & surfaceName, const mc_rbdyn::Robots
                  & robots, unsigned int robotIndex, double stiffness =
                  2.0, double weight = 500);

  /*! \brief Reset the task
   *
   * Set the task target to the current surface, and reset its target velocity
   * and acceleration to zero.
   *
   */
  virtual void reset() override;

  /*! \brief Get the body Surface target */
  sva::PTransformd target();

  /*! \brief Set the body Surface target
   *
   * \param pos Surface in world frame
   *
   */
  void target(const sva::PTransformd & pos);

  /*! \brief Retrieve the controlled surface name */
  std::string surface() { return surfaceName; }

  void addToLogger(mc_rtc::Logger & logger) override;

  void removeFromLogger(mc_rtc::Logger & logger) override;

  using TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::stiffness;
  using TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::damping;
  using TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::setGains;

  /*! \brief Set dimensional stiffness and damping
   *
   * \param stiffness Dimensional stiffness
   *
   * \param damping Dimensional damping
   *
   */
  void setGains(const sva::MotionVecd & stiffness, const sva::MotionVecd & damping)
  {
    return TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::setGains(stiffness.vector(), damping.vector());
  }

  /*! \brief Set dimensional stiffness
   *
   * Damping is automatically set to 2*sqrt(stiffness)
   *
   * \param stiffness Dimensional stiffness as a motion vector
   *
   */
  void stiffness(const sva::MotionVecd & stiffness)
  {
    return TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::stiffness(stiffness.vector());
  }

  /*! \brief Get dimensional stiffness as a motion vector */
  sva::MotionVecd mvStiffness()
  {
    return sva::MotionVecd(dimStiffness());
  }

  /*! \brief Set dimensional damping
   *
   * \param damping Dimensional damping as a motion vector
   *
   */
  void damping(const sva::MotionVecd & damping)
  {
    return TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::damping(damping.vector());
  }

  /*! \brief Get dimensional damping as a motion vector */
  sva::MotionVecd mvDamping()
  {
    return sva::MotionVecd(dimDamping());
  }

  /*! \brief Set trajectory task's reference velocity from motion vector in
   * body coordinates.
   *
   * \param velB Reference velocity in body coordinates, i.e. velocity of the
   * surface frame in the surface frame.
   *
   */
  void refVelB(const sva::MotionVecd & velB)
  {
    return TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::refVel(velB.vector());
  }

  /*! \brief Set trajectory task's reference acceleration from motion vector.
   *
   * \param acc Reference acceleration.
   *
   */
  void refAccel(const sva::MotionVecd & accel)
  {
    return TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::refAccel(accel.vector());
  }

protected:
  std::string surfaceName;

  void addToGUI(mc_rtc::gui::StateBuilder & gui) override;

  /* Don't use parent's refVel() as the velocity frame (spatial or body) is
   * ambiguous. */
  using TrajectoryTaskGeneric<tasks::qp::SurfaceTransformTask>::refVel;
};

}
