#include "mc_com_controller.h"

#include <mc_rbdyn/Surface.h>
#include <mc_rtc/logging.h>
#include <mc_tasks/SurfaceTransformTask.h>

#include <RBDyn/FK.h>
#include <RBDyn/FV.h>

namespace mc_control
{

MCCoMController::MCCoMController(std::shared_ptr<mc_rbdyn::RobotModule> robot_module, double dt)
: MCController(robot_module, dt)
{
  qpsolver->addConstraintSet(contactConstraint);
  qpsolver->addConstraintSet(dynamicsConstraint);
  qpsolver->addConstraintSet(selfCollisionConstraint);
  qpsolver->addTask(postureTask);

  comTask.reset(new mc_tasks::CoMTask(robots(), robots().robotIndex()));
  postureTask->stiffness(1);
  postureTask->weight(1);
  comTask->weight(1000);
}

void MCCoMController::reset(const ControllerResetData & reset_data)
{
  MCController::reset(reset_data);
  comTask->reset();
  solver().addTask(comTask);
  if(robot().name() == "hrp2_drc")
  {
    qpsolver->setContacts(
        {mc_rbdyn::Contact(robots(), "LFullSole", "AllGround"), mc_rbdyn::Contact(robots(), "RFullSole", "AllGround")});
  }
  else if(robot().name() == "hrp4")
  {
    qpsolver->setContacts({
        mc_rbdyn::Contact(robots(), "LeftFoot", "AllGround"),
        mc_rbdyn::Contact(robots(), "RightFoot", "AllGround"),
    });
  }
  else
  {
    LOG_ERROR("MCCoMController does not support robot " << robot().name())
    LOG_ERROR_AND_THROW(std::runtime_error, "MCCoMController does not support your robot")
  }
}

bool MCCoMController::move_com(const Eigen::Vector3d & v)
{
  comTask->move_com(v);
  return true;
}

} // namespace mc_control
