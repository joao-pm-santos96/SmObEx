#include <ros/ros.h>

#include <geometry_msgs/Point.h>
#include <geometry_msgs/PoseArray.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>
#include <visualization_msgs/MarkerArray.h>

#include <math.h>
#include <cmath>

#include <tf/LinearMath/Quaternion.h>
#include <tf/LinearMath/Vector3.h>
#include <tf/tf.h>
#include <tf/transform_datatypes.h>
#include <tf_conversions/tf_eigen.h>

#include <pcl-1.8/pcl/filters/frustum_culling.h>
#include <pcl/point_types.h>
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>

#include <octomap/OcTreeBaseImpl.h>
#include <octomap/OcTreeKey.h>
#include <octomap/OccupancyOcTreeBase.h>
#include <octomap/math/Pose6D.h>
#include <octomap/math/Quaternion.h>
#include <octomap/math/Utils.h>
#include <octomap/math/Vector3.h>
#include <octomap/octomap.h>
#include <octomap_msgs/conversions.h>
#include <octomap_ros/conversions.h>

#include <eigen3/Eigen/Core>

#include <colormap/colormap.h>

/*
    PROTOTYPES AND TYPEDEFS
*/

class UnknownVoxel;

typedef std::map<size_t, UnknownVoxel> map_type;

/*
    CLASSES AND FUNCTIONS
*/

class UnknownVoxel
{
public:
    octomap::OcTreeKey key;
    octomap::point3d center;
    double distance_to_camera;
    bool to_visit;
    size_t map_key;

    // Constructor
    UnknownVoxel()
    {
        to_visit = true;
    }

    // Overloads
    bool operator==(const UnknownVoxel &rhs) const { return this->key == rhs.key; }
    bool operator==(const octomap::OcTreeKey &rhs) const { return this->key == rhs; }
};

bool compareVoxelDistance(UnknownVoxel const &a, UnknownVoxel const &b)
{
    return a.distance_to_camera > b.distance_to_camera;
}

bool compareVoxelDistanceIterator(map_type::iterator const &a, map_type::iterator const &b)
{
    return a->second.distance_to_camera > b->second.distance_to_camera;
}

class Smobex
{
private:
    tf::Pose _view_pose;

    float _min_range;
    float _max_range;
    float _width_FOV;
    float _height_FOV;
    float _score;

    ros::NodeHandle _nh;

    //TODO: set a default
    std_msgs::ColorRGBA _score_color;

    octomap::point3d _min_bbx, _max_bbx;
    octomap::OcTree *_known_octree = NULL;
    octomap::OcTree *_unknown_octree = NULL;
    octomap::KeySet _first_keys;
	octomap::KeySet _posterior_keys;
    octomap::point3d_list _ray_points_list;

    pcl::PointCloud<pcl::PointXYZ> _unknown_centers_pcl;

public:
    Smobex(float min_range, float max_range, float width_FOV, float height_FOV)
    {
        _min_range = _min_range;
        _max_range = _max_range;
        _width_FOV = _width_FOV;
        _height_FOV = _height_FOV;

        // TODO: remove this
        ros::param::get("x_max", _max_bbx.x());
        ros::param::get("y_max", _max_bbx.y());
        ros::param::get("z_max", _max_bbx.z());

        ros::param::get("x_min", _min_bbx.x());
        ros::param::get("y_min", _min_bbx.y());
        ros::param::get("z_min", _min_bbx.z());
    }

    ~Smobex()
    {
    }

    // Callback to store the real Octomap
    void writeKnownOctomap(std::string topic)
    {
        using namespace octomap;

        AbstractOcTree *tree = NULL;

        if (_known_octree != NULL)
        {
            delete (_known_octree);
        }

        octomap_msgs::OctomapConstPtr map = ros::topic::waitForMessage<octomap_msgs::Octomap>(topic, _nh);
        tree = msgToMap(*map);
        _known_octree = dynamic_cast<OcTree *>(tree);
    }

