#include <ros/ros.h>
#include <actionlib/server/simple_action_server.h>
#include <smobex_explorer_action_skill_server/smobex_explorer_action_skill_server.h>

#include <moveit/move_group_interface/move_group_interface.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit_msgs/DisplayRobotState.h>
#include <moveit_msgs/DisplayTrajectory.h>

#include <moveit/trajectory_processing/iterative_time_parameterization.h>
#include <moveit/robot_trajectory/robot_trajectory.h>

#include <tf/LinearMath/Matrix3x3.h>
#include <tf/LinearMath/Quaternion.h>
#include <tf/LinearMath/Vector3.h>
#include <tf/tf.h>
#include <tf/transform_datatypes.h>

#include <sensor_msgs/PointCloud2.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

#include <pcl/point_types.h>
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>

#include <pcl/console/time.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>

#include <pcl/features/normal_3d.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/segmentation/conditional_euclidean_clustering.h>

#include <smobex_explorer/explorer.h>

typedef pcl::PointXYZRGBA PointTypeIO;

sensor_msgs::PointCloud2 cloud_clusters_publish;
sensor_msgs::PointCloud2 centroid_clusters_publish;
float octomap_resolution = 0.1;

using namespace std;

class aPose
{
public:
  float score;
  geometry_msgs::PoseStamped pose;
  int arrow_id;
  visualization_msgs::MarkerArray boxes;
};

bool cmp_aPose(const aPose &a, const aPose &b)
{
  return a.score > b.score;
}

bool customRegionGrowing(const PointTypeIO &point_a, const PointTypeIO &point_b, float squared_distance)
{
  if (squared_distance < (octomap_resolution * 2) * (octomap_resolution * 2) * 1.1)
  {
    return true;
  }
  else
  {
    return false;
  }
}

std::vector<geometry_msgs::Point> findClusters(sensor_msgs::PointCloud2ConstPtr unknown_cloud)
{
  std::vector<geometry_msgs::Point> centroids_vect;

  // Data containers used
  pcl::PointCloud<PointTypeIO>::Ptr cloud_out(new pcl::PointCloud<PointTypeIO>);
  pcl::IndicesClustersPtr clusters(new pcl::IndicesClusters);

  pcl::PointXYZRGBA point_add_rgba;
  pcl::PointXYZ point_add, centroid_pcl;
  // pcl::PointCloud<pcl::PointXYZ> all_centroids_pcl;
  pcl::PointCloud<PointTypeIO> all_centroids_pcl;

  // Load the input point cloud
  pcl::fromROSMsg(*unknown_cloud, *cloud_out);

  // Set up a Conditional Euclidean Clustering class
  pcl::ConditionalEuclideanClustering<PointTypeIO> cec(true);
  cec.setInputCloud(cloud_out);
  cec.setConditionFunction(&customRegionGrowing);
  cec.setClusterTolerance(octomap_resolution * 3);
  cec.segment(*clusters);

  geometry_msgs::Point centroid;

  for (int i = 0; i < clusters->size(); ++i)
  {
    pcl::CentroidPoint<pcl::PointXYZ> centroid_points;

    int label_r = ((double)rand() / RAND_MAX) * 255;
    int label_g = ((double)rand() / RAND_MAX) * 255;
    int label_b = ((double)rand() / RAND_MAX) * 255;

    for (int j = 0; j < (*clusters)[i].indices.size(); ++j)
    {
      cloud_out->points[(*clusters)[i].indices[j]].r = label_r;
      cloud_out->points[(*clusters)[i].indices[j]].g = label_g;
      cloud_out->points[(*clusters)[i].indices[j]].b = label_b;

      point_add_rgba = cloud_out->points[(*clusters)[i].indices[j]];

      pcl::copyPoint(point_add_rgba, point_add);

      centroid_points.add(point_add);
    }

    centroid_points.get(centroid_pcl);

    centroid.x = centroid_pcl.x;
    centroid.y = centroid_pcl.y;
    centroid.z = centroid_pcl.z;

    ROS_INFO_STREAM("Centroid i: " << centroid);

    centroids_vect.push_back(centroid);

    PointTypeIO centroid_pcl_colored;

    centroid_pcl_colored.x = centroid_pcl.x;
    centroid_pcl_colored.y = centroid_pcl.y;
    centroid_pcl_colored.z = centroid_pcl.z;

    centroid_pcl_colored.r = label_r;
    centroid_pcl_colored.g = label_g;
    centroid_pcl_colored.b = label_b;
    // centroid_pcl_colored.a = 1;

    all_centroids_pcl.push_back(centroid_pcl_colored);
  }

  // Save the output point cloud
  // ROS_INFO("SAVING PCL");
  // pcl::io::savePCDFile("output.pcd", *cloud_out);

  pcl::toROSMsg(*cloud_out, cloud_clusters_publish);
  pcl::toROSMsg(all_centroids_pcl, centroid_clusters_publish);

  return centroids_vect;
}

