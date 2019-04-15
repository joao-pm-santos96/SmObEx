/*
 * Copyright (c) 2011, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ros/ros.h>

#include <interactive_markers/interactive_marker_server.h>
#include <interactive_markers/menu_handler.h>

#include <tf/transform_broadcaster.h>
#include <tf/tf.h>

#include <math.h>

#include <smobex_explorer/explorer.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

using namespace visualization_msgs;

// %Tag(vars)%
boost::shared_ptr<interactive_markers::InteractiveMarkerServer> server;
interactive_markers::MenuHandler menu_handler;

ros::Publisher pub_lines;
ros::Publisher pub_space;
ros::Publisher pub_text;

visualization_msgs::Marker line, text;
visualization_msgs::MarkerArray single_view_boxes;
bool publish = false;
// %EndTag(vars)%

void genAndEvalPose(geometry_msgs::Pose marker_pose)
{
    evaluatePose pose(20, 0.8, 10, 58 * M_PI / 180, 45 * M_PI / 180);
    tf::poseMsgToTF(marker_pose, pose.view_pose);

    pose.evalPose();

    line = pose.rayLinesVis("base_link", 0);
    text = pose.textVis("base_link");
    single_view_boxes = pose.discoveredBoxesVis("base_link", 0);

    publish = true;
}

// %Tag(Box)%
Marker makeBox(InteractiveMarker &msg)
{
    Marker marker;

    marker.type = Marker::CUBE;
    marker.scale.x = msg.scale * 0.45;
    marker.scale.y = msg.scale * 0.45;
    marker.scale.z = msg.scale * 0.45;
    marker.color.r = 0.5;
    marker.color.g = 0.5;
    marker.color.b = 0.5;
    marker.color.a = 1.0;

    return marker;
}

InteractiveMarkerControl &makeBoxControl(InteractiveMarker &msg)
{
    InteractiveMarkerControl control;
    control.always_visible = true;
    control.markers.push_back(makeBox(msg));
    msg.controls.push_back(control);

    return msg.controls.back();
}
// %EndTag(Box)%

// %Tag(frameCallback)%
void frameCallback(const ros::TimerEvent &)
{
    static uint32_t counter = 0;

    static tf::TransformBroadcaster br;

    tf::Transform t;

    ros::Time time = ros::Time::now();

    t.setOrigin(tf::Vector3(0.0, 0.0, sin(float(counter) / 140.0) * 2.0));
    t.setRotation(tf::Quaternion(0.0, 0.0, 0.0, 1.0));
    br.sendTransform(tf::StampedTransform(t, time, "base_link", "moving_frame"));

    t.setOrigin(tf::Vector3(0.0, 0.0, 0.0));
    t.setRotation(tf::createQuaternionFromRPY(0.0, float(counter) / 140.0, 0.0));
    br.sendTransform(tf::StampedTransform(t, time, "base_link", "rotating_frame"));

    counter++;
}
// %EndTag(frameCallback)%

// %Tag(processFeedback)%
void processFeedback(const visualization_msgs::InteractiveMarkerFeedbackConstPtr &feedback)
{
    std::ostringstream s;
    s << "Feedback from marker '" << feedback->marker_name << "' "
      << " / control '" << feedback->control_name << "'";

    std::ostringstream mouse_point_ss;
    if (feedback->mouse_point_valid)
    {
        mouse_point_ss << " at " << feedback->mouse_point.x
                       << ", " << feedback->mouse_point.y
                       << ", " << feedback->mouse_point.z
                       << " in frame " << feedback->header.frame_id;
    }

    switch (feedback->event_type)
    {
    case visualization_msgs::InteractiveMarkerFeedback::BUTTON_CLICK:
        ROS_INFO_STREAM(s.str() << ": button click" << mouse_point_ss.str() << ".");
        break;

    case visualization_msgs::InteractiveMarkerFeedback::MENU_SELECT:
        ROS_INFO_STREAM(s.str() << ": menu item " << feedback->menu_entry_id << " clicked" << mouse_point_ss.str() << ".");

        ROS_INFO_STREAM("GENERATING POSE");

        genAndEvalPose(feedback->pose);

        ROS_INFO_STREAM("POSE DONE");

        break;

    case visualization_msgs::InteractiveMarkerFeedback::POSE_UPDATE:
        ROS_INFO_STREAM(s.str() << ": pose changed"
                                << "\nposition = "
                                << feedback->pose.position.x
                                << ", " << feedback->pose.position.y
                                << ", " << feedback->pose.position.z
                                << "\norientation = "
                                << feedback->pose.orientation.w
                                << ", " << feedback->pose.orientation.x
                                << ", " << feedback->pose.orientation.y
                                << ", " << feedback->pose.orientation.z
                                << "\nframe: " << feedback->header.frame_id
                                << " time: " << feedback->header.stamp.sec << "sec, "
                                << feedback->header.stamp.nsec << " nsec");
        break;

        // case visualization_msgs::InteractiveMarkerFeedback::MOUSE_DOWN:
        //     ROS_INFO_STREAM(s.str() << ": mouse down" << mouse_point_ss.str() << ".");
        //     break;

        // case visualization_msgs::InteractiveMarkerFeedback::MOUSE_UP:
        //     ROS_INFO_STREAM(s.str() << ": mouse up" << mouse_point_ss.str() << ".");
        //     break;
    }

    server->applyChanges();
}
// %EndTag(processFeedback)%

double rand(double min, double max)
{
    double t = (double)rand() / (double)RAND_MAX;
    return min + t * (max - min);
}

void saveMarker(InteractiveMarker int_marker)
{
    server->insert(int_marker);
    server->setCallback(int_marker.name, &processFeedback);
}

////////////////////////////////////////////////////////////////////////////////////

// %Tag(6DOF)%
void make6DofMarker(bool fixed, unsigned int interaction_mode, const tf::Vector3 &position, bool show_6dof)
{
    InteractiveMarker int_marker;
    int_marker.header.frame_id = "base_link";
    tf::pointTFToMsg(position, int_marker.pose.position);
    // int_marker.scale = 1;
    int_marker.scale = 0.5;

    int_marker.name = "simple_6dof";
    int_marker.description = "Simple 6-DOF Control";

    // insert a box
    makeBoxControl(int_marker);
    int_marker.controls[0].interaction_mode = interaction_mode;

    InteractiveMarkerControl control;

    if (fixed)
    {
        int_marker.name += "_fixed";
        int_marker.description += "\n(fixed orientation)";
        control.orientation_mode = InteractiveMarkerControl::FIXED;
    }

    if (interaction_mode != visualization_msgs::InteractiveMarkerControl::NONE)
    {
        std::string mode_text;
        if (interaction_mode == visualization_msgs::InteractiveMarkerControl::MOVE_3D)
            mode_text = "MOVE_3D";
        if (interaction_mode == visualization_msgs::InteractiveMarkerControl::ROTATE_3D)
            mode_text = "ROTATE_3D";
        if (interaction_mode == visualization_msgs::InteractiveMarkerControl::MOVE_ROTATE_3D)
            mode_text = "MOVE_ROTATE_3D";
        int_marker.name += "_" + mode_text;
        int_marker.description = std::string("3D Control") + (show_6dof ? " + 6-DOF controls" : "") + "\n" + mode_text;
    }

    if (show_6dof)
    {
        control.orientation.w = 1;
        control.orientation.x = 1;
        control.orientation.y = 0;
        control.orientation.z = 0;
        control.name = "rotate_x";
        control.interaction_mode = InteractiveMarkerControl::ROTATE_AXIS;
        int_marker.controls.push_back(control);
        control.name = "move_x";
        control.interaction_mode = InteractiveMarkerControl::MOVE_AXIS;
        int_marker.controls.push_back(control);

        control.orientation.w = 1;
        control.orientation.x = 0;
        control.orientation.y = 1;
        control.orientation.z = 0;
        control.name = "rotate_z";
        control.interaction_mode = InteractiveMarkerControl::ROTATE_AXIS;
        int_marker.controls.push_back(control);
        control.name = "move_z";
        control.interaction_mode = InteractiveMarkerControl::MOVE_AXIS;
        int_marker.controls.push_back(control);

        control.orientation.w = 1;
        control.orientation.x = 0;
        control.orientation.y = 0;
        control.orientation.z = 1;
        control.name = "rotate_y";
        control.interaction_mode = InteractiveMarkerControl::ROTATE_AXIS;
        int_marker.controls.push_back(control);
        control.name = "move_y";
        control.interaction_mode = InteractiveMarkerControl::MOVE_AXIS;
        int_marker.controls.push_back(control);
    }

    server->insert(int_marker);
    server->setCallback(int_marker.name, &processFeedback);
    if (interaction_mode != visualization_msgs::InteractiveMarkerControl::NONE)
        menu_handler.apply(*server, int_marker.name);
}
// %EndTag(6DOF)%

// %Tag(Menu)%
void makeMenuMarker(const tf::Vector3 &position)
{
    InteractiveMarker int_marker;
    int_marker.header.frame_id = "base_link";
    tf::pointTFToMsg(position, int_marker.pose.position);
    int_marker.scale = 1;

    int_marker.name = "context_menu";
    int_marker.description = "Context Menu\n(Right Click)";

    InteractiveMarkerControl control;

    control.interaction_mode = InteractiveMarkerControl::MENU;
    control.name = "menu_only_control";

    Marker marker = makeBox(int_marker);
    control.markers.push_back(marker);
    control.always_visible = true;
    int_marker.controls.push_back(control);

    server->insert(int_marker);
    server->setCallback(int_marker.name, &processFeedback);
    menu_handler.apply(*server, int_marker.name);
}
// %EndTag(Menu)%

// %Tag(Button)%
void makeButtonMarker(const tf::Vector3 &position)
{
    InteractiveMarker int_marker;
    int_marker.header.frame_id = "base_link";
    tf::pointTFToMsg(position, int_marker.pose.position);
    int_marker.scale = 1;

    int_marker.name = "button";
    int_marker.description = "Button\n(Left Click)";

    InteractiveMarkerControl control;

    control.interaction_mode = InteractiveMarkerControl::BUTTON;
    control.name = "button_control";

    Marker marker = makeBox(int_marker);
    control.markers.push_back(marker);
    control.always_visible = true;
    int_marker.controls.push_back(control);

    server->insert(int_marker);
    server->setCallback(int_marker.name, &processFeedback);
}
// %EndTag(Button)%

// %Tag(main)%
int main(int argc, char **argv)
{
    ros::init(argc, argv, "basic_controls");
    ros::NodeHandle n;

    // create a timer to update the published transforms
    ros::Timer frame_timer = n.createTimer(ros::Duration(0.01), frameCallback);

    server.reset(new interactive_markers::InteractiveMarkerServer("basic_controls", "", false));

    ros::Duration(0.1).sleep();

    // menu_handler.insert("First Entry", &processFeedback);
    // menu_handler.insert("Second Entry", &processFeedback);
    // interactive_markers::MenuHandler::EntryHandle sub_menu_handle = menu_handler.insert("Submenu");
    // menu_handler.insert(sub_menu_handle, "First Entry", &processFeedback);
    // menu_handler.insert(sub_menu_handle, "Second Entry", &processFeedback);

    menu_handler.insert("Gen Pose", &processFeedback);

    pub_lines = n.advertise<visualization_msgs::Marker>("/ray_cast_lines", 10);
    pub_space = n.advertise<visualization_msgs::MarkerArray>("/discovered_space", 10);
    pub_text = n.advertise<visualization_msgs::Marker>("/pose_text", 10);

    tf::Vector3 position;
    position = tf::Vector3(0, 0, 0);
    make6DofMarker(false, visualization_msgs::InteractiveMarkerControl::MOVE_ROTATE_3D, position, true);

    server->applyChanges();

    // ros::spin();

    while (ros::ok())
    {

        if (publish)
        {
            pub_lines.publish(line);
            pub_space.publish(single_view_boxes);
            pub_text.publish(text);
        }

        ros::spinOnce();

        ros::Duration(0.01).sleep();
    }

    server.reset();
}
// %EndTag(main)%