    // Callback to store the Octomap in which the free voxels are unknown in the real Octomap
    void writeUnknownOctomap(std::string topic)
    {
        using namespace octomap;

        AbstractOcTree *tree = NULL;

        if (_unknown_octree != NULL)
        {
            delete (_unknown_octree);
        }

        octomap_msgs::OctomapConstPtr map = ros::topic::waitForMessage<octomap_msgs::Octomap>(topic, _nh);
        tree = msgToMap(*map);
        _unknown_octree = dynamic_cast<OcTree *>(tree);
    }

    // Callback to store the point cloud that are the centers of the unknown voxels
    void writeUnknownCloud(sensor_msgs::PointCloud2ConstPtr unknown_cloud)
    {
        pcl::fromROSMsg(*unknown_cloud, _unknown_centers_pcl);
    }

    // Method to evaluate a pose (voxel based ray casting)
    void evalPose()
    {
        using namespace octomap;
        using namespace octomath;

        Vector3 origin;

        // TODO: fix this
        // while (_known_octree == NULL || _unknown_octree == NULL)
        // {
        //     ROS_WARN("No OcTrees... Did you call the writting functions? Calling them automatically.");

        //     writeKnownOctomap();
        //     writeUnknownOctomap();
        // }

        _first_keys.clear();
        _posterior_keys.clear();

        Pose6D octo_pose = poseTfToOctomap(_view_pose);

        origin.x() = octo_pose.x();
        origin.y() = octo_pose.y();
        origin.z() = octo_pose.z();

        if (_known_octree != NULL && _unknown_octree != NULL)
        {
            pcl::FrustumCulling<pcl::PointXYZ> fc;
            fc.setInputCloud(_unknown_centers_pcl.makeShared());
            fc.setVerticalFOV(_height_FOV * 180 / M_PI);
            fc.setHorizontalFOV(_width_FOV * 180 / M_PI);
            fc.setNearPlaneDistance(_min_range);
            fc.setFarPlaneDistance(_max_range);

            Eigen::Affine3d pose_origin_affine;
            Eigen::Matrix4d pose_orig;
            Eigen::Matrix4d cam2robot;

            // Eigen, TF and PCL use different standards for the orientations, so
            // lets do some conversions
            tf::poseTFToEigen(_view_pose, pose_origin_affine);

            pose_orig = pose_origin_affine.matrix();

            cam2robot << 0, 0, 1, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1;

            Eigen::Matrix4d pose_new_d = pose_orig * cam2robot;
            Eigen::Matrix4f pose_new = pose_new_d.cast<float>();

            fc.setCameraPose(pose_new);

            // Filter the points (of the unknown point cloud) that are within the camera's frustum
            pcl::PointCloud<pcl::PointXYZ> points_inside;
            fc.filter(points_inside);

            std::vector<map_type::iterator> unknown_voxels;
            map_type map_unknown_voxels;

            // ROS_INFO_STREAM("N points unknown: " << points_inside.size());

            // Iterate thru each unknown centroid within the frustum, and store it in a set of voxels to test
            for (pcl::PointCloud<pcl::PointXYZ>::iterator it = points_inside.begin(); it != points_inside.end(); it++)
            {
                UnknownVoxel a_voxel;
                octomap::OcTreeKey::KeyHash hash;

                a_voxel.key = _unknown_octree->coordToKey(it->x, it->y, it->z);
                a_voxel.center = Vector3(it->x, it->y, it->z);
                a_voxel.distance_to_camera = origin.distance(Vector3(it->x, it->y, it->z));
                a_voxel.map_key = hash(a_voxel.key);

                map_unknown_voxels.insert(std::pair<size_t, UnknownVoxel>(a_voxel.map_key, a_voxel));

                map_type::iterator map_it = map_unknown_voxels.find(a_voxel.map_key);

                unknown_voxels.push_back(map_it);
            }

            // Sort these unknown voxels by their distance to the camera, from furthest to closest
            std::sort(unknown_voxels.begin(), unknown_voxels.end(), compareVoxelDistanceIterator);

            KeyRay ray_keys_before, ray_keys_after;
            Vector3 voxel_center, end_point, direction;

            // Perform raycast for each unknown voxel within the frustum, from furthest to closest
            for (size_t idx = 0; idx < unknown_voxels.size(); idx++)
            {
                octomap::OcTreeKey::KeyHash hash;

                if (map_unknown_voxels[unknown_voxels[idx]->second.map_key].to_visit == false)
                {
                    continue;
                }

                // Get the voxel center
                voxel_center = _unknown_octree->keyToCoord(unknown_voxels[idx]->second.key);

                // Get the direction from the camera to voxel
                direction = voxel_center - origin;

                // Cast ray to obtain the end point of the ray
                bool occupied = _known_octree->castRay(origin, direction, end_point, true, _max_range);

                // Compute the intersected voxels (by this ray) that go from the camera to the end point
                _unknown_octree->computeRayKeys(origin, end_point, ray_keys_before);

                bool first = true;

                // Iterative cycle that will remove the intersected voxels from the set of voxels to test
                // (since they have been passed by, there is no need to test them again)
                for (KeyRay::iterator it_key = ray_keys_before.begin(); it_key != ray_keys_before.end(); it_key++)
                {
                    bool C1 = origin.distance(_unknown_octree->keyToCoord(*it_key)) >= _min_range;

                    if (C1)
                    {
                        bool C2 = _unknown_octree->search(*it_key);

                        if (C2)
                        {

                            // For scoring the pose, we want to differentiate the first intersected voxels from the next ones
                            if (first)
                            {
                                _first_keys.insert(*it_key);
                                first = false;
                            }
                            else
                            {
                                _posterior_keys.insert(*it_key);
                            }

                            // Store the casted rays for visualization
                            _ray_points_list.push_back(origin);
                            _ray_points_list.push_back(end_point);

                            // Mark the voxel to not be visited (i.e. tested) again
                            map_type::iterator map_it = map_unknown_voxels.find(hash(*it_key));

                            if (map_it != map_unknown_voxels.end())
                            {
                                map_it->second.to_visit = false;
                            }
                        }
                    }
                }

                // If the endpoint of the ray was an occupied voxel, we know that are some voxels after it that
                // are impossible to be passed by a ray, so there is no need to further test them either
                if (occupied)
                {
                    _unknown_octree->computeRayKeys(end_point, voxel_center, ray_keys_after);

                    for (KeyRay::iterator it_key = ray_keys_after.begin(); it_key != ray_keys_after.end(); it_key++)
                    {
                        map_type::iterator map_it = map_unknown_voxels.find(hash(*it_key));

                        if (map_it != map_unknown_voxels.end())
                        {
                            map_it->second.to_visit = false;
                        }
                    }
                }
            }

            getScore();
        }
        else
        {
            ROS_INFO("No OcTree.");
        }
    }