geometry_msgs::Quaternion getOrientation(geometry_msgs::PoseStamped pose, geometry_msgs::Point point)
{
  tf::Vector3 z_direction, y_direction, x_direction, rand_vector;
  tf::Matrix3x3 rotation_matrix;
  tf::Quaternion view_orientation;

  geometry_msgs::Quaternion quat_out;

#if 0

  pcl::PointCloud<pcl::PointXYZ> unknown_pcl;

  pcl::fromROSMsg(*unknown_cloud, unknown_pcl);

  size_t n_points = unknown_pcl.size();
  pcl::PointXYZ point_pcl;

    pcl::CentroidPoint<pcl::PointXYZ> centroid;

    for (pcl::PointCloud<pcl::PointXYZ>::iterator it = unknown_pcl.begin(); it != unknown_pcl.end(); it++)
    {
        centroid.add(*it);
    }

    centroid.get(point_pcl);

#elif 0

  int pt_number = ((double)rand() / RAND_MAX) * n_points;
  point_pcl = unknown_pcl.at(pt_number);

  float xc = point_pcl.x;
  float yc = point_pcl.y;
  float zc = point_pcl.z;

#endif

  float xc = point.x;
  float yc = point.y;
  float zc = point.z;

  float x = pose.pose.position.x;
  float y = pose.pose.position.y;
  float z = pose.pose.position.z;

  z_direction.setX(xc - x);
  z_direction.setY(yc - y);
  z_direction.setZ(zc - z);

  rand_vector.setX((double)rand() / RAND_MAX);
  rand_vector.setY((double)rand() / RAND_MAX);
  rand_vector.setZ((double)rand() / RAND_MAX);

  y_direction = z_direction.cross(rand_vector);
  // y_direction.setZ(-1 * abs(y_direction.getZ()));
  y_direction.normalize();

  x_direction = y_direction.cross(z_direction);
  x_direction.normalize();

  rotation_matrix.setValue(x_direction.getX(), y_direction.getX(), z_direction.getX(), x_direction.getY(),
                           y_direction.getY(), z_direction.getY(), x_direction.getZ(), y_direction.getZ(),
                           z_direction.getZ());

  rotation_matrix.getRotation(view_orientation);
  view_orientation.normalize();

  tf::quaternionTFToMsg(view_orientation, quat_out);

  return quat_out;
}

SmobexExplorerActionSkill::SmobexExplorerActionSkill(std::string name) : as_(nh_, name, boost::bind(&SmobexExplorerActionSkill::executeCB, this, _1), false),
                                                                         action_name_(name)
{
  as_.start();
}

SmobexExplorerActionSkill::~SmobexExplorerActionSkill()
{
}

