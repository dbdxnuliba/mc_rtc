#include <mc_rbdyn/configuration_io.h>
#include <mc_rbdyn/rpy_utils.h>
#include <mc_tasks/CoPTask.h>
#include <mc_tasks/MetaTaskLoader.h>

namespace mc_tasks
{

namespace
{
  constexpr double MIN_PRESSURE = 0.5;  // [N]
}

CoPTask::CoPTask(const std::string & surfaceName,
      const mc_rbdyn::Robots & robots,
      unsigned int robotIndex,
      double stiffness, double weight)
  : AdmittanceTask(surfaceName, robots, robotIndex, stiffness, weight)
{
  name_ = "cop_" + robot_.name() + "_" + surfaceName;
}

void CoPTask::reset()
{
  targetCoP_ = Eigen::Vector2d::Zero();
  targetForce_ = Eigen::Vector3d::Zero();
  AdmittanceTask::reset();
}

void CoPTask::update()
{
  double pressure = std::max(0., measuredWrench().force().z());
  Eigen::Vector3d targetTorque = {+targetCoP_.y() * pressure, -targetCoP_.x() * pressure, 0.};
  AdmittanceTask::targetWrench({targetTorque, targetForce_});
  AdmittanceTask::update();
}

Eigen::Vector2d CoPTask::measuredCoP() const
{
  const sva::ForceVecd w_surf = measuredWrench();
  const double pressure = w_surf.force()(2);
  if (pressure < MIN_PRESSURE)
  {
    return Eigen::Vector2d::Zero();
  }
  const Eigen::Vector3d tau_surf = w_surf.couple();
  return Eigen::Vector2d(-tau_surf(1) / pressure, +tau_surf(0) / pressure);
}

void CoPTask::addToLogger(mc_rtc::Logger & logger)
{
  AdmittanceTask::addToLogger(logger);
  logger.addLogEntry(name_ + "_measured_cop",
                     [this]() -> Eigen::Vector2d
                     {
                     return measuredCoP();
                     });
  logger.addLogEntry(name_ + "_target_cop",
                     [this]() -> const Eigen::Vector2d &
                     {
                     return targetCoP_;
                     });
}

void CoPTask::removeFromLogger(mc_rtc::Logger & logger)
{
  AdmittanceTask::removeFromLogger(logger);
  logger.removeLogEntry(name_ + "_measured_cop");
  logger.removeLogEntry(name_ + "_target_cop");
}

std::function<bool(const mc_tasks::MetaTask&, std::string&)>
  CoPTask::buildCompletionCriteria(double dt,
                                   const mc_rtc::Configuration & config) const
{
  if(config.has("copError"))
  {
    double copError = config("copError");
    assert(copError >= 0);
    return [copError](const mc_tasks::MetaTask & t, std::string & out)
    {
      const auto & self = static_cast<const CoPTask&>(t);
      Eigen::Vector2d error = self.measuredCoP() - self.targetCoP();
      if(error.norm() < copError)
      {
        out += "CoP error";
        return true;
      }
      return false;
    };
  }
  if(config.has("force"))
  {
    Eigen::Vector3d force = config("force");
    Eigen::Vector3d dof = Eigen::Vector3d::Ones();
    for(size_t i = 0; i < 3; ++i)
    {
      if(std::isnan(force(i))) { dof(i) = 0.; force(i) = 0.; }
      else if(force(i) < 0) { dof(i) = -1.; }
    }
    return [dof,force](const mc_tasks::MetaTask & t, std::string & out)
    {
      const auto & self = static_cast<const CoPTask&>(t);
      Eigen::Vector3d f = self.measuredWrench().force();
      for(size_t i = 0; i < 3; ++i)
      {
        if(dof(i)*fabs(f(i)) < force(i))
        {
          return false;
        }
      }
      out += "force";
      return true;
    };
  }
  return AdmittanceTask::buildCompletionCriteria(dt, config);
}

} // mc_tasks

namespace
{

static bool registered = mc_tasks::MetaTaskLoader::register_load_function("cop",
  [](mc_solver::QPSolver & solver,
     const mc_rtc::Configuration & config)
  {
    auto t = std::make_shared<mc_tasks::CoPTask>(config("surface"), solver.robots(), config("robotIndex"));
    if(config.has("admittance")) { t->admittance(config("admittance")); }
    if(config.has("cop")) { t->targetCoP(config("cop")); }
    if(config.has("force")) { t->targetForce(config("force")); }
    if(config.has("pose")) { t->targetPose(config("pose")); }
    t->load(solver, config);
    return t;
  }
);

}
