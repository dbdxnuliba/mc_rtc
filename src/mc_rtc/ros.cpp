#include <mc_rtc/ros.h>
#include <mc_rtc/config.h>
#include <mc_rbdyn/robot.h>

#include <mc_rtc/logging.h>

#include <RBDyn/FK.h>

#ifdef MC_RTC_HAS_ROS
#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/JointState.h>
#include <tf2_ros/transform_broadcaster.h>

#include <mutex>
#include <queue>
#include <thread>
#endif

#ifdef MC_RTC_HAS_ROS
namespace mc_rtc
{

static const std::vector<std::string> REF_JOINT_ORDER = {
  "RLEG_JOINT0", "RLEG_JOINT1", "RLEG_JOINT2", "RLEG_JOINT3", "RLEG_JOINT4", "RLEG_JOINT5",
  "LLEG_JOINT0", "LLEG_JOINT1", "LLEG_JOINT2", "LLEG_JOINT3", "LLEG_JOINT4", "LLEG_JOINT5",
  "CHEST_JOINT0", "CHEST_JOINT1", "HEAD_JOINT0", "HEAD_JOINT1",
  "RARM_JOINT0", "RARM_JOINT1", "RARM_JOINT2", "RARM_JOINT3", "RARM_JOINT4", "RARM_JOINT5", "RARM_JOINT6", "RARM_JOINT7",
  "LARM_JOINT0", "LARM_JOINT1", "LARM_JOINT2", "LARM_JOINT3", "LARM_JOINT4", "LARM_JOINT5", "LARM_JOINT6", "LARM_JOINT7",
  "RHAND_JOINT0", "RHAND_JOINT1", "RHAND_JOINT2", "RHAND_JOINT3", "RHAND_JOINT4",
  "LHAND_JOINT0", "LHAND_JOINT1", "LHAND_JOINT2", "LHAND_JOINT3", "LHAND_JOINT4"};

static const std::map<std::string, unsigned int> LGRIPPER_JOINTS = {
  {"LARM_JOINT7"  , 0},
  {"LHAND_JOINT0" , 1},
  {"LHAND_JOINT1" , 2},
  {"LHAND_JOINT2" , 3},
  {"LHAND_JOINT3" , 4},
  {"LHAND_JOINT4" , 5}
};

static const std::map<std::string, unsigned int> RGRIPPER_JOINTS = {
  {"RARM_JOINT7"  , 0},
  {"RHAND_JOINT0" , 1},
  {"RHAND_JOINT1" , 2},
  {"RHAND_JOINT2" , 3},
  {"RHAND_JOINT3" , 4},
  {"RHAND_JOINT4" , 5}
};

inline geometry_msgs::TransformStamped PT2TF(const sva::PTransformd & X, const ros::Time & tm, const std::string & from, const std::string & to, unsigned int seq)
{
  geometry_msgs::TransformStamped msg;
  msg.header.seq = seq;
  msg.header.stamp = tm;
  msg.header.frame_id = from;
  msg.child_frame_id = to;

  Eigen::Vector4d q = Eigen::Quaterniond(X.rotation()).inverse().coeffs();
  const Eigen::Vector3d & t = X.translation();

  msg.transform.translation.x = t.x();
  msg.transform.translation.y = t.y();
  msg.transform.translation.z = t.z();

  msg.transform.rotation.w = q.w();
  msg.transform.rotation.x = q.x();
  msg.transform.rotation.y = q.y();
  msg.transform.rotation.z = q.z();

  return msg;
}

struct RobotPublisher
{
public:
  RobotPublisher(ros::NodeHandle & nh)
  : nh(nh),
    j_state_pub(this->nh.advertise<sensor_msgs::JointState>("joint_states", 1)),
    imu_pub(this->nh.advertise<sensor_msgs::Imu>("imu", 1)),
    odom_pub(this->nh.advertise<nav_msgs::Odometry>("odom", 1)),
    iter_since_start(0),
    imu_noise(Eigen::Vector3d::Zero()),
    tf_caster(),
    running(true), seq(0), msgs(),
    th(std::bind(&RobotPublisher::publishThread, this))
  {
  }

  ~RobotPublisher()
  {
    running = false;
    th.join();
  }