    // Compute the score of the pose
	// See the dissertation for further knowledge
	void getScore()
	{
		using namespace octomap;

		float resolution = _known_octree->getResolution();
		float one_volume = resolution * resolution * resolution;

		float weight = 0.5;

		float found_volume = (_first_keys.size() + _posterior_keys.size() * weight) * one_volume;

		point3d deltas = _max_bbx - _min_bbx;

		float total_volume = deltas.x() * deltas.y() * deltas.z();
		float inner_volume =
			(deltas.x() - resolution * 2) * (deltas.y() - resolution * 2) * (deltas.z() - resolution * 2);
		float outer_volume = total_volume - inner_volume;

		float score_volume = outer_volume + inner_volume * weight;

		_score = found_volume / score_volume;

		// Get a color for the frustum accordingly to the score
		getColor();
	}

    // Get a color for the frustum accordingly to the score
	void getColor()
	{
		class_colormap frustum_color("jet", 64, 1, true);

		_score_color = frustum_color.color(_score * 64);
	}

    // Get the potentially discovered voxels's centroids
	octomap::point3d_collection getDiscoveredCenters()
	{
		using namespace octomap;

		point3d_collection discovered_centers;

		for (KeySet::iterator it = _posterior_keys.begin(); it != _posterior_keys.end(); it++)
		{
			discovered_centers.push_back(_unknown_octree->keyToCoord(*it));
		}

		return discovered_centers;
	}