void SmobexExplorerActionSkill::executeCB(const smobex_explorer_action_skill_msgs::SmobexExplorerActionSkillGoalConstPtr &goal)
{
  // geometry_msgs::PoseStamped best_pose;

  static const std::string PLANNING_GROUP = "manipulator";

  // ros::AsyncSpinner spinner(1); // TODO see if improves performance
  // spinner.start();

  ros::NodeHandle n;

  ros::param::get("/octomap_server_node/resolution", octomap_resolution);

  ros::Publisher pub_cloud_clusters = n.advertise<sensor_msgs::PointCloud2>("/clusters_cloud", 10);
  ros::Publisher pub_centers_clusters = n.advertise<sensor_msgs::PointCloud2>("/clusters_centers", 10);

  ros::Publisher pub_arrows = n.advertise<visualization_msgs::MarkerArray>("/pose_arrows", 10);
  ros::Publisher pub_space = n.advertise<visualization_msgs::MarkerArray>("/discovered_space", 10);

  moveit::planning_interface::MoveGroupInterface move_group(PLANNING_GROUP);
  moveit::planning_interface::PlanningSceneInterface planning_scene_interface;
  moveit::planning_interface::MoveGroupInterface::Plan my_plan;

  const robot_state::JointModelGroup *joint_model_group =
      move_group.getCurrentState()->getJointModelGroup(PLANNING_GROUP);

  std_msgs::ColorRGBA green_color;
  green_color.r = 0.0;
  green_color.g = 1.0;
  green_color.b = 0.0;
  green_color.a = 1.0;

  // int step = 1;
  float min_range = 0;
  float max_range = 1;
  float width_FOV = M_PI;
  float height_FOV = M_PI;
  std::string frame_id = "/world";

  // ros::param::get("~" + ros::names::remap("step"), step);
  ros::param::get("~" + ros::names::remap("min_range"), min_range);
  ros::param::get("~" + ros::names::remap("max_range"), max_range);
  ros::param::get("~" + ros::names::remap("width_FOV"), width_FOV);
  ros::param::get("~" + ros::names::remap("height_FOV"), height_FOV);
  ros::param::get("~" + ros::names::remap("frame_id"), frame_id);

  // evaluatePose pose_test(20, 0.8, 3.5, 58 * M_PI / 180, 45 * M_PI / 180);
  // evaluatePose pose_test(step, min_range, max_range, width_FOV, height_FOV);
  evaluatePose pose_test(min_range, max_range, width_FOV, height_FOV);

  int n_poses = goal->n_poses;
  float threshold = goal->threshold;
  float max_reach = 0.951;

  int arrow_id = -1;
  float best_score = 1;
  int best_arrow_id;

  srand(time(NULL));

  visualization_msgs::MarkerArray all_poses;
  visualization_msgs::MarkerArray single_view_boxes;
  sensor_msgs::PointCloud2ConstPtr unknown_cloud = NULL;
  std::vector<aPose> poses_vector;
  geometry_msgs::Point observation_point;
  geometry_msgs::PoseStamped target_pose;
  geometry_msgs::PoseStamped best_pose;
  moveit_msgs::Constraints constraints;
  moveit_msgs::JointConstraint joint3_constraint;
  geometry_msgs::Quaternion quat_orient;
  geometry_msgs::Quaternion q_arrow;
  moveit::core::RobotStatePtr current_state;
  std::vector<double> joint_group_positions;
  tf::Quaternion q_rot, q_new;
  visualization_msgs::Marker arrow;
  std::vector<geometry_msgs::Point> clusters_centroids;

  constraints.name = "joint3_limit";

  joint3_constraint.joint_name = "joint_3";
  joint3_constraint.position = 0;
  joint3_constraint.tolerance_below = M_PI / 180 * 60;
  joint3_constraint.tolerance_above = M_PI / 180 * 75;
  joint3_constraint.weight = 1;

  constraints.joint_constraints.push_back(joint3_constraint);

  //do
  while (best_score > threshold)
  {
    // visualization_msgs::MarkerArray all_poses;
    arrow_id = -1;
    best_score = -1;

    move_group.clearPoseTargets();

    unknown_cloud = ros::topic::waitForMessage<sensor_msgs::PointCloud2>("/unknown_pc", n);

    pose_test.writeKnownOctomap();
    pose_test.writeUnknownOctomap();

    pose_test.writeUnknownCloud(unknown_cloud);

    clusters_centroids = findClusters(unknown_cloud);

    if (clusters_centroids.size() > 0)
    {
      pub_cloud_clusters.publish(cloud_clusters_publish);

      centroid_clusters_publish.header.stamp = ros::Time(0);
      centroid_clusters_publish.header.frame_id = frame_id;

      pub_centers_clusters.publish(centroid_clusters_publish);
    }

    //  move_group.setPlanningTime(0.4);

    const std::string end_effector_link = move_group.getEndEffectorLink();

    size_t total_clusters = clusters_centroids.size();

    size_t poses_by_cluster = n_poses / total_clusters;

    if (poses_by_cluster < 1)
    {
      poses_by_cluster = 1;
    }

    ROS_INFO_STREAM("Number of clusters: " << total_clusters);
    ROS_INFO_STREAM("Poses by cluster: " << poses_by_cluster);

    for (size_t cluster_idx = 0; cluster_idx < total_clusters; cluster_idx++)
    {
      observation_point = clusters_centroids[cluster_idx];
      ROS_INFO_STREAM("Obervating towards: " << observation_point);
      // poses_vector.clear();

      for (size_t pose_idx = 0; pose_idx < poses_by_cluster; pose_idx++)
      {
        // bool set_target;
        aPose one_pose;

        target_pose = move_group.getRandomPose();
        target_pose.pose.position.x = abs(target_pose.pose.position.x);

        quat_orient = getOrientation(target_pose, observation_point);
        target_pose.pose.orientation = quat_orient;

        ROS_INFO_STREAM("Cluster " << cluster_idx + 1 << " of " << total_clusters << " Pose " << pose_idx + 1 << " of " << poses_by_cluster);

        tf::poseMsgToTF(target_pose.pose, pose_test.view_pose);

        arrow.header.stamp = ros::Time::now();
        arrow.header.frame_id = frame_id;

        arrow.id = ++arrow_id;

        arrow.type = visualization_msgs::Marker::ARROW;
        arrow.action = visualization_msgs::Marker::ADD;

        arrow.pose.position = target_pose.pose.position;

        tf::quaternionMsgToTF(target_pose.pose.orientation, q_new);

        q_rot.setRPY(0, -M_PI / 2, 0);
        q_new = q_new * q_rot;
        q_new.normalize();

        tf::quaternionTFToMsg(q_new, q_arrow);
        arrow.pose.orientation = q_arrow;

        arrow.scale.x = 0.10;
        arrow.scale.y = 0.02;
        arrow.scale.z = 0.02;

        pose_test.evalPose();

        ROS_INFO_STREAM("Score: " << pose_test.score);

        arrow.color = pose_test.score_color;

        // if (pose_test.score > best_score)
        // {
        //   best_score = pose_test.score;
        //   best_pose = target_pose;
        //   best_arrow_id = arrow_id;

        //   single_view_boxes = pose_test.discoveredBoxesVis(frame_id);
        // }

        one_pose.score = pose_test.score;
        one_pose.pose = target_pose;
        one_pose.arrow_id = arrow_id;
        // one_pose.boxes = pose_test.discoveredBoxesVis(frame_id);

        poses_vector.push_back(one_pose);

        all_poses.markers.push_back(arrow);
        pub_arrows.publish(all_poses);

        ROS_INFO("---------");
        ros::spinOnce();
      }
    }

    ROS_INFO("Sorting...");

    std::sort(poses_vector.begin(), poses_vector.end(), cmp_aPose);

    ROS_INFO("Sorted!");

    bool set_target = false;
    bool set_plan = false;

    int sorted_pose_idx = -1;

    move_group.setPlanningTime(0.5);
    move_group.setNumPlanningAttempts(5);

    do
    {
      sorted_pose_idx++;

      best_pose = poses_vector[sorted_pose_idx].pose;

      ROS_INFO_STREAM("Planning " << sorted_pose_idx << " ...");

      // set_target = move_group.setJointValueTarget(best_pose, end_effector_link);
      move_group.setPathConstraints(constraints);
      set_target = move_group.setPoseTarget(best_pose, end_effector_link);
      set_plan = (move_group.plan(my_plan) == moveit::planning_interface::MoveItErrorCode::SUCCESS);

      // std::vector<geometry_msgs::Pose> waypoints;
      // waypoints.push_back(best_pose.pose);

      // moveit_msgs::RobotTrajectory trajectory;
      // set_target = (move_group.computeCartesianPath(waypoints, 0.005, 0.0, trajectory, true) > 0.95);
      // my_plan.trajectory_ = trajectory;
      // set_plan = true;

      ROS_INFO("Target (pose goal) %s", set_target ? "SUCCESS" : "FAILED");
      ROS_INFO("Plan (pose goal) %s", set_plan ? "SUCCESS" : "FAILED");

      if (sorted_pose_idx == poses_vector.size() - 1)
      {
        ROS_INFO("Couldn't plan for any pose...");
        this->set_aborted();
        return;
      }

    } while ((set_target == false) || (set_plan == false));

    tf::poseMsgToTF(best_pose.pose, pose_test.view_pose);
    pose_test.evalPose();

    best_score = poses_vector[sorted_pose_idx].score;
    best_arrow_id = poses_vector[sorted_pose_idx].arrow_id;
    // single_view_boxes = poses_vector[sorted_pose_idx].boxes;
    single_view_boxes = pose_test.discoveredBoxesVis(frame_id);

    all_poses.markers[best_arrow_id].color = green_color;
    all_poses.markers[best_arrow_id].scale.x *= 2;
    all_poses.markers[best_arrow_id].scale.y *= 2;
    all_poses.markers[best_arrow_id].scale.z *= 2;

    pub_arrows.publish(all_poses);
    pub_space.publish(single_view_boxes);

    // ROS_INFO("getchar");
    // getchar();

    ROS_WARN("MOVING!!!");
    ROS_INFO_STREAM("Moving towards score " << best_score);

    bool success = (move_group.execute(my_plan) == moveit::planning_interface::MoveItErrorCode::SUCCESS);

    ROS_INFO("Execute (best pose goal) %s", success ? "SUCCESS" : "FAILED");

    ROS_INFO("---------");

    for (size_t id_arrow_mrk = 0; id_arrow_mrk < all_poses.markers.size(); id_arrow_mrk++)
    {
      all_poses.markers[id_arrow_mrk].action = visualization_msgs::Marker::DELETEALL;
    }
    pub_arrows.publish(all_poses);

    for (size_t id_vol = 0; id_vol < single_view_boxes.markers.size(); id_vol++)
    {
      single_view_boxes.markers[id_vol].action = visualization_msgs::Marker::DELETEALL;
    }
    pub_space.publish(single_view_boxes);

    all_poses.markers.clear();
    single_view_boxes.markers.clear();
    poses_vector.clear();
    clusters_centroids.clear();

    ros::Duration(5).sleep(); //Give time for map to update

  } //while (best_score > threshold);

  ROS_INFO_STREAM("Final best score: " << best_score);

  current_state = move_group.getCurrentState();
  current_state->copyJointGroupPositions(joint_model_group, joint_group_positions);

  joint_group_positions[0] = -M_PI / 2; // radians
  joint_group_positions[1] = 0.0;       // radians
  joint_group_positions[2] = 0.0;       // radians
  joint_group_positions[3] = 0.0;       // radians
  joint_group_positions[4] = 0.0;       // radians
  joint_group_positions[5] = 0.0;       // radians

  move_group.setJointValueTarget(joint_group_positions);
  move_group.move();

  this->set_succeeded();
}