  void update(double dt, const mc_rbdyn::Robot & robot, const RTC::TimedPoint3D & p, const RTC::TimedOrientation3D & rpy, const RTC::TimedAngularVelocity3D & rate, const RTC::TimedAcceleration3D & gsensor, const std::vector<double> & lGq, const std::vector<double> & rGq)
  {
    ros::Time tm = ros::Time::now();
    sensor_msgs::JointState msg;
    sensor_msgs::Imu imu;
    std::vector<geometry_msgs::TransformStamped> tfs;

    rbd::MultiBodyConfig mbc = robot.mbc();
    if(lGq.size() == LGRIPPER_JOINTS.size())
    {
      for(const auto & j : LGRIPPER_JOINTS)
      {
        mbc.q[robot.jointIndexByName(j.first)][0] = lGq[j.second];
      }
    }
    if(rGq.size() == RGRIPPER_JOINTS.size())
    {
      for(const auto & j : RGRIPPER_JOINTS)
      {
        mbc.q[robot.jointIndexByName(j.first)][0] = rGq[j.second];
      }
    }
    rbd::forwardKinematics(robot.mb(), mbc);

    msg.header.seq = ++seq;
    msg.header.stamp = tm;
    msg.header.frame_id = "";
    msg.name = REF_JOINT_ORDER;
    msg.position.reserve(REF_JOINT_ORDER.size());
    for(size_t i = 0; i < REF_JOINT_ORDER.size(); ++i)
    {
      const std::string & jName = REF_JOINT_ORDER[i];
      msg.position.push_back(mbc.q[robot.jointIndexByName(jName)][0]);
    }
    msg.velocity.resize(0);
    msg.effort.resize(0);

    imu.header = msg.header;
#ifdef MC_RTC_HAS_HRPSYS_BASE
    if(iter_since_start >= 2000)
    {
      imu.linear_acceleration.x = gsensor.data.ax - imu_noise.x();
      imu.linear_acceleration.y = gsensor.data.ay - imu_noise.y();
      imu.linear_acceleration.z = gsensor.data.az - imu_noise.z();
    }
    else
    {
      imu_noise.x() += gsensor.data.ax;
      imu_noise.y() += gsensor.data.ay;
      imu_noise.z() += gsensor.data.az;
      imu.linear_acceleration.x = 0;
      imu.linear_acceleration.y = 0;
      imu.linear_acceleration.z = 0;
    }
    iter_since_start++;
    if(iter_since_start == 2000)
    {
      imu_noise /= 2000;
    }
#endif

    nav_msgs::Odometry odom;
    odom.header = msg.header;
    odom.header.frame_id = "CHEST_LINK1",
    odom.child_frame_id = "robot_odom",
    /* Position of the sensor in CHEST_LINK1 frame */
    odom.pose.pose.position.x = -0.13;
    odom.pose.pose.position.y = 0.;
    odom.pose.pose.position.z = 0.118;
    odom.pose.pose.orientation.w = 1;
    odom.pose.pose.orientation.x = 0;
    odom.pose.pose.orientation.y = 0;
    odom.pose.pose.orientation.z = 0;
    odom.pose.covariance.fill(0);
    /* Provide linear and angular velocity */
#ifdef MC_RTC_HAS_HRPSYS_BASE
    odom.twist.twist.linear.x = gsensor.data.ax * dt;
    odom.twist.twist.linear.y = gsensor.data.ay * dt;
    odom.twist.twist.linear.z = gsensor.data.az * dt;
    odom.twist.twist.angular.x = rate.data.avx;
    odom.twist.twist.angular.y = rate.data.avy;
    odom.twist.twist.angular.z = rate.data.avz;
    odom.twist.covariance.fill(0);
#endif

    tfs.push_back(PT2TF(robot.bodyTransform(robot.mb().body(0).name())*mbc.parentToSon[0], tm, std::string("/robot_map"), robot.mb().body(0).name(), seq));
    for(int j = 1; j < robot.mb().nrJoints(); ++j)
    {
      const auto & predIndex = robot.mb().predecessor(j);
      const auto & succIndex = robot.mb().successor(j);
      const auto & predName = robot.mb().body(predIndex).name();
      const auto & succName = robot.mb().body(succIndex).name();
      const auto & X_predp_pred = robot.bodyTransform(predName);
      const auto & X_succp_succ = robot.bodyTransform(succName);
      tfs.push_back(PT2TF(X_succp_succ*mbc.parentToSon[static_cast<unsigned int>(j)]*X_predp_pred.inv(), tm, predName, succName, seq));
    }

    sva::PTransformd X_0_hl1 = mbc.bodyPosW[robot.bodyIndexByName("HEAD_LINK1")];
    // Calib 2016/02/02
    sva::PTransformd X_hl1_xtion = sva::PTransformd(Eigen::Quaterniond(0.995971, -0.00632932, 0.0894339, 0.00188757).inverse(), Eigen::Vector3d(0.109125, 0.0055295, 0.0915054));
    tfs.push_back(PT2TF(X_hl1_xtion, tm, "HEAD_LINK1", "xtion_link", seq));

    sva::PTransformd X_0_xtion = X_hl1_xtion * X_0_hl1;

    // Relate the robot tf tree with SLAM tf tree
    tfs.push_back(PT2TF(X_0_xtion, tm, "robot_map", "odom", seq));
#ifdef MC_RTC_HAS_HRPSYS_BASE
    sva::PTransformd X_0_base_odom = sva::PTransformd(
                        Eigen::Quaterniond(sva::RotZ(rpy.data.y)*sva::RotY(rpy.data.p)*sva::RotX(-rpy.data.r).inverse()),
                        Eigen::Vector3d(p.data.x, p.data.y, p.data.z));
    sva::PTransformd X_0_base = mbc.bodyPosW[0];
    sva::PTransformd X_base_xtion = X_0_xtion * (X_0_base.inv());
    tfs.push_back(PT2TF(X_0_base_odom, tm, "/robot_map", "/odom_base_link", seq));
    tfs.push_back(PT2TF(X_base_xtion, tm, "/odom_base_link", "/odom_xtion_link", seq));
#endif

    if(seq % 5 == 0)
    {
      mut.lock();
      msgs.push({msg, tfs, imu, odom});
      mut.unlock();
    }
  }