    // Marker Array with the voxels that are potentially discovered
	visualization_msgs::MarkerArray getDiscoveredVoxelsMarker(std::string frame_id)
	{
		using namespace std;
		using namespace octomap;

		visualization_msgs::MarkerArray all_boxes;
		double unknown_octree_depth = _unknown_octree->getTreeDepth();
		all_boxes.markers.resize(unknown_octree_depth + 1);

		ros::Time t = ros::Time::now();

		std_msgs::ColorRGBA blue;
		blue.r = 0.0;
		blue.g = 0.0;
		blue.b = 1.0;
		blue.a = 1.0;

		for (OcTree::iterator it = _unknown_octree->begin(), end = _unknown_octree->end(); it != end; ++it)
		{
			OcTreeKey node_key = it.getKey();

			if (_first_keys.find(node_key) != _first_keys.end())
			{
				double size = it.getSize();
				double x = it.getX();
				double y = it.getY();
				double z = it.getZ();

				unsigned idx = it.getDepth();
				assert(idx < all_boxes.markers.size());

				geometry_msgs::Point cubeCenter;
				cubeCenter.x = x;
				cubeCenter.y = y;
				cubeCenter.z = z;

				all_boxes.markers[idx].points.push_back(cubeCenter);
				// all_boxes.markers[idx].color = blue;
				// all_boxes.markers[idx].colors.push_back(blue);
				// all_boxes.markers[idx].ns = "first_boxes";
			}
			else if (_posterior_keys.find(node_key) != _posterior_keys.end())
			{
				double size = it.getSize();
				double x = it.getX();
				double y = it.getY();
				double z = it.getZ();

				unsigned idx = it.getDepth();
				assert(idx < all_boxes.markers.size());

				geometry_msgs::Point cubeCenter;
				cubeCenter.x = x;
				cubeCenter.y = y;
				cubeCenter.z = z;

				all_boxes.markers[idx].points.push_back(cubeCenter);
				// all_boxes.markers[idx].color = light_blue;
				// all_boxes.markers[idx].colors.push_back(light_blue);
				// all_boxes.markers[idx].ns = "posterior_boxes";
			}
		}

		for (unsigned i = 0; i < all_boxes.markers.size(); i++)
		{
			double size = _unknown_octree->getNodeSize(i);

			all_boxes.markers[i].header.frame_id = frame_id;
			all_boxes.markers[i].header.stamp = t;
			all_boxes.markers[i].ns = "Pose";
			all_boxes.markers[i].id = i;
			all_boxes.markers[i].type = visualization_msgs::Marker::CUBE_LIST;
			all_boxes.markers[i].scale.x = size;
			all_boxes.markers[i].scale.y = size;
			all_boxes.markers[i].scale.z = size;
			all_boxes.markers[i].color = blue;

			if (all_boxes.markers[i].points.size() > 0)
			{
				all_boxes.markers[i].action = visualization_msgs::Marker::ADD;
			}
			else
			{
				all_boxes.markers[i].action = visualization_msgs::Marker::DELETE;
			}
		}

		return all_boxes;
	}

    // Marker Array with the casted ray lines
	visualization_msgs::Marker getRayCastingLinesMarer(std::string frame_id)
	{
		using namespace std;

		visualization_msgs::Marker line_vis;

		line_vis.header.frame_id = frame_id;
		line_vis.header.stamp = ros::Time::now();
		line_vis.ns = "Pose rays";
		line_vis.action = visualization_msgs::Marker::ADD;
		line_vis.pose.orientation.w = 1.0;
		line_vis.id = 0;
		line_vis.type = visualization_msgs::Marker::LINE_LIST;
		line_vis.scale.x = 0.001;

		line_vis.color.r = 0.5;
		line_vis.color.g = 0.5;
		line_vis.color.b = 0.5;
		line_vis.color.a = 1.0;

		for (octomap::point3d_list::iterator it = _ray_points_list.begin(); it != _ray_points_list.end(); it++)
		{
			geometry_msgs::Point point;
			point.x = it->octomath::Vector3::x();
			point.y = it->octomath::Vector3::y();
			point.z = it->octomath::Vector3::z();

			line_vis.points.push_back(point);
		}

		return line_vis;
	}