void SmobexExplorerActionSkill::set_succeeded(std::string outcome)
{
  result_.percentage = 100;
  result_.skillStatus = action_name_.c_str();
  result_.skillStatus += ": Succeeded";
  result_.outcome = outcome;
  ROS_INFO("%s: Succeeded", action_name_.c_str());
  as_.setSucceeded(result_);
}

void SmobexExplorerActionSkill::set_aborted(std::string outcome)
{
  result_.percentage = 0;
  result_.skillStatus = action_name_.c_str();
  result_.skillStatus += ": Aborted";
  result_.outcome = outcome;
  ROS_INFO("%s: Aborted", action_name_.c_str());
  as_.setAborted(result_);
}
void SmobexExplorerActionSkill::feedback(float percentage)
{
  feedback_.percentage = percentage;
  feedback_.skillStatus = action_name_.c_str();
  feedback_.skillStatus += " Executing";
  ROS_INFO("%s: Executing. Percentage: %f%%.", action_name_.c_str(), percentage);
  as_.publishFeedback(feedback_);
}

bool SmobexExplorerActionSkill::check_preemption()
{
  if (as_.isPreemptRequested() || !ros::ok())
  {
    result_.percentage = 0;
    result_.skillStatus = action_name_.c_str();
    result_.skillStatus += ": Preempted";
    result_.outcome = "preempted";
    ROS_INFO("%s: Preempted", action_name_.c_str());
    as_.setPreempted(result_);
    return true;
  }
  else
  {
    return false;
  }
}