  void reset_imu_offset()
  {
    imu_noise = Eigen::Vector3d::Zero();
    iter_since_start = 0;
  }
private:
  ros::NodeHandle & nh;
  ros::Publisher j_state_pub;
  ros::Publisher imu_pub;
  ros::Publisher odom_pub;
  unsigned int iter_since_start;
  Eigen::Vector3d imu_noise;
  tf2_ros::TransformBroadcaster tf_caster;

  struct RobotStateData
  {
    sensor_msgs::JointState js;
    std::vector<geometry_msgs::TransformStamped> tfs;
    sensor_msgs::Imu imu;
    nav_msgs::Odometry odom;
  };

  bool running;
  uint32_t seq;
  std::queue<RobotStateData> msgs;
  std::thread th;
  std::mutex mut;

  void publishThread()
  {
    ros::Rate rt(100);
    while(running && ros::ok())
    {
      while(msgs.size())
      {
        if(mut.try_lock())
        {
          const auto & msg = msgs.front();
          try
          {
            j_state_pub.publish(msg.js);
            imu_pub.publish(msg.imu);
            odom_pub.publish(msg.odom);
            tf_caster.sendTransform(msg.tfs);
          }
          catch(const ros::serialization::StreamOverrunException & e)
          {
            LOG_ERROR("EXCEPTION WHILE PUBLISHING STATE")
            LOG_WARNING(e.what())
          }
          msgs.pop();
          mut.unlock();
        }
      }
      rt.sleep();
    }
  }
};

inline bool ros_init(const std::string & name)
{
  int argc = 0;
  char * argv[] = {0};
  ros::init(argc, argv, name.c_str());
  if(!ros::master::check())
  {
    LOG_WARNING("ROS master is not available, continue without ROS functionalities")
    return false;
  }
  return true;
}

struct ROSBridgeImpl
{
  ROSBridgeImpl()
  : ros_is_init(ros_init("mc_rtc")),
    nh(ros_is_init ? new ros::NodeHandle() : 0),
    rpub(ros_is_init ? new RobotPublisher(*nh) : 0)
  {
  }
  bool ros_is_init;
  std::shared_ptr<ros::NodeHandle> nh;
  std::shared_ptr<RobotPublisher> rpub;
};

std::unique_ptr<ROSBridgeImpl> ROSBridge::impl = std::unique_ptr<ROSBridgeImpl>(new ROSBridgeImpl());

std::shared_ptr<ros::NodeHandle> ROSBridge::get_node_handle()
{
  return impl->nh;
}

void ROSBridge::update_robot_publisher(double dt, const mc_rbdyn::Robot & robot, const RTC::TimedPoint3D & p, const RTC::TimedOrientation3D & rpy, const RTC::TimedAngularVelocity3D & rate, const RTC::TimedAcceleration3D & gsensor, const std::vector<double> & lGq, const std::vector<double> & rGq)
{
  if(impl->rpub)
  {
    impl->rpub->update(dt, robot, p, rpy, rate, gsensor, lGq, rGq);
  }
}

void ROSBridge::reset_imu_offset()
{
  if(impl->rpub)
  {
    impl->rpub->reset_imu_offset();
  }
}

void ROSBridge::shutdown()
{
  ros::shutdown();
}

}
#else
namespace ros
{
  class NodeHandle {};
}

namespace mc_rtc
{

struct ROSBridgeImpl
{
  ROSBridgeImpl() : nh(0) {}
  std::shared_ptr<ros::NodeHandle> nh;
};

std::unique_ptr<ROSBridgeImpl> ROSBridge::impl = std::unique_ptr<ROSBridgeImpl>(new ROSBridgeImpl());

std::shared_ptr<ros::NodeHandle> ROSBridge::get_node_handle()
{
  return impl->nh;
}

void ROSBridge::update_robot_publisher(double, const mc_rbdyn::Robot &, const RTC::TimedPoint3D &, const RTC::TimedOrientation3D &, const RTC::TimedAngularVelocity3D &, const RTC::TimedAcceleration3D &, const std::vector<double> &, const std::vector<double> &)
{
}

void ROSBridge::reset_imu_offset()
{
}

void ROSBridge::shutdown()
{
}

}
#endif