    // Marker Array with the frustum lines
	visualization_msgs::Marker getFrustumMarker(std::string frame_id)
	{
		using namespace std;

		visualization_msgs::Marker line_vis;

		line_vis.header.frame_id = frame_id;
		line_vis.header.stamp = ros::Time::now();
		line_vis.ns = "Frustum";
		line_vis.action = visualization_msgs::Marker::ADD;
		line_vis.pose.orientation.w = 1.0;
		line_vis.id = 0;
		line_vis.type = visualization_msgs::Marker::LINE_LIST;
		line_vis.scale.x = 0.01;

		line_vis.color = _score_color;

		pcl::PointCloud<pcl::PointXYZ> frustum_cloud_start, frustum_cloud_end;

		float rad_h = (M_PI - _height_FOV) / 2;

		// The frustum is essentially to rectangles connected, so lets compute the 2 * 4 points that make both rectangles
		// and store them as two point clouds (in the world's frame)
		for (int i = 0; i < 2; i++)
		{
			float rad_w = (M_PI - _width_FOV) / 2;

			for (int n = 0; n < 2; n++)
			{
				float x1 = _min_range * sin(rad_h) * cos(rad_w);
				float z1 = _min_range * sin(rad_h) * sin(rad_w);
				float y1 = _min_range * cos(rad_h);
				float x2 = _max_range * sin(rad_h) * cos(rad_w);
				float z2 = _max_range * sin(rad_h) * sin(rad_w);
				float y2 = _max_range * cos(rad_h);

				frustum_cloud_start.push_back(pcl::PointXYZ(x1, y1, z1));
				frustum_cloud_end.push_back(pcl::PointXYZ(x2, y2, z2));

				rad_w += _width_FOV;
			}
			rad_h += _height_FOV;
		}

		// And transform them to the camera's frame
		pcl_ros::transformPointCloud(frustum_cloud_start, frustum_cloud_start, _view_pose);
		pcl_ros::transformPointCloud(frustum_cloud_end, frustum_cloud_end, _view_pose);

		// Go thru the point clouds and "import" the points to the marker
		// (this will generate the lines connecting the rectangle's corners...
		for (size_t i = 0; i < 4; i++)
		{
			geometry_msgs::Point p1, p2;
			pcl::PointXYZ start, end;

			start = frustum_cloud_start.at(i);
			end = frustum_cloud_end.at(i);

			p1.x = start.x;
			p1.y = start.y;
			p1.z = start.z;

			p2.x = end.x;
			p2.y = end.y;
			p2.z = end.z;

			line_vis.points.push_back(p1);
			line_vis.points.push_back(p2);
		}

		// ...and this will create the rectangles themselves)
		line_vis.points.push_back(line_vis.points[0]);
		line_vis.points.push_back(line_vis.points[2]);

		line_vis.points.push_back(line_vis.points[2]);
		line_vis.points.push_back(line_vis.points[6]);

		line_vis.points.push_back(line_vis.points[4]);
		line_vis.points.push_back(line_vis.points[6]);

		line_vis.points.push_back(line_vis.points[0]);
		line_vis.points.push_back(line_vis.points[4]);

		line_vis.points.push_back(line_vis.points[1]);
		line_vis.points.push_back(line_vis.points[3]);

		line_vis.points.push_back(line_vis.points[3]);
		line_vis.points.push_back(line_vis.points[7]);

		line_vis.points.push_back(line_vis.points[5]);
		line_vis.points.push_back(line_vis.points[7]);

		line_vis.points.push_back(line_vis.points[1]);
		line_vis.points.push_back(line_vis.points[5]);

		return line_vis;
	}
};