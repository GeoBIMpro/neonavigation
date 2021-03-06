/*
 * Copyright (c) 2014-2017, the neonavigation authors
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
 *     * Neither the name of the copyright holder nor the names of its 
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
#include <geometry_msgs/PolygonStamped.h>
#include <nav_msgs/OccupancyGrid.h>
#include <sensor_msgs/PointCloud.h>

#include <string>
#include <vector>

#include <costmap_cspace/node_handle_float.h>
#include <costmap_cspace/CSpace3D.h>
#include <costmap_cspace/CSpace3DUpdate.h>

#include <costmap_3d.h>

class Costmap3DOFNode : public costmap_cspace::Costmap3DOF
{
protected:
  ros::NodeHandle_f nh_;
  ros::NodeHandle_f nhp_;
  ros::Subscriber sub_map_;
  ros::Subscriber sub_map_overlay_;
  ros::Publisher pub_costmap_;
  ros::Publisher pub_costmap_update_;
  ros::Publisher pub_footprint_;
  ros::Publisher pub_debug_;

  void cbMap(const nav_msgs::OccupancyGrid::ConstPtr &msg)
  {
    if (getAngularGrid() <= 0)
    {
      ROS_ERROR("ang_resolution is not set.");
      std::runtime_error("ang_resolution is not set.");
    }
    ROS_INFO("2D costmap received");

    setBaseMap(*msg);
    processMap();
    ROS_DEBUG("C-Space costmap generated");

    pub_costmap_.publish(map_);
    publishDebug(map_);
  }
  void cbMapOverlay(const nav_msgs::OccupancyGrid::ConstPtr &msg)
  {
    ROS_DEBUG("Overlay 2D costmap received");
    if (map_.header.frame_id != msg->header.frame_id)
    {
      ROS_ERROR("map and map_overlay must have same frame_id");
      return;
    }
    if (map_.info.width < 1 ||
        map_.info.height < 1)
      return;

    costmap_cspace::CSpace3DUpdate update = processMapOverlay(*msg);
    ROS_DEBUG("C-Space costmap updated");

    publishDebug(map_overlay_);
    pub_costmap_update_.publish(update);
  }
  void publishDebug(costmap_cspace::CSpace3D &map_)
  {
    sensor_msgs::PointCloud pc;
    pc.header = map_.header;
    pc.header.stamp = ros::Time::now();
    for (size_t yaw = 0; yaw < map_.info.angle; yaw++)
    {
      for (unsigned int i = 0; i < map_.info.width * map_.info.height; i++)
      {
        int gx = i % map_.info.width;
        int gy = i / map_.info.width;
        if (map_.data[i + yaw * map_.info.width * map_.info.height] < 100)
          continue;
        geometry_msgs::Point32 p;
        p.x = gx * map_.info.linear_resolution + map_.info.origin.position.x;
        p.y = gy * map_.info.linear_resolution + map_.info.origin.position.y;
        p.z = yaw * 0.1;
        pc.points.push_back(p);
      }
    }
    pub_debug_.publish(pc);
  }

public:
  void spin()
  {
    ros::Rate wait(1);
    while (ros::ok())
    {
      wait.sleep();
      ros::spinOnce();
      footprint_.header.stamp = ros::Time::now();
      pub_footprint_.publish(footprint_);
    }
  }
  Costmap3DOFNode()
    : nh_()
    , nhp_("~")
  {
    int ang_resolution;
    float linear_expand;
    float linear_spread;
    int unknown_cost;
    map_overlay_mode overlay_mode;
    nhp_.param("ang_resolution", ang_resolution, 16);
    nhp_.param("linear_expand", linear_expand, 0.2f);
    nhp_.param("linear_spread", linear_spread, 0.5f);
    nhp_.param("unknown_cost", unknown_cost, 0);
    std::string overlay_mode_str;
    nhp_.param("overlay_mode", overlay_mode_str, std::string("max"));
    if (overlay_mode_str.compare("overwrite") == 0)
    {
      overlay_mode = OVERWRITE;
    }
    else if (overlay_mode_str.compare("max") == 0)
    {
      overlay_mode = MAX;
    }
    else
    {
      ROS_ERROR("Unknown overlay_mode \"%s\"", overlay_mode_str.c_str());
      throw std::runtime_error("Unknown overlay_mode.");
    }
    ROS_INFO("costmap_3d: %s mode", overlay_mode_str.c_str());
    setCSpaceConfig(ang_resolution, linear_expand, linear_spread, unknown_cost, overlay_mode);

    XmlRpc::XmlRpcValue footprint_xml;
    if (!nhp_.hasParam("footprint"))
    {
      ROS_FATAL("Footprint doesn't specified");
      throw std::runtime_error("Footprint doesn't specified.");
    }
    nhp_.getParam("footprint", footprint_xml);
    if (!setFootprint(footprint_xml))
    {
      ROS_FATAL("Invalid footprint");
      throw std::runtime_error("Invalid footprint");
    }
    ROS_INFO("footprint radius: %0.3f", footprint_radius_);

    sub_map_ = nh_.subscribe("map", 1, &Costmap3DOFNode::cbMap, this);
    sub_map_overlay_ = nh_.subscribe("map_overlay", 1, &Costmap3DOFNode::cbMapOverlay, this);
    pub_costmap_ = nhp_.advertise<costmap_cspace::CSpace3D>("costmap", 1, true);
    pub_costmap_update_ = nhp_.advertise<costmap_cspace::CSpace3DUpdate>("costmap_update", 1, true);
    pub_footprint_ = nhp_.advertise<geometry_msgs::PolygonStamped>("footprint", 2, true);
    pub_debug_ = nhp_.advertise<sensor_msgs::PointCloud>("debug", 1, true);
  }
};

int main(int argc, char *argv[])
{
  ros::init(argc, argv, "costmap_3d");

  Costmap3DOFNode cm;
  cm.spin();

  return 0;
}
